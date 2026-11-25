/*
 * host/crash_handler.h -- Native POSIX signal, wasm2c trap, and diagnostic log
 * exporter for Android (Termux/Shizuku/ADB and save-folder access).
 */

#ifndef EIDOS_HOST_CRASH_HANDLER_H
#define EIDOS_HOST_CRASH_HANDLER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize crash log destinations, install signal handlers & thread altstack. */
void host_crash_init(const char *internal_dir,
                     const char *external_dir,
                     const char *save_dir,
                     const char *stderr_path);

/* Update the effective save directory (mirrors crash & diag logs there too). */
void host_crash_set_save_dir(const char *save_dir);

/* Record wasm linear memory base/size so native signals can report wasm offsets. */
void host_crash_set_wasm_mem(const void *base, size_t size);

/* Install an alternate signal stack on the calling thread (safe for stack overflow). */
void host_crash_install_thread_altstack(void);

/* Capture backtrace + stderr tail on a wasm2c trap and write crash log files. */
void host_crash_report_trap(int trap_code, const char *trap_msg);

/* UI / diagnostics helpers. */
int         host_crash_has_log(void);
const char *host_crash_primary_path(void);
int         host_crash_export_diag(char *out_status, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_HOST_CRASH_HANDLER_H */
