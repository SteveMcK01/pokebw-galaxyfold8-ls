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

    /* Display & Foldable */
    POKEBW_SET_FOLD_PIP_MODE     = 20,

    /* Phase 2 QoL */
    POKEBW_SET_UNIVERSAL_TM          = 30,
    POKEBW_SET_HARD_LEVEL_CAP        = 31,
    POKEBW_SET_NO_RANDOM_ENCOUNTERS  = 32,
    POKEBW_SET_ALWAYS_BITE_FISHING   = 33,
    POKEBW_SET_INSTANT_TEXT          = 34,
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
    return id == POKEBW_SET_TRADE_EVO_FIX ? 1 : 0;
}
#endif

#endif /* POKEBW_SETTINGS_IDS_H */
