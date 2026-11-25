/*
 * sdk/pokebw_gear_state.h -- Fixed-layout, pointer-free ABI shared between
 * the wasm32 game module (sdk/pokebw_gear.c) and the 64-bit native host
 * overlay (host/pokebw_settings.c + host/imgui_overlay.cpp).
 */

#ifndef POKEBW_GEAR_STATE_H
#define POKEBW_GEAR_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POKEBW_GEAR_MAX_PARTY 6
#define POKEBW_GEAR_MAX_MOVES 4
#define POKEBW_GEAR_HABITATS  7
#define POKEBW_GEAR_MAX_ENC   12

typedef struct {
    uint16_t id;
    uint8_t  type;
    uint8_t  category;
    uint8_t  pp;
    uint8_t  max_pp;
    uint8_t  pad[2];
    char     name[24];
} PokebwGearMove;

typedef struct {
    uint16_t       species;
    uint16_t       hp;
    uint16_t       max_hp;
    uint8_t        level;
    uint8_t        is_egg;
    uint8_t        status;
    uint8_t        move_count;
    uint8_t        pad[2];
    char           name[24];
    char           species_name[24];
    PokebwGearMove moves[POKEBW_GEAR_MAX_MOVES];
} PokebwGearMon;

typedef struct {
    uint16_t species;
    uint8_t  min_lv;
    uint8_t  max_lv;
    uint8_t  chance_pct;
    uint8_t  dex_state; /* 0 = New (unseen), 1 = Seen, 2 = Caught */
    char     name[24];
} PokebwGearEncEntry;

typedef struct {
    uint8_t            rate;
    uint8_t            count;
    uint8_t            pad[2];
    PokebwGearEncEntry entries[POKEBW_GEAR_MAX_ENC];
} PokebwGearHabitat;

typedef struct {
    uint32_t          valid;
    uint32_t          zone_id;
    uint16_t          ow_sprites_total;
    uint16_t          ow_sprites_custom;
    uint8_t           season;
    uint8_t           party_count;
    uint8_t           has_encounters;
    uint8_t           catch_em_all_active;
    uint8_t           active_save_slot;
    uint8_t           pad[3];
    char              zone_name[48];
    PokebwGearMon     party[POKEBW_GEAR_MAX_PARTY];
    PokebwGearHabitat habitats[POKEBW_GEAR_HABITATS];
} PokebwGearSnapshot;

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_GEAR_STATE_H */
