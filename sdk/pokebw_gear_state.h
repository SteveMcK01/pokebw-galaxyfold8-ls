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
#define POKEBW_GEAR_FIELD_MOVES 8
#define POKEBW_GEAR_MAX_BOXES   24
#define POKEBW_GEAR_BAG_POCKETS 5

/* Phase 4.6: Auto-Sorter request encoding (host <-> wasm). */
enum {
    POKEBW_SORT_KIND_PC_BOX     = 1, /* target = 0..23 or 0xFF (current box) */
    POKEBW_SORT_KIND_PC_ALL     = 2, /* sort across all usable boxes */
    POKEBW_SORT_KIND_BAG_POCKET = 3, /* target = 0..4 */
    POKEBW_SORT_KIND_BAG_ALL    = 4  /* sort all 5 pockets */
};

enum {
    POKEBW_SORT_PC_DEX   = 0,
    POKEBW_SORT_PC_NAME  = 1,
    POKEBW_SORT_PC_LEVEL = 2,
    POKEBW_SORT_PC_TYPE  = 3,
    POKEBW_SORT_PC_SHINY = 4
};

enum {
    POKEBW_SORT_BAG_GAME = 0,
    POKEBW_SORT_BAG_NAME = 1,
    POKEBW_SORT_BAG_QTY  = 2,
    POKEBW_SORT_BAG_ID   = 3
};

static inline uint32_t pokebw_sort_pack(int kind, int mode, int target)
{
    return ((uint32_t)(kind & 0xFu)) |
           (((uint32_t)(mode & 0xFu)) << 4) |
           (((uint32_t)(target & 0xFFu)) << 8);
}

typedef struct {
    uint8_t skill_index;  /* 0=Cut, 4=Fly, 1=Surf, 3=Strength, 2=Waterfall, 10=Dive, 5=Flash, 7=Dig */
    uint8_t party_slot;   /* 0..5 eligible party member, or 0xFF if none */
    uint8_t usable;       /* 1 if usable right now on current tile/zone, else 0 */
    uint8_t blocked_code; /* 0=ok, 1..4=game block reason, 5=no party user */
} PokebwGearFieldMove;

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
    PokebwGearMon       party[POKEBW_GEAR_MAX_PARTY];
    PokebwGearHabitat   habitats[POKEBW_GEAR_HABITATS];
    PokebwGearFieldMove field_moves[POKEBW_GEAR_FIELD_MOVES];
    uint8_t             box_usable_count;
    uint8_t             box_current;
    uint8_t             pad_box[2];
    uint8_t             box_counts[POKEBW_GEAR_MAX_BOXES];
    uint16_t            bag_pocket_counts[POKEBW_GEAR_BAG_POCKETS];
    uint16_t            pad_bag;
    char                box_names[POKEBW_GEAR_MAX_BOXES][20];
} PokebwGearSnapshot;

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_GEAR_STATE_H */
