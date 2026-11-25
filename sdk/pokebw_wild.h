/*
 * sdk/pokebw_wild.h -- Phase 3.3: Native C Overworld Wild Pokémon Spawner.
 */

#ifndef POKEBW_WILD_H
#define POKEBW_WILD_H

#ifdef __cplusplus
extern "C" {
#endif

void  pokebw_wild_tick(void *mmdlSys);
void  pokebw_wild_on_delete_sys(void *mmdlSys);
void *pokebw_wild_check_encounter(void *gameSystem, void *fieldMap);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_WILD_H */
