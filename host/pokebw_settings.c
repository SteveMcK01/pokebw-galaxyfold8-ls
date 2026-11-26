/*
 * host/pokebw_settings.c -- Persistent INI configuration for PokeBW Fold.
 */

#include "crash_handler.h"
#include "pokebw_settings.h"
#include "pokebw_settings_ids.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#endif

static PokebwSettings g_settings;
static char g_settings_path[1024] = "pokebw_settings.ini";
static int g_initialized = 0;
static int g_eidos_opt[POKEBW_EIDOS_OPT_COUNT];
static unsigned char g_eidos_dirty[POKEBW_EIDOS_OPT_COUNT];
static int g_pc_req_pending = 0;
static int g_pc_req_mode = 2;
static unsigned long long g_pc_req_ms = 0;
static int g_pc_last_status = 0;
static int g_hm_req_pending = 0;
static int g_hm_req_skill = 0;
static unsigned long long g_hm_req_ms = 0;
static int g_hm_last_status = 0;
static unsigned g_sort_req_word = 0u;
static int g_sort_req_kind = 0;
static int g_sort_req_mode = 0;
static int g_sort_req_target = 0;
static unsigned long long g_sort_req_ms = 0;
static char g_sort_status[160] = "";
static char g_default_save_dir[1024] = "";
static int g_save_dir_dirty = 0;
static char g_save_dir_status[160] = "";
static PokebwGearSnapshot g_gear_snap;
static char g_backup_status[160] = "";
static unsigned g_dex_req_word = 0u;
static unsigned long long g_dex_req_ms = 0;
static PokebwDexIndex g_dex_index;
static PokebwDexDetail g_dex_detail;
static char g_dex_status[160] = "";

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
    s->following_pokemon = 1;
    s->overworld_wilds = 1;
    s->overworld_wild_density = 4;
    s->type_effectiveness = 1;
    s->repel_prompt = 1;
    s->ball_carousel = 1;
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
        else if (strcmp(key, "following_pokemon") == 0) g_settings.following_pokemon = parse_bool(val);
        else if (strcmp(key, "overworld_wilds") == 0) g_settings.overworld_wilds = parse_bool(val);
        else if (strcmp(key, "overworld_wild_density") == 0) g_settings.overworld_wild_density = clamp_int(atoi(val), 1, 6);
    } else if (strcmp(section, "QoL") == 0) {
        if (strcmp(key, "hard_level_cap") == 0) g_settings.hard_level_cap = parse_bool(val);
        else if (strcmp(key, "no_random_encounters") == 0) g_settings.no_random_encounters = parse_bool(val);
        else if (strcmp(key, "always_bite_fishing") == 0) g_settings.always_bite_fishing = parse_bool(val);
        else if (strcmp(key, "instant_text") == 0) g_settings.instant_text = parse_bool(val);
        else if (strcmp(key, "season_override") == 0) g_settings.season_override = clamp_int(atoi(val), 0, 4);
        else if (strcmp(key, "clock_override") == 0) g_settings.clock_override = clamp_int(atoi(val), 0, 4);
        else if (strcmp(key, "type_effectiveness") == 0) g_settings.type_effectiveness = parse_bool(val);
        else if (strcmp(key, "fast_forward_speed") == 0) g_settings.fast_forward_speed = clamp_int(atoi(val), 0, 3);
        else if (strcmp(key, "shiny_odds") == 0) g_settings.shiny_odds = clamp_int(atoi(val), 0, 4);
        else if (strcmp(key, "repel_prompt") == 0) g_settings.repel_prompt = parse_bool(val);
        else if (strcmp(key, "ball_carousel") == 0) g_settings.ball_carousel = parse_bool(val);
    } else if (strcmp(section, "Save") == 0) {
        if (strcmp(key, "save_dir") == 0) {
            snprintf(g_settings.save_dir, sizeof(g_settings.save_dir), "%s", val);
        }
    }
}

static void load_ini(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1200];
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
            snprintf(g_default_save_dir, sizeof(g_default_save_dir), "%ssaves", profile_dir);
        } else {
            snprintf(g_settings_path, sizeof(g_settings_path), "%s/pokebw_settings.ini", profile_dir);
            snprintf(g_default_save_dir, sizeof(g_default_save_dir), "%s/saves", profile_dir);
        }
    } else {
        snprintf(g_settings_path, sizeof(g_settings_path), "pokebw_settings.ini");
        snprintf(g_default_save_dir, sizeof(g_default_save_dir), "saves");
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
    case POKEBW_SET_FOLLOWING_POKEMON:    return s->following_pokemon;
    case POKEBW_SET_OVERWORLD_WILDS:      return s->overworld_wilds;
    case POKEBW_SET_OVERWORLD_WILD_DENSITY: return clamp_int(s->overworld_wild_density, 1, 6);
    case POKEBW_SET_FOLD_PIP_MODE:        return s->fold_pip_mode;
    case POKEBW_SET_HARD_LEVEL_CAP:       return s->hard_level_cap;
    case POKEBW_SET_NO_RANDOM_ENCOUNTERS: return s->no_random_encounters;
    case POKEBW_SET_ALWAYS_BITE_FISHING:  return s->always_bite_fishing;
    case POKEBW_SET_INSTANT_TEXT:         return s->instant_text;
    case POKEBW_SET_SEASON_OVERRIDE:      return s->season_override;
    case POKEBW_SET_CLOCK_OVERRIDE:       return s->clock_override;
    case POKEBW_SET_TYPE_EFFECTIVENESS:   return s->type_effectiveness;
    case POKEBW_SET_FAST_FORWARD:         return s->fast_forward ? 1 : 0;
    case POKEBW_SET_FAST_FORWARD_SPEED:   return clamp_int(s->fast_forward_speed, 0, 3);
    case POKEBW_SET_SHINY_ODDS:           return clamp_int(s->shiny_odds, 0, 4);
    case POKEBW_SET_REPEL_PROMPT:         return s->repel_prompt;
    case POKEBW_SET_BALL_CAROUSEL:        return s->ball_carousel;
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
    PokebwSettings *s = host_settings_get();
    int had_custom_dir = (s->save_dir[0] != '\0');

    apply_all_defaults(s);
    if (had_custom_dir) {
        g_save_dir_dirty = 1;
        snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                 "Save folder reset to default. Restart app if switching existing saves.");
    }
    host_eidos_opt_set(POKEBW_EIDOS_OPT_INSTANT_TEXT, 0);
    host_crash_set_save_dir(host_settings_effective_save_dir());
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
            "portable_pc = %d\n"
            "following_pokemon = %d\n"
            "overworld_wilds = %d\n"
            "overworld_wild_density = %d\n\n"
            "[QoL]\n"
            "hard_level_cap = %d\n"
            "no_random_encounters = %d\n"
            "always_bite_fishing = %d\n"
            "instant_text = %d\n"
            "season_override = %d\n"
            "clock_override = %d\n"
            "type_effectiveness = %d\n"
            "fast_forward_speed = %d\n"
            "shiny_odds = %d\n"
            "repel_prompt = %d\n"
            "ball_carousel = %d\n\n"
            "[Save]\n"
            "save_dir = %s\n",
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
            s->following_pokemon ? 1 : 0,
            s->overworld_wilds ? 1 : 0,
            clamp_int(s->overworld_wild_density, 1, 6),
            s->hard_level_cap ? 1 : 0,
            s->no_random_encounters ? 1 : 0,
            s->always_bite_fishing ? 1 : 0,
            s->instant_text ? 1 : 0,
            clamp_int(s->season_override, 0, 4),
            clamp_int(s->clock_override, 0, 4),
            s->type_effectiveness ? 1 : 0,
            clamp_int(s->fast_forward_speed, 0, 3),
            clamp_int(s->shiny_odds, 0, 4),
            s->repel_prompt ? 1 : 0,
            s->ball_carousel ? 1 : 0,
            s->save_dir);
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

void host_hm_request_use(int skill_index, unsigned long long now_ms)
{
    if (skill_index < 0 || skill_index > 15) {
        return;
    }
    g_hm_req_skill = skill_index;
    g_hm_req_pending = 1;
    g_hm_req_ms = now_ms;
    g_hm_last_status = 0;
}

unsigned host_hm_request_poll(unsigned long long now_ms)
{
    if (!g_hm_req_pending) {
        return 0u;
    }
    if (now_ms > g_hm_req_ms && (now_ms - g_hm_req_ms) > 1500ull) {
        g_hm_req_pending = 0;
        g_hm_last_status = -1;
        return 0u;
    }
    g_hm_req_pending = 0;
    return (unsigned)(g_hm_req_skill + 1);
}

void host_hm_request_set_result(int status)
{
    g_hm_last_status = status;
}

int host_hm_last_status(void)
{
    return g_hm_last_status;
}

void host_sort_request_submit(int kind, int mode, int target, unsigned long long now_ms)
{
    g_sort_req_kind = kind;
    g_sort_req_mode = mode;
    g_sort_req_target = target;
    g_sort_req_ms = now_ms;
    g_sort_req_word = pokebw_sort_pack(kind, mode, target);
    snprintf(g_sort_status, sizeof(g_sort_status), "Sorting...");
}

unsigned host_sort_request_poll(unsigned long long now_ms)
{
    unsigned word;

    if (g_sort_req_word == 0u) {
        return 0u;
    }
    if (now_ms > g_sort_req_ms && (now_ms - g_sort_req_ms) > 5000ull) {
        g_sort_req_word = 0u;
        snprintf(g_sort_status, sizeof(g_sort_status),
                 "Sort unavailable right now (free-roam overworld only).");
        return 0u;
    }
    word = g_sort_req_word;
    g_sort_req_word = 0u;
    return word;
}

void host_sort_request_set_result(int status, int count)
{
    static const char *const k_pc_modes[] = { "Dex #", "Name (A-Z)", "Level", "Type", "Shiny First" };
    static const char *const k_bag_modes[] = { "Game Order", "Name (A-Z)", "Quantity", "Item ID" };
    static const char *const k_pockets[] = { "Items", "Medicine", "TM & HM", "Berries", "Key Items" };

    if (status < 0) {
        snprintf(g_sort_status, sizeof(g_sort_status),
                 "Sort blocked (only available while free-roaming on the overworld).");
        return;
    }
    if (status == 0) {
        if (g_sort_req_kind == POKEBW_SORT_KIND_PC_BOX || g_sort_req_kind == POKEBW_SORT_KIND_PC_ALL) {
            snprintf(g_sort_status, sizeof(g_sort_status), "Target box selection is already empty.");
        } else {
            snprintf(g_sort_status, sizeof(g_sort_status), "Target pocket selection is already empty.");
        }
        return;
    }

    if (g_sort_req_kind == POKEBW_SORT_KIND_PC_BOX) {
        int b = g_sort_req_target;
        const char *m = (g_sort_req_mode >= 0 && g_sort_req_mode < 5) ? k_pc_modes[g_sort_req_mode] : "Order";
        if (b >= 0 && b < POKEBW_GEAR_MAX_BOXES && g_gear_snap.box_names[b][0] != '\0') {
            snprintf(g_sort_status, sizeof(g_sort_status),
                     "Sorted %s (%d Pokemon) by %s.", g_gear_snap.box_names[b], count, m);
        } else {
            snprintf(g_sort_status, sizeof(g_sort_status),
                     "Sorted Box %d (%d Pokemon) by %s.", b + 1, count, m);
        }
    } else if (g_sort_req_kind == POKEBW_SORT_KIND_PC_ALL) {
        const char *m = (g_sort_req_mode >= 0 && g_sort_req_mode < 5) ? k_pc_modes[g_sort_req_mode] : "Order";
        snprintf(g_sort_status, sizeof(g_sort_status),
                 "Sorted all usable boxes (%d Pokemon) by %s.", count, m);
    } else if (g_sort_req_kind == POKEBW_SORT_KIND_BAG_POCKET) {
        int pk = clamp_int(g_sort_req_target, 0, POKEBW_GEAR_BAG_POCKETS - 1);
        const char *m = (g_sort_req_mode >= 0 && g_sort_req_mode < 4) ? k_bag_modes[g_sort_req_mode] : "Order";
        snprintf(g_sort_status, sizeof(g_sort_status),
                 "Sorted %s (%d stacks) by %s.", k_pockets[pk], count, m);
    } else {
        const char *m = (g_sort_req_mode >= 0 && g_sort_req_mode < 4) ? k_bag_modes[g_sort_req_mode] : "Order";
        snprintf(g_sort_status, sizeof(g_sort_status),
                 "Sorted all Bag pockets (%d stacks) by %s.", count, m);
    }
}

const char *host_sort_status(void)
{
    return g_sort_status;
}

const char *host_settings_effective_save_dir(void)
{
    const PokebwSettings *s = host_settings_get();

    if (s->save_dir[0] != '\0') {
        return s->save_dir;
    }
    return g_default_save_dir[0] != '\0' ? g_default_save_dir : "saves";
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

static int copy_file_atomic(const char *src_path, const char *dst_path)
{
    FILE *in;
    FILE *out;
    char tmp_path[1208];
    unsigned char buf[16384];
    size_t nread;
    int ok = 1;

    in = fopen(src_path, "rb");
    if (in == NULL) {
        return -1;
    }
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", dst_path);
    out = fopen(tmp_path, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((nread = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, nread, out) != nread) {
            ok = 0;
            break;
        }
    }
    if (ferror(in) || ferror(out)) {
        ok = 0;
    }
    if (fclose(out) != 0) {
        ok = 0;
    }
    fclose(in);
    if (!ok || rename(tmp_path, dst_path) != 0) {
        (void)remove(tmp_path);
        return -1;
    }
    return 0;
}

void host_settings_set_save_dir(const char *new_dir)
{
    PokebwSettings *s = host_settings_get();
    char clean_dir[1024];
    char old_eff[1024];
    char new_eff[1024];
    size_t len;

    snprintf(old_eff, sizeof(old_eff), "%s", host_settings_effective_save_dir());
    if (new_dir == NULL) {
        clean_dir[0] = '\0';
    } else {
        snprintf(clean_dir, sizeof(clean_dir), "%s", new_dir);
        len = strlen(clean_dir);
        while (len > 1 && (clean_dir[len - 1] == '/' || clean_dir[len - 1] == '\\')) {
            clean_dir[--len] = '\0';
        }
    }

    snprintf(s->save_dir, sizeof(s->save_dir), "%s", clean_dir);
    (void)host_settings_save();
    snprintf(new_eff, sizeof(new_eff), "%s", host_settings_effective_save_dir());
    host_crash_set_save_dir(new_eff);

    if (strcmp(old_eff, new_eff) != 0) {
        char old_sav[1200];
        char new_sav[1200];

        snprintf(old_sav, sizeof(old_sav), "%s/pokeblack.sav", old_eff);
        snprintf(new_sav, sizeof(new_sav), "%s/pokeblack.sav", new_eff);
        if (file_exists(new_sav)) {
            snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                     "Save folder updated. Restart app to load the existing .sav here.");
        } else if (file_exists(old_sav)) {
            if (copy_file_atomic(old_sav, new_sav) == 0) {
                snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                         "Save folder updated (copied pokeblack.sav).");
            } else {
                snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                         "Save folder updated (could not copy existing pokeblack.sav).");
            }
        } else {
            snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                     "Save folder updated.");
        }
        g_save_dir_dirty = 1;
    } else {
        snprintf(g_save_dir_status, sizeof(g_save_dir_status),
                 "Save folder unchanged.");
    }
}

int host_save_dir_poll_pending(char *out, unsigned n)
{
    const char *eff;
    size_t len;

    if (!g_save_dir_dirty || out == NULL || n == 0u) {
        return 0;
    }
    eff = host_settings_effective_save_dir();
    len = strlen(eff);
    if (len >= n) {
        return 0;
    }
    g_save_dir_dirty = 0;
    memcpy(out, eff, len + 1);
    return 1;
}

const char *host_save_dir_status(void)
{
    return g_save_dir_status;
}

void host_save_dir_set_status(const char *msg)
{
    snprintf(g_save_dir_status, sizeof(g_save_dir_status), "%s", msg != NULL ? msg : "");
}

void host_gear_update(const PokebwGearSnapshot *snap)
{
    if (snap == NULL) {
        return;
    }
    g_gear_snap = *snap;
}

const PokebwGearSnapshot *host_gear_get(void)
{
    return &g_gear_snap;
}

int host_gear_active_save_slot(void)
{
    char slot_path[1152];
    FILE *f;
    int slot = 1;

    if (g_gear_snap.valid != 0u &&
        g_gear_snap.active_save_slot >= 1u &&
        g_gear_snap.active_save_slot <= 8u) {
        return (int)g_gear_snap.active_save_slot;
    }
    snprintf(slot_path, sizeof(slot_path), "%s/slot.txt", host_settings_effective_save_dir());
    f = fopen(slot_path, "rb");
    if (f != NULL) {
        if (fscanf(f, "%d", &slot) != 1 || slot < 1 || slot > 8) {
            slot = 1;
        }
        fclose(f);
    }
    return slot;
}

static void make_dir_if_needed(const char *dir)
{
#if defined(_WIN32)
    (void)_mkdir(dir);
#else
    (void)mkdir(dir, 0755);
#endif
}

static int active_save_file_path(char *out, size_t out_sz)
{
    const char *eff = host_settings_effective_save_dir();
    int slot = host_gear_active_save_slot();
    int n;

    if (slot <= 1) {
        n = snprintf(out, out_sz, "%s/pokeblack.sav", eff);
    } else {
        char slots_dir[1152];
        snprintf(slots_dir, sizeof(slots_dir), "%s/slots", eff);
        make_dir_if_needed(slots_dir);
        n = snprintf(out, out_sz, "%s/slots/save%d.sav", eff, slot);
    }
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

static int backup_slot_path(int slot, char *out, size_t out_sz, int create_dir)
{
    const char *eff = host_settings_effective_save_dir();
    char backup_dir[1152];
    int n;

    if (slot < 1 || slot > 3) {
        return -1;
    }
    if (create_dir) {
        snprintf(backup_dir, sizeof(backup_dir), "%s/backups", eff);
        make_dir_if_needed(backup_dir);
    }
    n = snprintf(out, out_sz, "%s/backups/backup_%d.sav", eff, slot);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

int host_backup_slot_info(int slot, PokebwBackupSlotInfo *out)
{
    char path[1152];
    struct stat st;
    struct tm tm_buf;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (backup_slot_path(slot, path, sizeof(path), 0) != 0) {
        return -1;
    }
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        return 0;
    }
    out->exists = 1;
    out->size_bytes = (unsigned long long)st.st_size;
#if defined(_WIN32)
    if (localtime_s(&tm_buf, &st.st_mtime) == 0) {
        strftime(out->timestamp, sizeof(out->timestamp), "%Y-%m-%d %H:%M", &tm_buf);
    } else {
        snprintf(out->timestamp, sizeof(out->timestamp), "Saved");
    }
#else
    if (localtime_r(&st.st_mtime, &tm_buf) != NULL) {
        strftime(out->timestamp, sizeof(out->timestamp), "%Y-%m-%d %H:%M", &tm_buf);
    } else {
        snprintf(out->timestamp, sizeof(out->timestamp), "Saved");
    }
#endif
    return 0;
}

int host_backup_slot_save(int slot)
{
    char src[1152];
    char dst[1152];

    if (active_save_file_path(src, sizeof(src)) != 0 ||
        backup_slot_path(slot, dst, sizeof(dst), 1) != 0) {
        snprintf(g_backup_status, sizeof(g_backup_status), "Invalid backup slot %d.", slot);
        return -1;
    }
    if (!file_exists(src)) {
        snprintf(g_backup_status, sizeof(g_backup_status),
                 "No active save file found to back up yet (save in-game first).");
        return -1;
    }
    if (copy_file_atomic(src, dst) != 0) {
        snprintf(g_backup_status, sizeof(g_backup_status),
                 "Failed to write Backup Slot %d.", slot);
        return -1;
    }
    snprintf(g_backup_status, sizeof(g_backup_status),
             "Backed up Slot %d/8 to Backup %d.", host_gear_active_save_slot(), slot);
    return 0;
}

int host_backup_slot_restore(int slot)
{
    char src[1152];
    char dst[1152];

    if (backup_slot_path(slot, src, sizeof(src), 0) != 0 ||
        active_save_file_path(dst, sizeof(dst)) != 0) {
        snprintf(g_backup_status, sizeof(g_backup_status), "Invalid backup slot %d.", slot);
        return -1;
    }
    if (!file_exists(src)) {
        snprintf(g_backup_status, sizeof(g_backup_status), "Backup %d is empty.", slot);
        return -1;
    }
    if (copy_file_atomic(src, dst) != 0) {
        snprintf(g_backup_status, sizeof(g_backup_status),
                 "Failed to restore Backup %d.", slot);
        return -1;
    }
    snprintf(g_backup_status, sizeof(g_backup_status),
             "Restored Backup %d into Save Slot %d/8. Restart or reload save from Title.",
             slot, host_gear_active_save_slot());
    return 0;
}

const char *host_backup_status(void)
{
    return g_backup_status;
}

void host_dex_request_submit(int want_index, uint16_t species, uint8_t form, unsigned long long now_ms)
{
    g_dex_req_ms = now_ms;
    g_dex_req_word = pokebw_dex_pack(want_index, species, form);
    snprintf(g_dex_status, sizeof(g_dex_status), "Loading Pokedex data...");
}

unsigned host_dex_request_poll(unsigned long long now_ms)
{
    unsigned word;

    if (g_dex_req_word == 0u) {
        return 0u;
    }
    if (now_ms > g_dex_req_ms && (now_ms - g_dex_req_ms) > 5000ull) {
        g_dex_req_word = 0u;
        snprintf(g_dex_status, sizeof(g_dex_status),
                 "Pokedex lookup requires free-roam overworld.");
        return 0u;
    }
    word = g_dex_req_word;
    g_dex_req_word = 0u;
    return word;
}

void host_dex_index_update(const PokebwDexIndex *idx)
{
    if (idx == NULL) {
        return;
    }
    g_dex_index = *idx;
    if (g_dex_index.valid != 0u) {
        g_dex_status[0] = '\0';
    }
}

const PokebwDexIndex *host_dex_index_get(void)
{
    return &g_dex_index;
}

void host_dex_detail_update(const PokebwDexDetail *detail)
{
    if (detail == NULL) {
        return;
    }
    g_dex_detail = *detail;
    if (g_dex_detail.valid != 0u) {
        g_dex_status[0] = '\0';
    }
}

const PokebwDexDetail *host_dex_detail_get(void)
{
    return &g_dex_detail;
}

const char *host_dex_status(void)
{
    return g_dex_status;
}
