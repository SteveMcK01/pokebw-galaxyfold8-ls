/*
 * host/pokebw_settings.h -- Persistent INI settings for the PokeBW Fold
 * Android overlay and gameplay hooks (saved to <profile_dir>/pokebw_settings.ini).
 */

#ifndef EIDOS_HOST_POKEBW_SETTINGS_H
#define EIDOS_HOST_POKEBW_SETTINGS_H

#include "pokebw_gear_state.h"

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
    int portable_pc;

    /* QoL */
    int hard_level_cap;
    int no_random_encounters;
    int always_bite_fishing;
    int instant_text;
    int season_override; /* 0 Auto, 1 Spring, 2 Summer, 3 Autumn, 4 Winter */
    int clock_override;  /* 0 Auto, 1 Morning, 2 Day, 3 Evening, 4 Night */
    int type_effectiveness;

    /* Save */
    char save_dir[1024];
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

/* One-shot Portable PC Box request (host -> wasm). */
void host_pc_request_open(int mode, unsigned long long now_ms);
unsigned host_pc_request_poll(unsigned long long now_ms);
void host_pc_request_set_result(int status);
int  host_pc_last_status(void);

/* Custom external save folder support (PKForge / emulator .sav interop). */
const char *host_settings_effective_save_dir(void);
void host_settings_set_save_dir(const char *new_dir);
int  host_save_dir_poll_pending(char *out, unsigned n);
const char *host_save_dir_status(void);
void host_save_dir_set_status(const char *msg);
void host_pick_save_dir_begin(void);
void host_pick_save_dir_pump(void);

/* Phase 2.3: Live Gear telemetry cache & Save Backup Slots. */
typedef struct {
    int exists;
    unsigned long long size_bytes;
    char timestamp[32];
} PokebwBackupSlotInfo;

void host_gear_update(const PokebwGearSnapshot *snap);
const PokebwGearSnapshot *host_gear_get(void);
int  host_gear_active_save_slot(void);
int  host_backup_slot_info(int slot, PokebwBackupSlotInfo *out);
int  host_backup_slot_save(int slot);
int  host_backup_slot_restore(int slot);
const char *host_backup_status(void);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_HOST_POKEBW_SETTINGS_H */
