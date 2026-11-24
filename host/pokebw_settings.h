/*
 * host/pokebw_settings.h -- Persistent INI settings for the PokeBW Fold
 * Android overlay and gameplay hooks (saved to <profile_dir>/pokebw_settings.ini).
 */

#ifndef EIDOS_HOST_POKEBW_SETTINGS_H
#define EIDOS_HOST_POKEBW_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* TouchControls */
    int touch_visible;
    int touch_opacity; /* 15..100 */
    int touch_scale;   /* 70..140 */

    /* Display */
    int show_fps;
    int fold_pip_mode;

    /* Gameplay */
    int trade_evo_fix;
    int modern_exp_share;
    int hm_freedom;
    int hm_no_badges;
    int catch_em_all;

    /* QoL */
    int universal_tm;
    int hard_level_cap;
    int no_random_encounters;
    int always_bite_fishing;
    int instant_text;
} PokebwSettings;

void host_settings_init(const char *profile_dir);
PokebwSettings *host_settings_get(void);
int  host_settings_get_by_id(int id);
void host_settings_reset_controls(void);
void host_settings_reset_defaults(void);
int  host_settings_save(void);
const char *host_settings_path(void);

/* Two-way mirror of eidos built-in EIDOS_OPT_* settings. */
void host_eidos_opt_from_wasm(int id, int value);
int  host_eidos_opt_get(int id);
void host_eidos_opt_set(int id, int value);
unsigned host_eidos_opt_poll_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_HOST_POKEBW_SETTINGS_H */
