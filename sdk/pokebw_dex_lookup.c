/*
 * sdk/pokebw_dex_lookup.c -- Phase 4.8: Gear Built-in Pokédex Lookup.
 *
 * Reads base stats, types, abilities, breeding/catch metadata, evolution
 * chains (honoring Phase 1.1 Trade Evolution Fixes), full Level-Up / TM-HM /
 * Egg / Tutor learnsets, and seasonal Unova habitat encounter locations
 * (honoring Phase 1.4 Gotta Catch 'Em All!) directly from the mounted Black/
 * White ROM archives and pushes them to the host overlay on demand.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pokebw_dex_lookup.h"
#include "pokebw_dex_state.h"
#include "pokebw_gear.h"
#include "pokebw_settings_ids.h"
#include "port.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_DEX_WEAK __attribute__((weak))
#else
#define POKEBW_DEX_WEAK
#endif

#define MSG_ITEM_BANK     54
#define MSG_SPECIES_BANK  70
#define MSG_PLACE_BANK    89
#define MSG_ABILITY_BANK  182
#define MSG_MOVE_BANK     203
#define MSG_CATEGORY_BANK 260

#define BW_ZONEDATA_ARC   0x0Cu
#define BW_EGG_MOVE_ARC   0x7Bu
#define BW_ENCOUNT_ARC    0x7Eu
#define BW_TEMP_HEAP      0x8015u

#if defined(__wasm__)
struct PokebwDexEvoRow {
    uint16_t method;
    uint16_t param;
    uint16_t species;
};

struct PokebwDexEvoData {
    struct PokebwDexEvoRow rows[7];
    uint16_t pad;
};

typedef char pokebw_dex_evo_size_check[
    sizeof(struct PokebwDexEvoData) == 0x2Cu ? 1 : -1];

struct PokebwDexEncSlot {
    uint16_t species : 11;
    uint16_t form : 5;
    uint8_t  minLevel;
    uint8_t  maxLevel;
};

struct PokebwDexEncData {
    uint8_t                 rate[7];
    uint8_t                 flags;
    struct PokebwDexEncSlot ground[3][12];
    struct PokebwDexEncSlot water[2][5];
    struct PokebwDexEncSlot fishing[2][5];
};

typedef char pokebw_dex_enc_size_check[
    sizeof(struct PokebwDexEncData) == 0xE8u ? 1 : -1];

extern void *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_DEX_WEAK;
extern void *ZUKAN_SAVE_GetFromGameData(void *gd) POKEBW_DEX_WEAK;
extern uint32_t ZUKAN_SAVE_IsCaught(void *zukan, uint16_t species) POKEBW_DEX_WEAK;
extern uint32_t ZUKAN_SAVE_IsSeen(void *zukan, uint16_t species) POKEBW_DEX_WEAK;

extern uint32_t POKE_TOOL_GetPersonalParam(uint16_t species, uint16_t form, uint32_t field) POKEBW_DEX_WEAK;
extern void POKE_PERSONAL_LoadDataDirect(uint32_t species, int form, void *dst) POKEBW_DEX_WEAK;
extern void SHINKA_CHECK_LoadData(uint32_t species, void *rows) POKEBW_DEX_WEAK;
extern uint32_t POKE_TOOL_CheckSpeciesTmCompat(uint16_t species, uint16_t form, int32_t tmIndex) POKEBW_DEX_WEAK;
extern uint32_t POKE_TOOL_CheckSpeciesTutorCompat(uint16_t species, uint16_t form, int32_t tutorIndex) POKEBW_DEX_WEAK;

extern uint16_t FILE_UTIL_GetMachineMove(uint16_t machineId) POKEBW_DEX_WEAK;
extern uint32_t FILE_UTIL_GetId(uint32_t machineId) POKEBW_DEX_WEAK;
extern uint32_t WAZA_TOOL_GetParam(uint32_t moveId, uint32_t paramId) POKEBW_DEX_WEAK;

extern void *ARC_TOOL_CreateHandle(uint32_t archiveID, uint32_t heapID) POKEBW_DEX_WEAK;
extern void ARC_TOOL_DeleteHandle(void *handle) POKEBW_DEX_WEAK;
extern uint32_t ARC_TOOL_GetArchiveFileCount(uint32_t archiveID) POKEBW_DEX_WEAK;
extern uint32_t ARC_TOOL_GetMemberSize(uint32_t archiveID, uint32_t memberIdx) POKEBW_DEX_WEAK;
extern uint32_t ARC_TOOL_GetHandleMemberSize(void *arc, uint32_t memberIdx) POKEBW_DEX_WEAK;
extern void ARC_TOOL_ReadHandleMemberRange(void *arc, uint32_t memberIdx, uint32_t offset, uint32_t size, void *dst) POKEBW_DEX_WEAK;
extern void *ARC_TOOL_LoadMember(uint32_t archiveID, uint32_t memberIdx, uint16_t heapID) POKEBW_DEX_WEAK;
extern void GFL_HEAP_Free(void *ptr) POKEBW_DEX_WEAK;

extern uint32_t ZONEDATA_GetEncountId(uint32_t id) POKEBW_DEX_WEAK;
extern uint32_t ZONEDATA_GetPlaceNameId(uint32_t id) POKEBW_DEX_WEAK;
extern void pokebw_catch_em_all_apply_if_enabled(void *encountData, uint32_t zoneId) POKEBW_DEX_WEAK;

static PokebwDexIndex g_dex_index;
static PokebwDexDetail g_dex_detail;

static const uint16_t k_tutor_moves[7] = {
    520u, /* 0: Grass Pledge (0x208) */
    519u, /* 1: Fire Pledge  (0x207) */
    518u, /* 2: Water Pledge (0x206) */
    338u, /* 3: Frenzy Plant (0x152) */
    307u, /* 4: Blast Burn   (0x133) */
    308u, /* 5: Hydro Cannon (0x134) */
    434u  /* 6: Draco Meteor (0x1B2) */
};

static const uint8_t k_ground_pct[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
static const uint8_t k_water_pct[5]   = { 60, 30, 5, 4, 1 };
static const uint8_t k_fish_pct[5]    = { 40, 30, 15, 10, 5 };

static void load_species_name(uint16_t sp, char *out, int cap)
{
    if (!pokebw_gear_msg_line(MSG_SPECIES_BANK, (uint32_t)sp, out, cap)) {
        snprintf(out, (size_t)cap, "#%u", (unsigned)sp);
    }
}

static uint8_t read_dex_state(void *zukan, uint16_t sp)
{
    if (zukan == NULL || sp == 0u || sp > POKEBW_DEX_MAX_SPECIES) {
        return 0u;
    }
    if (ZUKAN_SAVE_IsCaught != NULL && ZUKAN_SAVE_IsCaught(zukan, sp) != 0u) {
        return 2u;
    }
    if (ZUKAN_SAVE_IsSeen != NULL && ZUKAN_SAVE_IsSeen(zukan, sp) != 0u) {
        return 1u;
    }
    return 0u;
}

static void fill_move_entry(PokebwDexMove *dst, uint16_t move_id, uint8_t tag)
{
    memset(dst, 0, sizeof(*dst));
    dst->move_id = move_id;
    dst->tag = tag;
    if (WAZA_TOOL_GetParam != NULL) {
        uint32_t tp = WAZA_TOOL_GetParam(move_id, 0u);
        uint32_t cat = WAZA_TOOL_GetParam(move_id, 1u);
        uint32_t pwr = WAZA_TOOL_GetParam(move_id, 3u);
        uint32_t acc = WAZA_TOOL_GetParam(move_id, 4u);
        uint32_t pp = WAZA_TOOL_GetParam(move_id, 5u);
        dst->type = tp < 18u ? (uint8_t)tp : 17u;
        dst->category = cat < 3u ? (uint8_t)cat : 0u;
        dst->power = pwr > 255u ? 255u : (uint8_t)pwr;
        dst->accuracy = acc > 255u ? 255u : (uint8_t)acc;
        dst->pp = pp > 255u ? 255u : (uint8_t)pp;
    }
    if (!pokebw_gear_msg_line(MSG_MOVE_BANK, (uint32_t)move_id, dst->name, (int)sizeof(dst->name))) {
        snprintf(dst->name, sizeof(dst->name), "Move %u", (unsigned)move_id);
    }
}

static void fill_evo_param_name(PokebwDexEvo *e)
{
    e->param_name[0] = '\0';
    switch (e->method) {
    case 6u:
    case 8u:
    case 17u:
    case 18u:
    case 19u:
    case 20u:
        if (e->param != 0u) {
            pokebw_gear_msg_line(MSG_ITEM_BANK, (uint32_t)e->param,
                                 e->param_name, (int)sizeof(e->param_name));
        }
        break;
    case 21u:
        if (e->param != 0u) {
            pokebw_gear_msg_line(MSG_MOVE_BANK, (uint32_t)e->param,
                                 e->param_name, (int)sizeof(e->param_name));
        }
        break;
    case 22u:
        if (e->param != 0u) {
            load_species_name(e->param, e->param_name, (int)sizeof(e->param_name));
        }
        break;
    default:
        break;
    }
}

static void build_index(void *zukan)
{
    uint16_t sp;

    memset(&g_dex_index, 0, sizeof(g_dex_index));
    g_dex_index.valid = 1u;
    g_dex_index.count = POKEBW_DEX_MAX_SPECIES;

    for (sp = 1u; sp <= POKEBW_DEX_MAX_SPECIES; sp++) {
        uint8_t t1 = 0u, t2 = 0u;
        if (POKE_TOOL_GetPersonalParam != NULL) {
            uint32_t p1 = POKE_TOOL_GetPersonalParam(sp, 0u, 6u);
            uint32_t p2 = POKE_TOOL_GetPersonalParam(sp, 0u, 7u);
            t1 = p1 < 18u ? (uint8_t)p1 : 17u;
            t2 = p2 < 18u ? (uint8_t)p2 : t1;
        }
        g_dex_index.type1[sp] = t1;
        g_dex_index.type2[sp] = t2;
        g_dex_index.dex_state[sp] = read_dex_state(zukan, sp);
        load_species_name(sp, g_dex_index.names[sp], POKEBW_DEX_NAME_MAX);
    }
}

static void collect_evolutions(uint16_t species)
{
    struct PokebwDexEvoData raw;
    uint16_t s;
    int i;

    g_dex_detail.trade_evo_fix_active = bw_pokebw_setting(POKEBW_SET_TRADE_EVO_FIX) ? 1u : 0u;
    if (SHINKA_CHECK_LoadData == NULL) {
        return;
    }

    memset(&raw, 0, sizeof(raw));
    SHINKA_CHECK_LoadData(species, &raw);
    for (i = 0; i < 7 && g_dex_detail.evo_count < POKEBW_DEX_MAX_EVOS; i++) {
        if (raw.rows[i].method == 0u || raw.rows[i].species == 0u ||
            raw.rows[i].species > POKEBW_DEX_MAX_SPECIES) {
            continue;
        }
        PokebwDexEvo *e = &g_dex_detail.evos[g_dex_detail.evo_count++];
        memset(e, 0, sizeof(*e));
        e->target_species = raw.rows[i].species;
        e->method = raw.rows[i].method;
        e->param = raw.rows[i].param;
        load_species_name(e->target_species, e->target_name, (int)sizeof(e->target_name));
        fill_evo_param_name(e);
    }

    for (s = 1u; s <= POKEBW_DEX_MAX_SPECIES && g_dex_detail.pre_evo_count < POKEBW_DEX_MAX_PRE_EVO; s++) {
        if (s == species) {
            continue;
        }
        memset(&raw, 0, sizeof(raw));
        SHINKA_CHECK_LoadData(s, &raw);
        for (i = 0; i < 7; i++) {
            if (raw.rows[i].method != 0u && raw.rows[i].species == species) {
                PokebwDexEvo *e = &g_dex_detail.pre_evos[g_dex_detail.pre_evo_count++];
                memset(e, 0, sizeof(*e));
                e->target_species = s;
                e->method = raw.rows[i].method;
                e->param = raw.rows[i].param;
                load_species_name(s, e->target_name, (int)sizeof(e->target_name));
                fill_evo_param_name(e);
                break;
            }
        }
    }
}

static void collect_learnsets(uint16_t species, uint8_t form)
{
    /* Level-up learnset (archive 0x12) */
    if (POKE_PERSONAL_LoadDataDirect != NULL) {
        uint16_t pairs[256];
        int i;

        memset(pairs, 0xFF, sizeof(pairs));
        POKE_PERSONAL_LoadDataDirect(species, (int)form, pairs);
        for (i = 0; i + 1 < 256 && pairs[i] != 0xFFFFu && pairs[i] != 0u; i += 2) {
            uint16_t mv = pairs[i];
            uint16_t lv = pairs[i + 1];
            if (g_dex_detail.lvl_count < POKEBW_DEX_MAX_LVL_MOVES) {
                fill_move_entry(&g_dex_detail.lvl_moves[g_dex_detail.lvl_count++],
                                mv, lv > 100u ? 100u : (uint8_t)lv);
            }
        }
    }

    /* TM01..TM95 and HM01..HM06 compatibility */
    if (POKE_TOOL_CheckSpeciesTmCompat != NULL && FILE_UTIL_GetMachineMove != NULL &&
        FILE_UTIL_GetId != NULL) {
        uint32_t item;
        /* TM01..TM92: 0x148..0x1A3 */
        for (item = 0x148u; item <= 0x1A3u && g_dex_detail.tm_count < POKEBW_DEX_MAX_TM_MOVES; item++) {
            if (POKE_TOOL_CheckSpeciesTmCompat(species, form, (int32_t)FILE_UTIL_GetId(item)) != 0u) {
                uint16_t mv = FILE_UTIL_GetMachineMove((uint16_t)item);
                if (mv != 0u) {
                    fill_move_entry(&g_dex_detail.tm_moves[g_dex_detail.tm_count++],
                                    mv, (uint8_t)(1u + (item - 0x148u)));
                }
            }
        }
        /* TM93..TM95: 0x26A..0x26C */
        for (item = 0x26Au; item <= 0x26Cu && g_dex_detail.tm_count < POKEBW_DEX_MAX_TM_MOVES; item++) {
            if (POKE_TOOL_CheckSpeciesTmCompat(species, form, (int32_t)FILE_UTIL_GetId(item)) != 0u) {
                uint16_t mv = FILE_UTIL_GetMachineMove((uint16_t)item);
                if (mv != 0u) {
                    fill_move_entry(&g_dex_detail.tm_moves[g_dex_detail.tm_count++],
                                    mv, (uint8_t)(93u + (item - 0x26Au)));
                }
            }
        }
        /* HM01..HM06: 0x1A4..0x1A9 (tag 101..106) */
        for (item = 0x1A4u; item <= 0x1A9u && g_dex_detail.tm_count < POKEBW_DEX_MAX_TM_MOVES; item++) {
            if (POKE_TOOL_CheckSpeciesTmCompat(species, form, (int32_t)FILE_UTIL_GetId(item)) != 0u) {
                uint16_t mv = FILE_UTIL_GetMachineMove((uint16_t)item);
                if (mv != 0u) {
                    fill_move_entry(&g_dex_detail.tm_moves[g_dex_detail.tm_count++],
                                    mv, (uint8_t)(101u + (item - 0x1A4u)));
                }
            }
        }
    }

    /* Egg moves (archive 0x7B): check species, falling back through pre-evos if empty */
    if (ARC_TOOL_LoadMember != NULL && GFL_HEAP_Free != NULL) {
        uint32_t arc_cnt = (ARC_TOOL_GetArchiveFileCount != NULL)
                               ? ARC_TOOL_GetArchiveFileCount(BW_EGG_MOVE_ARC)
                               : (uint32_t)(POKEBW_DEX_MAX_SPECIES + 1);
        uint16_t query_sp = species;
        int depth;

        for (depth = 0; depth < 3 && query_sp != 0u && (uint32_t)query_sp < arc_cnt; depth++) {
            uint16_t *egg = (uint16_t *)ARC_TOOL_LoadMember(BW_EGG_MOVE_ARC, query_sp, (uint16_t)BW_TEMP_HEAP);
            uint32_t cnt = 0u;
            if (egg != NULL) {
                cnt = egg[0];
                if (cnt > 0u) {
                    uint32_t i;
                    for (i = 1u; i <= cnt && i < 64u && g_dex_detail.egg_count < POKEBW_DEX_MAX_EGG_MOVES; i++) {
                        if (egg[i] != 0u) {
                            fill_move_entry(&g_dex_detail.egg_moves[g_dex_detail.egg_count++], egg[i], 0u);
                        }
                    }
                    GFL_HEAP_Free(egg);
                    break;
                }
                GFL_HEAP_Free(egg);
            }
            if (depth == 0 && g_dex_detail.pre_evo_count > 0u) {
                query_sp = g_dex_detail.pre_evos[0].target_species;
            } else if (depth == 1 && SHINKA_CHECK_LoadData != NULL) {
                /* Walk one step further up toward the base stage if needed. */
                struct PokebwDexEvoData raw;
                uint16_t root = 0u, s;
                int j;
                for (s = 1u; s <= POKEBW_DEX_MAX_SPECIES && root == 0u; s++) {
                    memset(&raw, 0, sizeof(raw));
                    SHINKA_CHECK_LoadData(s, &raw);
                    for (j = 0; j < 7; j++) {
                        if (raw.rows[j].method != 0u && raw.rows[j].species == query_sp) {
                            root = s;
                            break;
                        }
                    }
                }
                query_sp = root;
            } else {
                break;
            }
        }
    }

    /* Gen 5 Move Tutors (indices 0..6) */
    if (POKE_TOOL_CheckSpeciesTutorCompat != NULL) {
        int32_t t;
        for (t = 0; t < 7 && g_dex_detail.tutor_count < POKEBW_DEX_MAX_TUTOR_MOVES; t++) {
            if (POKE_TOOL_CheckSpeciesTutorCompat(species, form, t) != 0u) {
                fill_move_entry(&g_dex_detail.tutor_moves[g_dex_detail.tutor_count++],
                                k_tutor_moves[t], (uint8_t)t);
            }
        }
    }
}

static void record_habitat_hit(
    uint16_t zone_id,
    uint16_t place_id,
    uint8_t habitat,
    uint8_t season_mask,
    const struct PokebwDexEncSlot *slots,
    const uint8_t *weights,
    int nslots,
    uint16_t target_species)
{
    uint16_t pct_sum = 0u;
    uint8_t min_lv = 255u;
    uint8_t max_lv = 0u;
    int i;

    for (i = 0; i < nslots; i++) {
        uint8_t lo = slots[i].minLevel;
        uint8_t hi = slots[i].maxLevel;
        if (slots[i].species != target_species || lo == 0u) {
            continue;
        }
        if (hi < lo) {
            hi = lo;
        }
        pct_sum += weights[i];
        if (lo < min_lv) {
            min_lv = lo;
        }
        if (hi > max_lv) {
            max_lv = hi;
        }
    }
    if (pct_sum == 0u) {
        return;
    }
    if (pct_sum > 100u) {
        pct_sum = 100u;
    }

    for (i = 0; i < (int)g_dex_detail.loc_count; i++) {
        PokebwDexLocation *loc = &g_dex_detail.locations[i];
        if (loc->place_id == place_id && loc->habitat == habitat) {
            loc->seasons_mask |= season_mask;
            if (min_lv < loc->min_lv) {
                loc->min_lv = min_lv;
            }
            if (max_lv > loc->max_lv) {
                loc->max_lv = max_lv;
            }
            if ((uint8_t)pct_sum > loc->chance_pct) {
                loc->chance_pct = (uint8_t)pct_sum;
            }
            return;
        }
    }

    if (g_dex_detail.loc_count >= POKEBW_DEX_MAX_LOCATIONS) {
        g_dex_detail.loc_truncated = 1u;
        return;
    }

    {
        PokebwDexLocation *loc = &g_dex_detail.locations[g_dex_detail.loc_count++];
        memset(loc, 0, sizeof(*loc));
        loc->zone_id = zone_id;
        loc->place_id = place_id;
        loc->habitat = habitat;
        loc->seasons_mask = season_mask;
        loc->min_lv = min_lv;
        loc->max_lv = max_lv;
        loc->chance_pct = (uint8_t)pct_sum;
        if (!pokebw_gear_msg_line(MSG_PLACE_BANK, (uint32_t)place_id,
                                  loc->place_name, (int)sizeof(loc->place_name))) {
            snprintf(loc->place_name, sizeof(loc->place_name), "Zone %u", (unsigned)zone_id);
        }
    }
}

static void collect_locations(uint16_t species)
{
    void *arc;
    uint32_t zone_count = 427u;
    uint32_t enc_count;
    uint32_t z;

    g_dex_detail.loc_catch_em_all_active = bw_pokebw_setting(POKEBW_SET_CATCH_EM_ALL) ? 1u : 0u;

    if (ARC_TOOL_CreateHandle == NULL || ARC_TOOL_DeleteHandle == NULL ||
        ARC_TOOL_GetHandleMemberSize == NULL || ARC_TOOL_ReadHandleMemberRange == NULL ||
        ZONEDATA_GetEncountId == NULL || ZONEDATA_GetPlaceNameId == NULL) {
        return;
    }

    if (ARC_TOOL_GetMemberSize != NULL) {
        uint32_t tbl_bytes = ARC_TOOL_GetMemberSize(BW_ZONEDATA_ARC, 0u);
        if (tbl_bytes >= 0x30u) {
            zone_count = tbl_bytes / 0x30u;
        }
    }
    enc_count = (ARC_TOOL_GetArchiveFileCount != NULL)
                    ? ARC_TOOL_GetArchiveFileCount(BW_ENCOUNT_ARC)
                    : 0xFFFFu;

    arc = ARC_TOOL_CreateHandle(BW_ENCOUNT_ARC, BW_TEMP_HEAP);
    if (arc == NULL) {
        return;
    }

    for (z = 0u; z < zone_count; z++) {
        uint32_t enc_id = ZONEDATA_GetEncountId(z);
        uint32_t member_sz;
        uint32_t rows;
        uint32_t s;
        uint16_t place_id;

        if (enc_id == 0xFFFFu || enc_id >= enc_count) {
            continue;
        }
        member_sz = ARC_TOOL_GetHandleMemberSize(arc, enc_id);
        rows = member_sz / sizeof(struct PokebwDexEncData);
        if (rows != 1u && rows != 4u) {
            continue;
        }
        place_id = (uint16_t)ZONEDATA_GetPlaceNameId(z);

        for (s = 0u; s < rows; s++) {
            struct PokebwDexEncData row;
            uint8_t season_mask = (rows == 1u) ? 0x0Fu : (uint8_t)(1u << s);

            memset(&row, 0, sizeof(row));
            ARC_TOOL_ReadHandleMemberRange(arc, enc_id, s * (uint32_t)sizeof(row),
                                           (uint32_t)sizeof(row), &row);
            if (pokebw_catch_em_all_apply_if_enabled != NULL) {
                pokebw_catch_em_all_apply_if_enabled(&row, z);
            }

            if (row.rate[0] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 0u, season_mask, row.ground[0], k_ground_pct, 12, species);
            }
            if (row.rate[1] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 1u, season_mask, row.ground[1], k_ground_pct, 12, species);
            }
            if (row.rate[2] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 2u, season_mask, row.ground[2], k_ground_pct, 12, species);
            }
            if (row.rate[3] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 3u, season_mask, row.water[0], k_water_pct, 5, species);
            }
            if (row.rate[4] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 4u, season_mask, row.water[1], k_water_pct, 5, species);
            }
            if (row.rate[5] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 5u, season_mask, row.fishing[0], k_fish_pct, 5, species);
            }
            if (row.rate[6] != 0u) {
                record_habitat_hit((uint16_t)z, place_id, 6u, season_mask, row.fishing[1], k_fish_pct, 5, species);
            }
        }
    }

    ARC_TOOL_DeleteHandle(arc);

    /* Sort locations by Unova zone progression then habitat. */
    {
        int i, j;
        for (i = 0; i < (int)g_dex_detail.loc_count; i++) {
            for (j = i + 1; j < (int)g_dex_detail.loc_count; j++) {
                const PokebwDexLocation *a = &g_dex_detail.locations[i];
                const PokebwDexLocation *b = &g_dex_detail.locations[j];
                if (b->zone_id < a->zone_id ||
                    (b->zone_id == a->zone_id && b->habitat < a->habitat)) {
                    PokebwDexLocation tmp = *a;
                    g_dex_detail.locations[i] = *b;
                    g_dex_detail.locations[j] = tmp;
                }
            }
        }
    }
}

static void build_detail(uint16_t species, uint8_t form, void *zukan)
{
    uint32_t fc = 1u;
    int i;

    memset(&g_dex_detail, 0, sizeof(g_dex_detail));
    if (species == 0u || species > POKEBW_DEX_MAX_SPECIES) {
        return;
    }

    if (POKE_TOOL_GetPersonalParam != NULL) {
        fc = POKE_TOOL_GetPersonalParam(species, 0u, 32u);
        if (fc == 0u) {
            fc = 1u;
        }
    }
    if (form >= fc) {
        form = 0u;
    }

    g_dex_detail.valid = 1u;
    g_dex_detail.species = species;
    g_dex_detail.form = form;
    g_dex_detail.form_count = (uint8_t)(fc > 255u ? 255u : fc);
    g_dex_detail.dex_state = read_dex_state(zukan, species);
    load_species_name(species, g_dex_detail.name, (int)sizeof(g_dex_detail.name));
    pokebw_gear_msg_line(MSG_CATEGORY_BANK, (uint32_t)species,
                         g_dex_detail.category, (int)sizeof(g_dex_detail.category));

    if (POKE_TOOL_GetPersonalParam != NULL) {
        static const uint32_t k_stat_fields[6] = { 0u, 1u, 2u, 4u, 5u, 3u };
        uint32_t p1 = POKE_TOOL_GetPersonalParam(species, form, 6u);
        uint32_t p2 = POKE_TOOL_GetPersonalParam(species, form, 7u);
        uint16_t sum = 0u;

        g_dex_detail.type1 = p1 < 18u ? (uint8_t)p1 : 17u;
        g_dex_detail.type2 = p2 < 18u ? (uint8_t)p2 : g_dex_detail.type1;
        g_dex_detail.catch_rate = (uint8_t)POKE_TOOL_GetPersonalParam(species, form, 8u);
        g_dex_detail.gender_ratio = (uint8_t)POKE_TOOL_GetPersonalParam(species, form, 20u);
        g_dex_detail.egg_cycles = (uint8_t)POKE_TOOL_GetPersonalParam(species, form, 21u);
        g_dex_detail.egg_group1 = (uint8_t)POKE_TOOL_GetPersonalParam(species, form, 24u);
        g_dex_detail.egg_group2 = (uint8_t)POKE_TOOL_GetPersonalParam(species, form, 25u);

        for (i = 0; i < 6; i++) {
            uint32_t v = POKE_TOOL_GetPersonalParam(species, form, k_stat_fields[i]);
            uint8_t b = v > 255u ? 255u : (uint8_t)v;
            g_dex_detail.base_stats[i] = b;
            sum += b;
        }
        g_dex_detail.bst = sum;

        for (i = 0; i < 3; i++) {
            uint16_t ab = (uint16_t)POKE_TOOL_GetPersonalParam(species, form, 26u + (uint32_t)i);
            g_dex_detail.ability_ids[i] = ab;
            if (ab != 0u) {
                if (!pokebw_gear_msg_line(MSG_ABILITY_BANK, (uint32_t)ab,
                                          g_dex_detail.abilities[i],
                                          (int)sizeof(g_dex_detail.abilities[i]))) {
                    snprintf(g_dex_detail.abilities[i], sizeof(g_dex_detail.abilities[i]),
                             "Ability %u", (unsigned)ab);
                }
            }
        }
    }

    collect_evolutions(species);
    collect_learnsets(species, form);
    collect_locations(species);
}

void pokebw_dex_lookup_on_field_tick(void *gameSystem, void *fieldMap)
{
    uint32_t req;
    int want_index;
    uint16_t species;
    uint8_t form;
    void *gd;
    void *zukan;

    if (gameSystem == NULL || fieldMap == NULL) {
        return;
    }

    req = bw_host_dex_request_poll();
    if (req == 0u) {
        return;
    }

    want_index = ((req >> 24) & 1u) != 0u;
    form = (uint8_t)((req >> 16) & 0xFFu);
    species = (uint16_t)(req & 0xFFFFu);

    gd = (GAMESYSTEM_GetGameData != NULL) ? GAMESYSTEM_GetGameData(gameSystem) : NULL;
    zukan = (gd != NULL && ZUKAN_SAVE_GetFromGameData != NULL)
                ? ZUKAN_SAVE_GetFromGameData(gd)
                : NULL;

    if (want_index || g_dex_index.valid == 0u) {
        build_index(zukan);
        bw_host_dex_index_push(&g_dex_index, (unsigned)sizeof(g_dex_index));
    }

    if (species >= 1u && species <= POKEBW_DEX_MAX_SPECIES) {
        build_detail(species, form, zukan);
        bw_host_dex_detail_push(&g_dex_detail, (unsigned)sizeof(g_dex_detail));
    }
}
#else
void pokebw_dex_lookup_on_field_tick(void *gameSystem, void *fieldMap)
{
    (void)gameSystem;
    (void)fieldMap;
}
#endif
