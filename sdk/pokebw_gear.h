/*
 * sdk/pokebw_gear.h -- Phase 2.3: Live Gear Companion & Unova Route Radar
 * collector hooks inside the wasm32 game module.
 */

#ifndef POKEBW_GEAR_H
#define POKEBW_GEAR_H

#include <stdint.h>

void pokebw_gear_on_encount_load(const void *encountData, uint32_t zoneId, int seasonIndex);
void pokebw_gear_on_field_tick(void *gameSystem, void *fieldMap);
void pokebw_gear_mark_dirty(void);
int  pokebw_gear_msg_line(int member, uint32_t idx, char *out, int cap);

#endif /* POKEBW_GEAR_H */
