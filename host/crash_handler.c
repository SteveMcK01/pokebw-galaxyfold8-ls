/*
 * host/crash_handler.c -- Native POSIX signal, wasm2c trap, and diagnostic log
 * exporter for Android (Termux/Shizuku/ADB and external save-folder retrieval).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "crash_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unwind.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unwind.h>
#endif

#ifndef POKEBW_BUILD_VERSION
#define POKEBW_BUILD_VERSION "dev"
#endif

#define CRASH_BUF_CAP       (32u * 1024u)
#define STDERR_TAIL_BYTES   (8u * 1024u)
#define DIAG_STDERR_BYTES   (32u * 1024u)
#define MAX_FRAMES          48
#define ALTSTACK_SLOTS      4
#define ALTSTACK_BYTES      (64u * 1024u)

static char g_dir_internal[1024];
static char g_dir_external[1024];
static char g_dir_save[1024];

static char g_path_internal[1152];
static char g_path_external[1152];
static char g_path_save[1152];
static char g_path_stderr[1152];

static volatile uintptr_t g_wasm_base = 0;
static volatile size_t    g_wasm_size = 0;

static int g_signals_installed = 0;

#if !defined(_WIN32)
static volatile sig_atomic_t g_in_crash = 0;
static unsigned char g_altstacks[ALTSTACK_SLOTS][ALTSTACK_BYTES];
static unsigned int g_altstack_next = 0;
#endif

static char g_crash_buf[CRASH_BUF_CAP];

struct CrashWriter {
    char  *buf;
    size_t len;
    size_t cap;
};

static void copy_trimmed_dir(char *dst, size_t dst_cap, const char *src)
{
    size_t len;

    if (dst == NULL || dst_cap == 0u) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src[0] == '\0') {
        return;
    }
    snprintf(dst, dst_cap, "%s", src);
    len = strlen(dst);
    while (len > 1u && (dst[len - 1u] == '/' || dst[len - 1u] == '\\')) {
        dst[--len] = '\0';
    }
}

static void join_file_path(char *dst, size_t dst_cap, const char *dir, const char *name)
{
    if (dst == NULL || dst_cap == 0u) {
        return;
    }
    dst[0] = '\0';
    if (dir == NULL || dir[0] == '\0' || name == NULL || name[0] == '\0') {
        return;
    }
    snprintf(dst, dst_cap, "%s/%s", dir, name);
}

static void cw_init(struct CrashWriter *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->len = 0u;
    w->cap = cap;
    if (cap > 0u) {
        buf[0] = '\0';
    }
}

static void cw_char(struct CrashWriter *w, char c)
{
    if (w->len + 1u < w->cap) {
        w->buf[w->len++] = c;
        w->buf[w->len] = '\0';
    }
}

static void cw_str(struct CrashWriter *w, const char *s)
{
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0' && w->len + 1u < w->cap) {
        w->buf[w->len++] = *s++;
    }
    if (w->cap > 0u) {
        w->buf[w->len] = '\0';
    }
}

static void cw_nstr(struct CrashWriter *w, const char *s, size_t n)
{
    size_t i;
    if (s == NULL) {
        return;
    }
    for (i = 0u; i < n && w->len + 1u < w->cap; i++) {
        w->buf[w->len++] = s[i];
    }
    if (w->cap > 0u) {
        w->buf[w->len] = '\0';
    }
}

static void cw_u64(struct CrashWriter *w, unsigned long long v)
{
    char tmp[32];
    int n = 0;

    if (v == 0ull) {
        cw_char(w, '0');
        return;
    }
    while (v > 0ull && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    while (n > 0) {
        cw_char(w, tmp[--n]);
    }
}

static void cw_i64(struct CrashWriter *w, long long v)
{
    unsigned long long uv;
    if (v < 0) {
        cw_char(w, '-');
        uv = (unsigned long long)(-(v + 1ll)) + 1ull;
    } else {
        uv = (unsigned long long)v;
    }
    cw_u64(w, uv);
}

static void cw_u64_pad2(struct CrashWriter *w, unsigned v)
{
    cw_char(w, (char)('0' + ((v / 10u) % 10u)));
    cw_char(w, (char)('0' + (v % 10u)));
}

static void cw_hex64(struct CrashWriter *w, unsigned long long v, int width)
{
    static const char k_hex[] = "0123456789abcdef";
    char tmp[16];
    int n = 0;

    cw_str(w, "0x");
    if (v == 0ull) {
        tmp[n++] = '0';
    } else {
        while (v > 0ull && n < 16) {
            tmp[n++] = k_hex[v & 0xFULL];
            v >>= 4;
        }
    }
    while (n < width && n < 16) {
        tmp[n++] = '0';
    }
    while (n > 0) {
        cw_char(w, tmp[--n]);
    }
}

/* Async-signal-safe UTC timestamp formatter (YYYY-MM-DD HH:MM:SS UTC). */
static void cw_utc_now(struct CrashWriter *w)
{
#if !defined(_WIN32)
    struct timespec ts;
    long long days, rem, z, era, yoe, doy, mp, d, m, y;
    unsigned doe;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        cw_str(w, "unknown");
        return;
    }
    days = (long long)(ts.tv_sec / 86400);
    rem = (long long)(ts.tv_sec % 86400);
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    /* Howard Hinnant's civil-from-days conversion (no allocations or tzset). */
    z = days + 719468ll;
    era = (z >= 0 ? z : z - 146096ll) / 146097ll;
    doe = (unsigned)(z - era * 146097ll);
    yoe = (long long)((doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u);
    y = yoe + era * 400ll;
    doy = (long long)doe - (365ll * yoe + yoe / 4ll - yoe / 100ll);
    mp = (5ll * doy + 2ll) / 153ll;
    d = doy - (153ll * mp + 2ll) / 5ll + 1ll;
    m = mp + (mp < 10ll ? 3ll : -9ll);
    y += (m <= 2ll ? 1ll : 0ll);

    cw_i64(w, y);
    cw_char(w, '-');
    cw_u64_pad2(w, (unsigned)m);
    cw_char(w, '-');
    cw_u64_pad2(w, (unsigned)d);
    cw_char(w, ' ');
    cw_u64_pad2(w, (unsigned)(rem / 3600ll));
    cw_char(w, ':');
    cw_u64_pad2(w, (unsigned)((rem % 3600ll) / 60ll));
    cw_char(w, ':');
    cw_u64_pad2(w, (unsigned)(rem % 60ll));
    cw_str(w, " UTC");
#else
    cw_u64(w, (unsigned long long)time(NULL));
#endif
}

static const char *path_basename(const char *p)
{
    const char *s;
    if (p == NULL) {
        return "?";
    }
    s = strrchr(p, '/');
    return s != NULL ? (s + 1) : p;
}

#if defined(__ANDROID__) || defined(__linux__)
struct UnwindBuf {
    uintptr_t frames[MAX_FRAMES];
    int       count;
    int       skip;
};

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context *ctx, void *arg)
{
    struct UnwindBuf *ub = (struct UnwindBuf *)arg;
    uintptr_t ip = (uintptr_t)_Unwind_GetIP(ctx);

    if (ip == 0u) {
        return _URC_NO_REASON;
    }
    if (ub->skip > 0) {
        ub->skip--;
        return _URC_NO_REASON;
    }
    if (ub->count < MAX_FRAMES) {
        ub->frames[ub->count++] = ip;
        return _URC_NO_REASON;
    }
    return _URC_END_OF_STACK;
}

static void cw_addr_sym(struct CrashWriter *w, uintptr_t addr)
{
    Dl_info info;

    cw_hex64(w, (unsigned long long)addr, 12);
    if (dladdr((const void *)addr, &info) != 0) {
        if (info.dli_fname != NULL && info.dli_fbase != NULL) {
            cw_str(w, "  ");
            cw_str(w, path_basename(info.dli_fname));
            cw_char(w, '+');
            cw_hex64(w, (unsigned long long)(addr - (uintptr_t)info.dli_fbase), 0);
        }
        if (info.dli_sname != NULL && info.dli_saddr != NULL) {
            cw_str(w, "  (");
            cw_str(w, info.dli_sname);
            cw_char(w, '+');
            cw_hex64(w, (unsigned long long)(addr - (uintptr_t)info.dli_saddr), 0);
            cw_char(w, ')');
        }
    }
}

static void cw_backtrace(struct CrashWriter *w, uintptr_t override_pc, uintptr_t override_lr)
{
    struct UnwindBuf ub;
    int i;

    ub.count = 0;
    ub.skip = 1;
    _Unwind_Backtrace(unwind_cb, &ub);

    cw_str(w, "Backtrace:\n");
    if (override_pc != 0u) {
        cw_str(w, "  #pc  ");
        cw_addr_sym(w, override_pc);
        cw_char(w, '\n');
    }
    if (override_lr != 0u && override_lr != override_pc) {
        cw_str(w, "  #lr  ");
        cw_addr_sym(w, override_lr);
        cw_char(w, '\n');
    }
    for (i = 0; i < ub.count; i++) {
        cw_str(w, "  #");
        cw_u64_pad2(w, (unsigned)i);
        cw_str(w, "  ");
        cw_addr_sym(w, ub.frames[i]);
        cw_char(w, '\n');
    }
    if (ub.count == 0 && override_pc == 0u) {
        cw_str(w, "  (no frames captured)\n");
    }
}
#endif

#if !defined(_WIN32)
static void cw_append_file_tail(struct CrashWriter *w, const char *path, size_t max_bytes)
{
    int fd;
    off_t sz, off;
    char tmp[1024];

    if (path == NULL || path[0] == '\0') {
        return;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }
    sz = lseek(fd, 0, SEEK_END);
    if (sz > 0) {
        off = (sz > (off_t)max_bytes) ? (sz - (off_t)max_bytes) : 0;
        if (lseek(fd, off, SEEK_SET) >= 0) {
            if (off > 0) {
                cw_str(w, "... [truncated] ...\n");
            }
            for (;;) {
                ssize_t nr = read(fd, tmp, sizeof(tmp));
                if (nr <= 0) {
                    if (nr < 0 && errno == EINTR) {
                        continue;
                    }
                    break;
                }
                cw_nstr(w, tmp, (size_t)nr);
                if (w->len + 2u >= w->cap) {
                    break;
                }
            }
        }
    }
    close(fd);
}

static void write_all_fd(int fd, const char *buf, size_t len)
{
    size_t done = 0u;
    while (done < len) {
        ssize_t nw = write(fd, buf + done, len - done);
        if (nw <= 0) {
            if (nw < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        done += (size_t)nw;
    }
}

static void write_crash_file(const char *path, const char *buf, size_t len)
{
    int fd;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        return;
    }
    write_all_fd(fd, buf, len);
    fsync(fd);
    close(fd);
}

static void emit_report_destinations(const char *buf, size_t len)
{
    /* Write external (Termux/Shizuku/ADB) first, then internal, then custom save dir. */
    write_crash_file(g_path_external, buf, len);
    if (g_path_internal[0] != '\0' && strcmp(g_path_internal, g_path_external) != 0) {
        write_crash_file(g_path_internal, buf, len);
    }
    if (g_path_save[0] != '\0' &&
        strcmp(g_path_save, g_path_external) != 0 &&
        strcmp(g_path_save, g_path_internal) != 0) {
        write_crash_file(g_path_save, buf, len);
    }
    write_all_fd(2, buf, len);

#if defined(__ANDROID__)
    {
        /* Emit header + backtrace lines to Android logcat (stop before stderr tail). */
        const char *cur = buf;
        const char *end = buf + len;
        int lines = 0;
        while (cur < end && lines < 40) {
            const char *nl = (const char *)memchr(cur, '\n', (size_t)(end - cur));
            size_t llen = nl ? (size_t)(nl - cur) : (size_t)(end - cur);
            char line[384];
            if (llen >= sizeof(line)) {
                llen = sizeof(line) - 1u;
            }
            memcpy(line, cur, llen);
            line[llen] = '\0';
            if (strncmp(line, "--- Recent stderr", 17) == 0) {
                break;
            }
            if (line[0] != '\0') {
                __android_log_write(ANDROID_LOG_FATAL, "PokeBW", line);
                lines++;
            }
            if (nl == NULL) {
                break;
            }
            cur = nl + 1;
        }
    }
#endif
}

static const char *sig_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGABRT: return "SIGABRT";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    default:      return "SIGNAL";
    }
}

static void cw_wasm_addr_hint(struct CrashWriter *w, uintptr_t fault_addr)
{
    uintptr_t base = g_wasm_base;
    size_t sz = g_wasm_size;

    if (base == 0u || fault_addr < base) {
        return;
    }
    /* wasm2c guard region can span up to 8 GiB past w2c_memory.data. */
    if ((unsigned long long)(fault_addr - base) < 0x200000000ULL) {
        cw_str(w, "Wasm Memory Offset: ");
        cw_hex64(w, (unsigned long long)(fault_addr - base), 8);
        cw_str(w, "  (active wasm memory size: ");
        cw_hex64(w, (unsigned long long)sz, 8);
        cw_str(w, ")\n");
    }
}

static void on_fatal_signal(int sig, siginfo_t *si, void *uctx)
{
    struct CrashWriter w;
    uintptr_t pc = 0u, lr = 0u;

    if (g_in_crash) {
        _exit(128 + sig);
    }
    g_in_crash = 1;

    cw_init(&w, g_crash_buf, sizeof(g_crash_buf));
    cw_str(&w, "=== PokeBW Fold Crash Report ===\n");
    cw_str(&w, "Time: ");
    cw_utc_now(&w);
    cw_str(&w, "\nVersion: " POKEBW_BUILD_VERSION "\nPID: ");
    cw_u64(&w, (unsigned long long)getpid());
    cw_str(&w, "  TID: ");
#if defined(__ANDROID__) || defined(__linux__)
    cw_u64(&w, (unsigned long long)syscall(SYS_gettid));
#else
    cw_u64(&w, (unsigned long long)getpid());
#endif
    cw_str(&w, "\nCause: ");
    cw_str(&w, sig_name(sig));
    cw_str(&w, " (");
    cw_i64(&w, (long long)sig);
    cw_str(&w, ")  si_code=");
    cw_i64(&w, si != NULL ? (long long)si->si_code : 0ll);
    if (si != NULL) {
        uintptr_t faddr = (uintptr_t)si->si_addr;
        cw_str(&w, "  fault_addr=");
        cw_hex64(&w, (unsigned long long)faddr, 12);
        cw_char(&w, '\n');
        cw_wasm_addr_hint(&w, faddr);
    } else {
        cw_char(&w, '\n');
    }

#if defined(__aarch64__) && (defined(__ANDROID__) || defined(__linux__))
    if (uctx != NULL) {
        const ucontext_t *uc = (const ucontext_t *)uctx;
        pc = (uintptr_t)uc->uc_mcontext.pc;
        lr = (uintptr_t)uc->uc_mcontext.regs[30];
        cw_str(&w, "ARM64 Registers:\n");
        cw_str(&w, "  pc=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.pc, 16);
        cw_str(&w, "  lr=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[30], 16);
        cw_str(&w, "  sp=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.sp, 16);
        cw_str(&w, "  fp=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[29], 16);
        cw_str(&w, "\n  pstate="); cw_hex64(&w, (unsigned long long)uc->uc_mcontext.pstate, 8);
        cw_str(&w, "  x0=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[0], 16);
        cw_str(&w, "  x1=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[1], 16);
        cw_str(&w, "\n  x2=");   cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[2], 16);
        cw_str(&w, "  x3=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[3], 16);
        cw_str(&w, "  x4=");     cw_hex64(&w, (unsigned long long)uc->uc_mcontext.regs[4], 16);
        cw_str(&w, "\n");
    }
#else
    (void)uctx;
#endif

#if defined(__ANDROID__) || defined(__linux__)
    cw_backtrace(&w, pc, lr);
#endif

    if (g_path_stderr[0] != '\0') {
        cw_str(&w, "\n--- Recent stderr (last 8 KiB) ---\n");
        cw_append_file_tail(&w, g_path_stderr, STDERR_TAIL_BYTES);
    }

    emit_report_destinations(w.buf, w.len);

    signal(sig, SIG_DFL);
    raise(sig);
}
#endif /* !_WIN32 */

void host_crash_install_thread_altstack(void)
{
#if !defined(_WIN32)
    unsigned int idx = __atomic_fetch_add(&g_altstack_next, 1u, __ATOMIC_RELAXED);
    stack_t ss;

    if (idx >= ALTSTACK_SLOTS) {
        return;
    }
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altstacks[idx];
    ss.ss_size = ALTSTACK_BYTES;
    ss.ss_flags = 0;
    (void)sigaltstack(&ss, NULL);
#endif
}

void host_crash_set_save_dir(const char *save_dir)
{
    copy_trimmed_dir(g_dir_save, sizeof(g_dir_save), save_dir);
    join_file_path(g_path_save, sizeof(g_path_save), g_dir_save, "pokebw_crash.log");
}

void host_crash_set_wasm_mem(const void *base, size_t size)
{
    g_wasm_base = (uintptr_t)base;
    g_wasm_size = size;
}

void host_crash_init(const char *internal_dir,
                     const char *external_dir,
                     const char *save_dir,
                     const char *stderr_path)
{
    copy_trimmed_dir(g_dir_internal, sizeof(g_dir_internal), internal_dir);
    copy_trimmed_dir(g_dir_external, sizeof(g_dir_external), external_dir);
    join_file_path(g_path_internal, sizeof(g_path_internal), g_dir_internal, "pokebw_crash.log");
    join_file_path(g_path_external, sizeof(g_path_external), g_dir_external, "pokebw_crash.log");
    host_crash_set_save_dir(save_dir);

    g_path_stderr[0] = '\0';
    if (stderr_path != NULL && stderr_path[0] != '\0') {
        snprintf(g_path_stderr, sizeof(g_path_stderr), "%s", stderr_path);
    }

#if !defined(_WIN32)
    if (!g_signals_installed) {
        static const int k_sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE };
        struct sigaction sa;
        size_t i;

        host_crash_install_thread_altstack();
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_fatal_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
        for (i = 0u; i < sizeof(k_sigs) / sizeof(k_sigs[0]); i++) {
            (void)sigaction(k_sigs[i], &sa, NULL);
        }
        g_signals_installed = 1;
    }
#endif
}

void host_crash_report_trap(int trap_code, const char *trap_msg)
{
    struct CrashWriter w;

#if !defined(_WIN32)
    if (g_in_crash) {
        return;
    }
    g_in_crash = 1;
#endif

    cw_init(&w, g_crash_buf, sizeof(g_crash_buf));
    cw_str(&w, "=== PokeBW Fold Crash Report (wasm2c trap) ===\n");
    cw_str(&w, "Time: ");
    cw_utc_now(&w);
    cw_str(&w, "\nVersion: " POKEBW_BUILD_VERSION);
#if !defined(_WIN32)
    cw_str(&w, "\nPID: ");
    cw_u64(&w, (unsigned long long)getpid());
#if defined(__ANDROID__) || defined(__linux__)
    cw_str(&w, "  TID: ");
    cw_u64(&w, (unsigned long long)syscall(SYS_gettid));
#endif
#endif
    cw_str(&w, "\nCause: wasm trap: ");
    cw_str(&w, trap_msg != NULL ? trap_msg : "unknown");
    cw_str(&w, " (code ");
    cw_i64(&w, (long long)trap_code);
    cw_str(&w, ")\n");
    if (g_wasm_size > 0u) {
        cw_str(&w, "Active wasm memory size: ");
        cw_hex64(&w, (unsigned long long)g_wasm_size, 8);
        cw_char(&w, '\n');
    }

#if defined(__ANDROID__) || defined(__linux__)
    cw_backtrace(&w, 0u, 0u);
#endif

#if !defined(_WIN32)
    if (g_path_stderr[0] != '\0') {
        cw_str(&w, "\n--- Recent stderr (last 8 KiB) ---\n");
        cw_append_file_tail(&w, g_path_stderr, STDERR_TAIL_BYTES);
    }
    emit_report_destinations(w.buf, w.len);
#else
    fprintf(stderr, "%s\n", w.buf);
#endif
}

static int file_exists_nonempty(const char *path)
{
    FILE *f;
    long sz;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fclose(f);
    return sz > 0;
}

int host_crash_has_log(void)
{
    return file_exists_nonempty(g_path_external) ||
           file_exists_nonempty(g_path_internal) ||
           file_exists_nonempty(g_path_save);
}

const char *host_crash_primary_path(void)
{
    if (g_path_external[0] != '\0') {
        return g_path_external;
    }
    if (g_path_save[0] != '\0') {
        return g_path_save;
    }
    if (g_path_internal[0] != '\0') {
        return g_path_internal;
    }
    return "pokebw_crash.log";
}

static int write_diag_to(const char *dst_path, const char *crash_src)
{
    FILE *out;
    FILE *in;
    char buf[4096];
    size_t n;

    if (dst_path == NULL || dst_path[0] == '\0') {
        return -1;
    }
    out = fopen(dst_path, "wb");
    if (out == NULL) {
        return -1;
    }
    fprintf(out, "=== PokeBW Fold Diagnostic Export ===\n");
    fprintf(out, "Version: %s\n", POKEBW_BUILD_VERSION);
    fprintf(out, "External log: %s\n", g_path_external[0] ? g_path_external : "(none)");
    fprintf(out, "Internal log: %s\n", g_path_internal[0] ? g_path_internal : "(none)");
    fprintf(out, "Save dir log: %s\n\n", g_path_save[0] ? g_path_save : "(none)");

    if (crash_src != NULL && (in = fopen(crash_src, "rb")) != NULL) {
        fprintf(out, "--- Last pokebw_crash.log (%s) ---\n", crash_src);
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        fclose(in);
        fprintf(out, "\n");
    } else {
        fprintf(out, "(No prior pokebw_crash.log found)\n\n");
    }

    if (g_path_stderr[0] != '\0' && (in = fopen(g_path_stderr, "rb")) != NULL) {
        long sz;
        fprintf(out, "--- Recent stderr.txt (last 32 KiB) ---\n");
        if (fseek(in, 0, SEEK_END) == 0 && (sz = ftell(in)) > 0) {
            long start = (sz > (long)DIAG_STDERR_BYTES) ? (sz - (long)DIAG_STDERR_BYTES) : 0L;
            fseek(in, start, SEEK_SET);
            if (start > 0L) {
                fprintf(out, "... [truncated] ...\n");
            }
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                fwrite(buf, 1, n, out);
            }
        }
        fclose(in);
    }

    fclose(out);
    return 0;
}

int host_crash_export_diag(char *out_status, size_t out_cap)
{
    const char *crash_src = NULL;
    char ext_diag[1152];
    char save_diag[1152];
    int wrote_ext = 0, wrote_save = 0;

    if (file_exists_nonempty(g_path_external)) {
        crash_src = g_path_external;
    } else if (file_exists_nonempty(g_path_internal)) {
        crash_src = g_path_internal;
    } else if (file_exists_nonempty(g_path_save)) {
        crash_src = g_path_save;
    }

    join_file_path(ext_diag, sizeof(ext_diag), g_dir_external, "pokebw_diag.log");
    join_file_path(save_diag, sizeof(save_diag), g_dir_save, "pokebw_diag.log");

    if (ext_diag[0] != '\0' && write_diag_to(ext_diag, crash_src) == 0) {
        wrote_ext = 1;
    }
    if (save_diag[0] != '\0' && strcmp(save_diag, ext_diag) != 0 &&
        write_diag_to(save_diag, crash_src) == 0) {
        wrote_save = 1;
    }

    if (out_status != NULL && out_cap > 0u) {
        if (wrote_ext && wrote_save) {
            snprintf(out_status, out_cap, "Exported to:\n%s\nand %s", ext_diag, save_diag);
        } else if (wrote_ext) {
            snprintf(out_status, out_cap, "Exported to:\n%s", ext_diag);
        } else if (wrote_save) {
            snprintf(out_status, out_cap, "Exported to:\n%s", save_diag);
        } else {
            snprintf(out_status, out_cap, "Export failed: no writable diagnostic folder.");
        }
    }
    return (wrote_ext || wrote_save) ? 0 : -1;
}
