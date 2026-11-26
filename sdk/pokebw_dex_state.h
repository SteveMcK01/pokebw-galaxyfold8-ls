/*
 * sdk/pokebw_dex_state.h -- Fixed-layout, pointer-free ABI shared between
 * the wasm32 Pokédex lookup collector (sdk/pokebw_dex_lookup.c) and the
 * 64-bit native host overlay (host/pokebw_settings.c + host/imgui_overlay.cpp).
 */

#ifndef POKEBW_DEX_STATE_H
#define POKEBW_DEX_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POKEBW_DEX_MAX_SPECIES     649
#define POKEBW_DEX_NAME_MAX        16
#define POKEBW_DEX_MAX_PRE_EVO     2
#define POKEBW_DEX_MAX_EVOS        7
#define POKEBW_DEX_MAX_LVL_MOVES   48
#define POKEBW_DEX_MAX_TM_MOVES    104
#define POKEBW_DEX_MAX_EGG_MOVES   36
#define POKEBW_DEX_MAX_TUTOR_MOVES 8
#define POKEBW_DEX_MAX_LOCATIONS   48
#define POKEBW_DEX_HABITATS        7

/* Request packing between host and wasm (bw_host_dex_request_poll). */
static inline uint32_t pokebw_dex_pack(int want_index, uint16_t species, uint8_t form)
{
    return (want_index ? (1u << 24) : 0u) |
           (((uint32_t)form & 0xFFu) << 16) |
           ((uint32_t)species & 0xFFFFu);
}

typedef struct {
    uint32_t valid;
    uint16_t count;
    uint8_t  pad[2];
    uint8_t  type1[POKEBW_DEX_MAX_SPECIES + 1];
    uint8_t  type2[POKEBW_DEX_MAX_SPECIES + 1];
    uint8_t  dex_state[POKEBW_DEX_MAX_SPECIES + 1]; /* 0=New, 1=Seen, 2=Caught */
    char     names[POKEBW_DEX_MAX_SPECIES + 1][POKEBW_DEX_NAME_MAX];
} PokebwDexIndex;

typedef struct {
    uint16_t target_species;
    uint16_t method;
    uint16_t param;
    uint8_t  pad[2];
    char     target_name[24];
    char     param_name[32];
} PokebwDexEvo;

typedef struct {
    uint16_t move_id;
    uint8_t  type;
    uint8_t  category; /* 0=Status, 1=Physical, 2=Special */
    uint8_t  power;
    uint8_t  accuracy;
    uint8_t  pp;
    uint8_t  tag;      /* Level-Up: level; TM/HM: 1..95 TM, 101..106 HM */
    char     name[24];
} PokebwDexMove;

typedef struct {
    uint16_t zone_id;
    uint16_t place_id;
    uint8_t  habitat;      /* 0..6 matching PokebwGearSnapshot habitats */
    uint8_t  seasons_mask; /* bit 0=Spring, 1=Summer, 2=Autumn, 3=Winter */
    uint8_t  min_lv;
    uint8_t  max_lv;
    uint8_t  chance_pct;
    uint8_t  pad[3];
    char     place_name[32];
} PokebwDexLocation;

typedef struct {
    uint32_t          valid;
    uint16_t          species;
    uint8_t           form;
    uint8_t           form_count;
    uint8_t           type1;
    uint8_t           type2;
    uint8_t           dex_state;
    uint8_t           catch_rate;
    uint8_t           gender_ratio;
    uint8_t           egg_cycles;
    uint8_t           egg_group1;
    uint8_t           egg_group2;
    uint8_t           base_stats[6]; /* 0=HP, 1=Atk, 2=Def, 3=SpA, 4=SpD, 5=Spe */
    uint16_t          bst;
    uint16_t          ability_ids[3]; /* 0=Primary, 1=Secondary, 2=Hidden */
    char              name[24];
    char              category[32];
    char              abilities[3][24];
    uint8_t           pre_evo_count;
    uint8_t           evo_count;
    uint8_t           trade_evo_fix_active;
    uint8_t           loc_catch_em_all_active;
    PokebwDexEvo      pre_evos[POKEBW_DEX_MAX_PRE_EVO];
    PokebwDexEvo      evos[POKEBW_DEX_MAX_EVOS];
    uint16_t          lvl_count;
    uint16_t          tm_count;
    uint16_t          egg_count;
    uint16_t          tutor_count;
    PokebwDexMove     lvl_moves[POKEBW_DEX_MAX_LVL_MOVES];
    PokebwDexMove     tm_moves[POKEBW_DEX_MAX_TM_MOVES];
    PokebwDexMove     egg_moves[POKEBW_DEX_MAX_EGG_MOVES];
    PokebwDexMove     tutor_moves[POKEBW_DEX_MAX_TUTOR_MOVES];
    uint8_t           loc_count;
    uint8_t           loc_truncated;
    uint8_t           pad_loc[2];
    PokebwDexLocation locations[POKEBW_DEX_MAX_LOCATIONS];
} PokebwDexDetail;

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_DEX_STATE_H */
