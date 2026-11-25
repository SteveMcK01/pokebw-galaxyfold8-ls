/*
 * sdk/pokebw_hm_shortcut.h -- Phase 4.4: In-Game HM/TM Quick-Use Shortcut.
 */

#ifndef POKEBW_HM_SHORTCUT_H
#define POKEBW_HM_SHORTCUT_H

#include "pokebw_gear_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compute live availability of all 8 field moves (Cut, Fly, Surf, Strength,
 * Waterfall, Dive, Flash, Dig) on the current overworld tile/zone. */
void pokebw_hm_shortcut_fill(
    void *gameSystem, void *fieldMap,
    PokebwGearFieldMove out[POKEBW_GEAR_FIELD_MOVES]);

/* Poll host quick-use requests and launch the matching Gen 5 field-skill event
 * (or TownMap Fly picker for Fly) when overworld guardrails pass. */
void *pokebw_hm_shortcut_try_run(void *gameSystem, void *fieldMap, int is_rail);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_HM_SHORTCUT_H */
