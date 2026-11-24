/*
 * host/pokebw_settings.c -- Persistent INI configuration for PokeBW Fold.
 */

#include "pokebw_settings.h"
#include "pokebw_settings_ids.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PokebwSettings g_settings;
static char g_settings_path[1024] = "pokebw_settings.ini";
static int g_initialized = 0;
static int g_eidos_opt[POKEBW_EIDOS_OPT_COUNT];
static unsigned char g_eidos_dirty[POKEBW_EIDOS_OPT_COUNT];
static int g_pc_req_pending = 0;
static int g_pc_req_mode = 2;
static unsigned long long g_pc_req_ms = 0;
static int g_pc_last_status = 0;

static void apply_control_defaults(PokebwSettings *s)
{
    s->touch_visible = 1;
    s->touch_opacity = 100;
    s->touch_scale   = 100;
    s->show_fps      = 0;
}

static void apply_all_defaults(PokebwSettings *s)
{
    memset(s, 0, sizeof(*s));
    apply_control_defaults(s);
    s->trade_evo_fix = 1;
    s->portable_pc = 1;
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static char *trim_ws(char *p)
{
    size_t len;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1])) {
        p[--len] = '\0';
    }
    return p;
}

static int parse_bool(const char *v)
{
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 ||
        strcmp(v, "TRUE") == 0 || strcmp(v, "yes") == 0 ||
        strcmp(v, "on") == 0) {
        return 1;
    }
    return 0;
}

static void parse_ini_line(const char *section, const char *key, const char *val)
{
    if (strcmp(section, "TouchControls") == 0) {
        if (strcmp(key, "touch_visible") == 0) g_settings.touch_visible = parse_bool(val);
        else if (strcmp(key, "touch_opacity") == 0) g_settings.touch_opacity = clamp_int(atoi(val), 15, 100);
        else if (strcmp(key, "touch_scale") == 0) g_settings.touch_scale = clamp_int(atoi(val), 70, 140);
    } else if (strcmp(section, "Display") == 0) {
        if (strcmp(key, "show_fps") == 0) g_settings.show_fps = parse_bool(val);
        else if (strcmp(key, "fold_pip_mode") == 0) g_settings.fold_pip_mode = parse_bool(val);
    } else if (strcmp(section, "Gameplay") == 0) {
        if (strcmp(key, "trade_evo_fix") == 0) g_settings.trade_evo_fix = parse_bool(val);
        else if (strcmp(key, "modern_exp_share") == 0) g_settings.modern_exp_share = parse_bool(val);
        else if (strcmp(key, "hm_freedom") == 0) g_settings.hm_freedom = parse_bool(val);
        else if (strcmp(key, "hm_no_badges") == 0) g_settings.hm_no_badges = parse_bool(val);
        else if (strcmp(key, "catch_em_all") == 0) g_settings.catch_em_all = parse_bool(val);
        else if (strcmp(key, "portable_pc") == 0) g_settings.portable_pc = parse_bool(val);
    } else if (strcmp(section, "QoL") == 0) {
        if (strcmp(key, "universal_tm") == 0) g_settings.universal_tm = parse_bool(val);
        else if (strcmp(key, "hard_level_cap") == 0) g_settings.hard_level_cap = parse_bool(val);
        else if (strcmp(key, "no_random_encounters") == 0) g_settings.no_random_encounters = parse_bool(val);
        else if (strcmp(key, "always_bite_fishing") == 0) g_settings.always_bite_fishing = parse_bool(val);
        else if (strcmp(key, "instant_text") == 0) g_settings.instant_text = parse_bool(val);
    }
}

static void load_ini(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    char section[64] = "";

    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *s = trim_ws(line);
        char *eq;

        if (*s == '\0' || *s == '#' || *s == ';') {
            continue;
        }
        if (*s == '[') {
            char *end = strchr(s + 1, ']');
            if (end != NULL) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", trim_ws(s + 1));
            }
            continue;
        }
        eq = strchr(s, '=');
        if (eq != NULL) {
            char *k;
            char *v;
            *eq = '\0';
            k = trim_ws(s);
            v = trim_ws(eq + 1);
            parse_ini_line(section, k, v);
        }
    }
    fclose(f);
}

static void init_eidos_opt_defaults(void)
{
    memset(g_eidos_opt, 0, sizeof(g_eidos_opt));
    memset(g_eidos_dirty, 0, sizeof(g_eidos_dirty));
    g_eidos_opt[POKEBW_EIDOS_OPT_LAYOUT] = 2; /* One Screen */
    g_eidos_opt[POKEBW_EIDOS_OPT_VOLUME] = 50;
    g_eidos_opt[POKEBW_EIDOS_OPT_MOVE_COLOURS] = 1;
    g_eidos_opt[POKEBW_EIDOS_OPT_VSYNC] = 1;
}

void host_settings_init(const char *profile_dir)
{
    apply_all_defaults(&g_settings);
    init_eidos_opt_defaults();
    if (profile_dir != NULL && profile_dir[0] != '\0') {
        size_t len = strlen(profile_dir);
        if (profile_dir[len - 1] == '/' || profile_dir[len - 1] == '\\') {
            snprintf(g_settings_path, sizeof(g_settings_path), "%spokebw_settings.ini", profile_dir);
        } else {
            snprintf(g_settings_path, sizeof(g_settings_path), "%s/pokebw_settings.ini", profile_dir);
        }
    } else {
        snprintf(g_settings_path, sizeof(g_settings_path), "pokebw_settings.ini");
    }
    g_initialized = 1;
    load_ini(g_settings_path);
    if (g_settings.instant_text != 0) {
        g_eidos_opt[POKEBW_EIDOS_OPT_INSTANT_TEXT] = 1;
        g_eidos_dirty[POKEBW_EIDOS_OPT_INSTANT_TEXT] = 1;
    }
}

PokebwSettings *host_settings_get(void)
{
    if (!g_initialized) {
        host_settings_init(NULL);
    }
    return &g_settings;
}

int host_settings_get_by_id(int id)
{
    const PokebwSettings *s = host_settings_get();

    switch (id) {
    case POKEBW_SET_TOUCH_VISIBLE:        return s->touch_visible;
    case POKEBW_SET_TOUCH_OPACITY:        return s->touch_opacity;
    case POKEBW_SET_TOUCH_SCALE:          return s->touch_scale;
    case POKEBW_SET_SHOW_FPS:             return s->show_fps;
    case POKEBW_SET_TRADE_EVO_FIX:        return s->trade_evo_fix;
    case POKEBW_SET_MODERN_EXP_SHARE:     return s->modern_exp_share;
    case POKEBW_SET_HM_FREEDOM:           return s->hm_freedom;
    case POKEBW_SET_HM_NO_BADGES:         return s->hm_no_badges;
    case POKEBW_SET_CATCH_EM_ALL:         return s->catch_em_all;
    case POKEBW_SET_PORTABLE_PC:          return s->portable_pc;
    case POKEBW_SET_FOLD_PIP_MODE:        return s->fold_pip_mode;
    case POKEBW_SET_UNIVERSAL_TM:         return s->universal_tm;
    case POKEBW_SET_HARD_LEVEL_CAP:       return s->hard_level_cap;
    case POKEBW_SET_NO_RANDOM_ENCOUNTERS: return s->no_random_encounters;
    case POKEBW_SET_ALWAYS_BITE_FISHING:  return s->always_bite_fishing;
    case POKEBW_SET_INSTANT_TEXT:         return s->instant_text;
    default:                              return 0;
    }
}

void host_settings_reset_controls(void)
{
    apply_control_defaults(host_settings_get());
    (void)host_settings_save();
}

void host_settings_reset_defaults(void)
{
    apply_all_defaults(host_settings_get());
    host_eidos_opt_set(POKEBW_EIDOS_OPT_INSTANT_TEXT, 0);
    (void)host_settings_save();
}

static int write_ini_file(const char *path, const PokebwSettings *s)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    fprintf(f,
            "# PokeBW Fold Settings\n\n"
            "[TouchControls]\n"
            "touch_visible = %d\n"
            "touch_opacity = %d\n"
            "touch_scale = %d\n\n"
            "[Display]\n"
            "show_fps = %d\n"
            "fold_pip_mode = %d\n\n"
            "[Gameplay]\n"
            "trade_evo_fix = %d\n"
            "modern_exp_share = %d\n"
            "hm_freedom = %d\n"
            "hm_no_badges = %d\n"
            "catch_em_all = %d\n"
            "portable_pc = %d\n\n"
            "[QoL]\n"
            "universal_tm = %d\n"
            "hard_level_cap = %d\n"
            "no_random_encounters = %d\n"
            "always_bite_fishing = %d\n"
            "instant_text = %d\n",
            s->touch_visible ? 1 : 0,
            clamp_int(s->touch_opacity, 15, 100),
            clamp_int(s->touch_scale, 70, 140),
            s->show_fps ? 1 : 0,
            s->fold_pip_mode ? 1 : 0,
            s->trade_evo_fix ? 1 : 0,
            s->modern_exp_share ? 1 : 0,
            s->hm_freedom ? 1 : 0,
            s->hm_no_badges ? 1 : 0,
            s->catch_em_all ? 1 : 0,
            s->portable_pc ? 1 : 0,
            s->universal_tm ? 1 : 0,
            s->hard_level_cap ? 1 : 0,
            s->no_random_encounters ? 1 : 0,
            s->always_bite_fishing ? 1 : 0,
            s->instant_text ? 1 : 0);
    if (ferror(f) || fclose(f) != 0) {
        return -1;
    }
    return 0;
}

int host_settings_save(void)
{
    const PokebwSettings *s = host_settings_get();
    char tmp_path[1040];

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_settings_path);
    if (write_ini_file(tmp_path, s) == 0) {
        if (rename(tmp_path, g_settings_path) == 0) {
            return 0;
        }
        (void)remove(tmp_path);
    }
    return write_ini_file(g_settings_path, s);
}

const char *host_settings_path(void)
{
    if (!g_initialized) {
        host_settings_init(NULL);
    }
    return g_settings_path;
}

void host_eidos_opt_from_wasm(int id, int value)
{
    if (!g_initialized) {
        host_settings_init(NULL);
    }
    if (id < 0 || id >= POKEBW_EIDOS_OPT_COUNT) {
        return;
    }
    if (g_eidos_dirty[id]) {
        return;
    }
    g_eidos_opt[id] = value;
    if (id == POKEBW_EIDOS_OPT_INSTANT_TEXT) {
        g_settings.instant_text = value ? 1 : 0;
    }
}

int host_eidos_opt_get(int id)
{
    if (!g_initialized) {
        host_settings_init(NULL);
    }
    if (id < 0 || id >= POKEBW_EIDOS_OPT_COUNT) {
        return 0;
    }
    return g_eidos_opt[id];
}

void host_eidos_opt_set(int id, int value)
{
    if (!g_initialized) {
        host_settings_init(NULL);
    }
    if (id < 0 || id >= POKEBW_EIDOS_OPT_COUNT) {
        return;
    }
    g_eidos_opt[id] = value;
    g_eidos_dirty[id] = 1;
    if (id == POKEBW_EIDOS_OPT_INSTANT_TEXT) {
        g_settings.instant_text = value ? 1 : 0;
        (void)host_settings_save();
    }
}

unsigned host_eidos_opt_poll_pending(void)
{
    int id;

    for (id = 0; id < POKEBW_EIDOS_OPT_COUNT; id++) {
        if (g_eidos_dirty[id]) {
            g_eidos_dirty[id] = 0;
            return (((unsigned)(id + 1)) << 16) | ((unsigned)g_eidos_opt[id] & 0xFFFFu);
        }
    }
    return 0u;
}

void host_pc_request_open(int mode, unsigned long long now_ms)
{
    g_pc_req_mode = clamp_int(mode, 0, 3);
    g_pc_req_pending = 1;
    g_pc_req_ms = now_ms;
    g_pc_last_status = 0;
}

unsigned host_pc_request_poll(unsigned long long now_ms)
{
    if (!g_pc_req_pending) {
        return 0u;
    }
    if (now_ms > g_pc_req_ms && (now_ms - g_pc_req_ms) > 1500ull) {
        g_pc_req_pending = 0;
        g_pc_last_status = -1;
        return 0u;
    }
    g_pc_req_pending = 0;
    return (unsigned)(g_pc_req_mode + 1);
}

void host_pc_request_set_result(int status)
{
    g_pc_last_status = status;
}

int host_pc_last_status(void)
{
    return g_pc_last_status;
}
