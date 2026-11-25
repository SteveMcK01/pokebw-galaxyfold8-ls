/*
 * sdk/pokebw_settings_ids.h -- Stable setting IDs shared between the native
 * host (host/pokebw_settings.c) and the wasm32 game module.
 */

#ifndef POKEBW_SETTINGS_IDS_H
#define POKEBW_SETTINGS_IDS_H

enum {
    /* Touch Controls & Display */
    POKEBW_SET_TOUCH_VISIBLE = 1,
    POKEBW_SET_TOUCH_OPACITY = 2,
    POKEBW_SET_TOUCH_SCALE   = 3,
    POKEBW_SET_SHOW_FPS      = 4,

    /* Phase 1 Gameplay */
    POKEBW_SET_TRADE_EVO_FIX     = 10,
    POKEBW_SET_MODERN_EXP_SHARE  = 11,
    POKEBW_SET_HM_FREEDOM        = 12,
    POKEBW_SET_HM_NO_BADGES      = 13,
    POKEBW_SET_CATCH_EM_ALL      = 14,
    POKEBW_SET_PORTABLE_PC       = 15,

    /* Display & Foldable */
    POKEBW_SET_FOLD_PIP_MODE     = 20,

    /* Phase 2 QoL */
    POKEBW_SET_HARD_LEVEL_CAP        = 31,
    POKEBW_SET_NO_RANDOM_ENCOUNTERS  = 32,
    POKEBW_SET_ALWAYS_BITE_FISHING   = 33,
    POKEBW_SET_INSTANT_TEXT          = 34,
    POKEBW_SET_SEASON_OVERRIDE       = 35, /* 0 Auto, 1 Spring, 2 Summer, 3 Autumn, 4 Winter */
    POKEBW_SET_CLOCK_OVERRIDE        = 36, /* 0 Auto, 1 Morning, 2 Day, 3 Evening, 4 Night */
    POKEBW_SET_TYPE_EFFECTIVENESS    = 37,

    /* Phase 3 Overworld Followers & Wild Spawns */
    POKEBW_SET_FOLLOWING_POKEMON     = 40,
    POKEBW_SET_OVERWORLD_WILDS       = 41,
    POKEBW_SET_OVERWORLD_WILD_DENSITY = 42,

    /* Phase 4 Extended QoL & Convenience */
    POKEBW_SET_FAST_FORWARD       = 50, /* 0 Off, 1 On (runtime toggle) */
    POKEBW_SET_FAST_FORWARD_SPEED = 51, /* 0=2x, 1=3x, 2=4x, 3=Max */
    POKEBW_SET_SHINY_ODDS         = 52, /* 0=1/8192, 1=1/1024, 2=1/256, 3=1/64, 4=100% */
};

/* Mirror of engine/platform/settings.h EIDOS_OPT_* IDs exposed to ImGui. */
enum {
    POKEBW_EIDOS_OPT_RESOLUTION       = 2,
    POKEBW_EIDOS_OPT_LAYOUT           = 6,
    POKEBW_EIDOS_OPT_SWAP             = 7,
    POKEBW_EIDOS_OPT_GAP              = 8,
    POKEBW_EIDOS_OPT_VOLUME           = 9,
    POKEBW_EIDOS_OPT_INSTANT_TEXT     = 10,
    POKEBW_EIDOS_OPT_BATTLE_BACKDROP  = 11,
    POKEBW_EIDOS_OPT_BATTLE_PLATFORMS = 12,
    POKEBW_EIDOS_OPT_MOVE_COLOURS     = 13,
    POKEBW_EIDOS_OPT_OUTLINES         = 15,
    POKEBW_EIDOS_OPT_TRANSITIONS      = 16,
    POKEBW_EIDOS_OPT_VSYNC            = 19,
    POKEBW_EIDOS_OPT_UI_THEME         = 20,
    POKEBW_EIDOS_OPT_WEATHER_HAZE     = 21,
    POKEBW_EIDOS_OPT_COUNT            = 22
};

#if defined(__wasm__)
#include "wasm_host.h"
static inline int bw_pokebw_setting(int id)
{
    return bw_host_setting_get(id);
}
#else
static inline int bw_pokebw_setting(int id)
{
    if (id == POKEBW_SET_OVERWORLD_WILD_DENSITY) {
        return 4;
    }
    return (id == POKEBW_SET_TRADE_EVO_FIX ||
            id == POKEBW_SET_PORTABLE_PC ||
            id == POKEBW_SET_TYPE_EFFECTIVENESS ||
            id == POKEBW_SET_FOLLOWING_POKEMON ||
            id == POKEBW_SET_OVERWORLD_WILDS) ? 1 : 0;
}
#endif

#endif /* POKEBW_SETTINGS_IDS_H */
