/*
 * sdk/pokebw_ow_sprites.h -- Phase 3.1: 649-Species Overworld Sprite Archive.
 *
 * Registers normal, shiny, and alternate-form Pokémon billboard BTX0 sprites
 * into a/0/4/9 via sdk/vfs.h (bw_vfs_mem_set) and serves MmdlObjCodeParam rows
 * for private Pokémon objCodes (0x4000..0x5FFF) by wrapping
 * ov010_FLDMMDL_LoadObjCodeParam.
 */

#ifndef POKEBW_OW_SPRITES_H
#define POKEBW_OW_SPRITES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POKEBW_OW_OBJCODE_BASE       0x4000u
#define POKEBW_OW_OBJCODE_END        0x6000u
#define POKEBW_OW_WATER_SLOTS        12u
#define POKEBW_OW_WATER_OBJCODE_BASE 0x5FE8u
#define POKEBW_OW_WATER_OBJCODE_END  (POKEBW_OW_WATER_OBJCODE_BASE + POKEBW_OW_WATER_SLOTS)
#define POKEBW_OW_SPECIES_MAX        649u

int pokebw_ow_sprites_register(void);
uint16_t pokebw_ow_sprite_objcode(uint16_t species, uint8_t form, int shiny);
uint16_t pokebw_ow_sprite_water_objcode(uint32_t pool_slot, uint16_t species, uint8_t form, int shiny);
uint8_t pokebw_ow_sprite_mdl_size(uint16_t objCode);
int pokebw_ow_sprites_is_code(uint32_t objCode);
void pokebw_ow_sprites_stats(uint32_t *out_total, uint32_t *out_custom, uint32_t *out_forms);

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_OW_SPRITES_H */
