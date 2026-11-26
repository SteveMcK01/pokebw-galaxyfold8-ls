/*
 * sdk/pokebw_area_map.h -- Phase 4.9: Gear Overhead Area Item Map
 * collector hook inside the wasm32 game module.
 */

#ifndef POKEBW_AREA_MAP_H
#define POKEBW_AREA_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

void pokebw_area_map_on_field_tick(void *gameSystem, void *fieldMap);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_AREA_MAP_H */
