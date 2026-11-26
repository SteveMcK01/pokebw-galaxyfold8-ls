/*
 * sdk/pokebw_dex_lookup.h -- Phase 4.8: Gear Built-in Pokédex Lookup
 * collector hook inside the wasm32 game module.
 */

#ifndef POKEBW_DEX_LOOKUP_H
#define POKEBW_DEX_LOOKUP_H

#ifdef __cplusplus
extern "C" {
#endif

void pokebw_dex_lookup_on_field_tick(void *gameSystem, void *fieldMap);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_DEX_LOOKUP_H */
