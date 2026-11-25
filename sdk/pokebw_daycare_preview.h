/*
 * sdk/pokebw_daycare_preview.h -- Phase 4.7: Gear Egg Step Counter & Route 3
 * Day Care Preview collector hooks inside the wasm32 game module.
 */

#ifndef POKEBW_DAYCARE_PREVIEW_H
#define POKEBW_DAYCARE_PREVIEW_H

#include <stdint.h>

#include "pokebw_gear_state.h"

uint32_t pokebw_daycare_egg_step_signature(void *gd, void *party);
void     pokebw_daycare_fill_party_eggs(void *gd, void *party, PokebwGearSnapshot *snap);
void     pokebw_daycare_fill_preview(void *gd, void *fieldMap, PokebwGearDaycare *out);

#endif /* POKEBW_DAYCARE_PREVIEW_H */
