/*
 * sdk/pokebw_daycare_preview.c -- Phase 4.7: Gear Egg Step Counter & Route 3
 * Day Care Preview.
 *
 * Computes live remaining steps & progress for every Egg in the player's party
 * (respecting GameData Egg step accumulator, Hatching Pass Power via
 * GPOWER_ApplyHatchRate, and Flame Body / Magma Armor 2x speed-up) and builds
 * a live Route 3 Day Care compatibility + offspring preview using ov021's
 * deterministic SODATEYA_* breeding helpers.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "overlay.h"
#include "pokebw_daycare_preview.h"
#include "pokebw_gear.h"
#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_DC_WEAK __attribute__((weak))
#else
#define POKEBW_DC_WEAK
#endif

/* Pokemon data parameters (aletheia/src/game/arm9/poke_tool_param.h) */
#define P_SPECIES   0x05u
#define P_HELD_ITEM 0x06u
#define P_EXP       0x08u
#define P_FRIENDSHIP 0x09u
#define P_ABILITY   0x0Au
#define P_LANGUAGE  0x0Cu
#define P_MOVE1     0x36u
#define P_IV_HP     0x46u
#define P_IV_ATK    0x47u
#define P_IV_DEF    0x48u
#define P_IV_SPE    0x49u
#define P_IV_SPA    0x4Au
#define P_IV_SPD    0x4Bu
#define P_IS_EGG    0x4Cu
#define P_GENDER    0x6Eu
#define P_FORM      0x6Fu
#define P_NATURE    0x70u
#define P_NICKNAME  0x74u
#define P_LEVEL     0x9Eu

/* Personal data field IDs (aletheia/src/game/arm9/poke_personal.c) */
#define PERSONAL_EGG_CYCLES 21u

#define ABILITY_MAGMA_ARMOR 0x28u
#define ABILITY_FLAME_BODY  0x31u
#define ITEM_EVERSTONE      0x00E5u
#define ITEM_POWER_FIRST    289u /* Power Weight .. Power Anklet (289..294) */
#define ITEM_POWER_LAST     294u

#define SPECIES_NIDORAN_F   0x001Du
#define SPECIES_NIDORAN_M   0x0020u
#define SPECIES_VOLBEAT     0x0139u
#define SPECIES_ILLUMISE    0x013Au

#define MSG_ITEM_BANK    54
#define MSG_SPECIES_BANK 70
#define MSG_MOVE_BANK    203

/* Matches ov021/sodateya.c DayCareEgg (0x8C bytes on ILP32). */
struct PokebwPreviewEgg {
    uint32_t species;            /* 0x00 */
    uint32_t form;               /* 0x04 */
    uint32_t level;              /* 0x08 */
    uint32_t nature;             /* 0x0C */
    uint32_t ability;            /* 0x10 */
    uint32_t baseFriendship;     /* 0x14 */
    uint32_t ivs[6];             /* 0x18 */
    uint32_t otId;               /* 0x30 */
    uint32_t personalRnd;        /* 0x34 */
    uint32_t moveCount;          /* 0x38 */
    uint32_t moves[4];           /* 0x3C */
    uint32_t abilitySlot;        /* 0x4C */
    uint32_t natureFromFather;   /* 0x50 */
    uint32_t natureFromMother;   /* 0x54 */
    uint32_t fatherIvFlags[6];   /* 0x58 */
    uint32_t motherIvFlags[6];   /* 0x70 */
    uint32_t inheritedIvCount;   /* 0x88 */
};

typedef char pokebw_preview_egg_size_check[
    sizeof(struct PokebwPreviewEgg) == 0x8Cu ? 1 : -1];

/* ARM9 symbols */
extern void    *GAME_DATA_GetSaveControl(const void *gd) POKEBW_DC_WEAK;
extern void    *PLAYER_LOCATION_GetData(void *saveControl) POKEBW_DC_WEAK;
extern void     PLAYER_LOCATION_GetEggStepCounter(const void *loc, uint32_t *out) POKEBW_DC_WEAK;
extern uint32_t GPOWER_ApplyHatchRate(uint32_t steps) POKEBW_DC_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_DC_WEAK;
extern void    *POKEPARTY_GetMember(void *party, uint32_t pos) POKEBW_DC_WEAK;
extern uint32_t POKE_TOOL_GetPartyValue(void *pp, uint32_t paramId, uintptr_t arg) POKEBW_DC_WEAK;
extern uint32_t POKE_TOOL_GetPersonalParam(uint32_t species, uint32_t form, uint32_t field) POKEBW_DC_WEAK;
extern uint32_t POKE_TOOL_CalcLevelFromExp(uint16_t species, uint16_t form, uint32_t exp) POKEBW_DC_WEAK;
extern void    *DAYCARE_GetFromSave(void *saveControl) POKEBW_DC_WEAK;
extern uint32_t DAYCARE_GetEggReady(void *daycare) POKEBW_DC_WEAK;
extern uint32_t DAYCARE_IsSlotInUse(void *daycare, uint32_t slot) POKEBW_DC_WEAK;
extern void    *DAYCARE_GetSlotMon(void *daycare, int32_t slot) POKEBW_DC_WEAK;
extern uint32_t DAYCARE_GetSlotSteps(void *daycare, uint32_t slot) POKEBW_DC_WEAK;

/* ov021 symbols */
extern uint16_t ov021_FIELDMAP_GetHeapId(void *fieldMap) POKEBW_DC_WEAK;
extern void    *ov021_FIELDMAP_GetSodateya(void *fieldMap) POKEBW_DC_WEAK;
extern int      ov021_SODATEYA_GetCurrentLevel(void *work, uint32_t slot) POKEBW_DC_WEAK;
extern int      ov021_SODATEYA_GetLevelsGained(void *work, int slot) POKEBW_DC_WEAK;
extern int      ov021_SODATEYA_CalcCompatibility(void *monA, void *monB) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_SortParentsByGender(void **parents) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_PutDittoSecond(void **parents) POKEBW_DC_WEAK;
extern int      ov021_SODATEYA_InitEgg(void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_SetBaseSpecies(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_ApplyPhioneRule(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_ApplyIncenseBabyRule(void **parents, void *egg) POKEBW_DC_WEAK;
extern int      ov021_SODATEYA_ResetEggForm(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_InheritFormShellos(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_InheritFormBurmy(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_InheritFormBasculin(void **parents, void *egg) POKEBW_DC_WEAK;
extern void    *ov021_SODATEYA_GetSpeciesParent(void **parents) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_AddLevelUpMoves(void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_AddEggMoves(void **parents, void *egg, uint32_t heapId) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_AddSharedMoves(void **parents, void *egg) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_AddTmMoves(void **parents, void *egg, uint32_t heapId) POKEBW_DC_WEAK;
extern void     ov021_SODATEYA_AddVoltTackle(void **parents, void *egg) POKEBW_DC_WEAK;

struct DaycareCache {
    int      valid;
    uint32_t key[12];
    uint16_t egg_species;
    uint8_t  egg_alt_flag;
    uint8_t  egg_cycles;
    uint8_t  egg_move_count;
    uint16_t egg_moves[4];
    char     egg_species_name[24];
    char     egg_move_names[4][24];
};

static struct DaycareCache g_dc_cache;

static int dc_put_utf8(char *out, int at, int cap, uint32_t cp)
{
    if (cp < 0x80u) {
        if (at + 1 >= cap) return at;
        out[at++] = (char)cp;
    } else if (cp < 0x800u) {
        if (at + 2 >= cap) return at;
        out[at++] = (char)(0xC0u | (cp >> 6));
        out[at++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        if (at + 3 >= cap) return at;
        out[at++] = (char)(0xE0u | (cp >> 12));
        out[at++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[at++] = (char)(0x80u | (cp & 0x3Fu));
    }
    return at;
}

static void dc_units_to_utf8(const uint16_t *u, int n, char *out, int cap)
{
    int i = 0, at = 0;
    if (cap <= 0) {
        return;
    }
    while (i < n && at < cap - 4) {
        uint32_t c = u[i++];
        if (c == 0xFFFFu || c == 0u) {
            break;
        }
        if (c == 0xF000u) {
            if (i + 1 < n) {
                i += 2 + (int)u[i + 1];
            }
            continue;
        }
        if (c == 0xFFFEu) {
            at = dc_put_utf8(out, at, cap, ' ');
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        at = dc_put_utf8(out, at, cap, c);
    }
    out[at] = '\0';
}

static uint32_t read_egg_step_counter(void *gd)
{
    void *sc;
    void *loc;
    uint32_t step = 0u;

    if (gd == NULL || GAME_DATA_GetSaveControl == NULL ||
        PLAYER_LOCATION_GetData == NULL ||
        PLAYER_LOCATION_GetEggStepCounter == NULL) {
        return 0u;
    }
    sc = GAME_DATA_GetSaveControl(gd);
    if (sc == NULL) {
        return 0u;
    }
    loc = PLAYER_LOCATION_GetData(sc);
    if (loc == NULL) {
        return 0u;
    }
    PLAYER_LOCATION_GetEggStepCounter(loc, &step);
    return step;
}

static int party_has_hatch_boost(void *party)
{
    uint32_t n, i;

    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0;
    }
    n = POKEPARTY_GetCount(party);
    if (n > POKEBW_GEAR_MAX_PARTY) {
        n = POKEBW_GEAR_MAX_PARTY;
    }
    for (i = 0u; i < n; i++) {
        void *pp = POKEPARTY_GetMember(party, i);
        if (pp == NULL) {
            continue;
        }
        if (POKE_TOOL_GetPartyValue(pp, P_IS_EGG, 0u) == 0u) {
            uint32_t ab = POKE_TOOL_GetPartyValue(pp, P_ABILITY, 0u);
            if (ab == ABILITY_FLAME_BODY || ab == ABILITY_MAGMA_ARMOR) {
                return 1;
            }
        }
    }
    return 0;
}

uint32_t pokebw_daycare_egg_step_signature(void *gd, void *party)
{
    uint32_t sig = 0u;
    uint32_t n = 0u, i;

    if (gd == NULL) {
        return 0u;
    }
    sig ^= read_egg_step_counter(gd) * 13u;

    if (party != NULL && POKEPARTY_GetCount != NULL &&
        POKEPARTY_GetMember != NULL && POKE_TOOL_GetPartyValue != NULL) {
        n = POKEPARTY_GetCount(party);
        if (n > POKEBW_GEAR_MAX_PARTY) {
            n = POKEBW_GEAR_MAX_PARTY;
        }
        for (i = 0u; i < n; i++) {
            void *pp = POKEPARTY_GetMember(party, i);
            if (pp != NULL && POKE_TOOL_GetPartyValue(pp, P_IS_EGG, 0u) != 0u) {
                sig = (sig * 33u) ^ (0x100u + POKE_TOOL_GetPartyValue(pp, P_FRIENDSHIP, 0u));
            }
        }
    }

    if (GAME_DATA_GetSaveControl != NULL && DAYCARE_GetFromSave != NULL) {
        void *sc = GAME_DATA_GetSaveControl(gd);
        void *dc = sc != NULL ? DAYCARE_GetFromSave(sc) : NULL;
        if (dc != NULL && DAYCARE_IsSlotInUse != NULL &&
            DAYCARE_GetEggReady != NULL && DAYCARE_GetSlotSteps != NULL) {
            uint32_t ready = DAYCARE_GetEggReady(dc) ? 1u : 0u;
            uint32_t u0 = DAYCARE_IsSlotInUse(dc, 0u) ? 1u : 0u;
            uint32_t u1 = DAYCARE_IsSlotInUse(dc, 1u) ? 1u : 0u;
            sig ^= (ready << 20) ^ (u0 << 21) ^ (u1 << 22);
            if (u0) sig ^= DAYCARE_GetSlotSteps(dc, 0u) * 7u;
            if (u1) sig ^= DAYCARE_GetSlotSteps(dc, 1u) * 11u;
        }
    }
    return sig;
}

void pokebw_daycare_fill_party_eggs(void *gd, void *party, PokebwGearSnapshot *snap)
{
    uint32_t step_counter;
    uint32_t step_inc;
    uint32_t steps_per_cycle;
    uint32_t cur_rem;
    uint32_t dec;
    int boost;
    uint32_t n, i;

    if (gd == NULL || party == NULL || snap == NULL ||
        POKEPARTY_GetCount == NULL || POKEPARTY_GetMember == NULL ||
        POKE_TOOL_GetPartyValue == NULL) {
        return;
    }

    boost = party_has_hatch_boost(party);
    dec = boost ? 2u : 1u;
    step_counter = read_egg_step_counter(gd);
    step_inc = (GPOWER_ApplyHatchRate != NULL) ? GPOWER_ApplyHatchRate(0x100u) : 0x100u;
    if (step_inc == 0u) {
        step_inc = 0x100u;
    }
    steps_per_cycle = (0x10000u / step_inc) + 1u;
    cur_rem = (step_counter <= 0x10000u) ? ((0x10000u - step_counter) / step_inc + 1u) : 1u;

    n = snap->party_count;
    if (n > POKEBW_GEAR_MAX_PARTY) {
        n = POKEBW_GEAR_MAX_PARTY;
    }
    for (i = 0u; i < n; i++) {
        void *pp = POKEPARTY_GetMember(party, i);
        PokebwGearMon *dst = &snap->party[i];
        uint32_t cycles, species, form, base_cycles, dec_left, dec_max, steps_left, steps_max;

        if (pp == NULL || !dst->is_egg) {
            continue;
        }
        cycles = POKE_TOOL_GetPartyValue(pp, P_FRIENDSHIP, 0u);
        species = POKE_TOOL_GetPartyValue(pp, P_SPECIES, 0u);
        form = POKE_TOOL_GetPartyValue(pp, P_FORM, 0u);
        base_cycles = (POKE_TOOL_GetPersonalParam != NULL)
                          ? POKE_TOOL_GetPersonalParam(species, form, PERSONAL_EGG_CYCLES)
                          : cycles;
        if (base_cycles < cycles) {
            base_cycles = cycles;
        }
        if (base_cycles == 0u) {
            base_cycles = 1u;
        }

        if (cycles == 0u) {
            steps_left = 0u;
        } else {
            dec_left = (cycles + dec - 1u) / dec;
            steps_left = cur_rem + (dec_left - 1u) * steps_per_cycle;
        }
        dec_max = (base_cycles + dec - 1u) / dec;
        steps_max = dec_max * steps_per_cycle;
        if (steps_max < steps_left) {
            steps_max = steps_left;
        }
        if (steps_left > 0xFFFFu) steps_left = 0xFFFFu;
        if (steps_max > 0xFFFFu) steps_max = 0xFFFFu;

        dst->egg_cycles_left = cycles > 255u ? 255u : (uint8_t)cycles;
        dst->egg_hatch_boost = (uint8_t)boost;
        dst->egg_steps_left = (uint16_t)steps_left;
        dst->egg_steps_max = (uint16_t)steps_max;
    }
}

static void build_preview_key(void *mon0, void *mon1, uint32_t key[12])
{
    static const uint32_t params[6] = {
        P_SPECIES, P_FORM, P_HELD_ITEM, P_MOVE1, P_MOVE1 + 1u, P_MOVE1 + 2u
    };
    int k;
    for (k = 0; k < 6; k++) {
        key[k] = POKE_TOOL_GetPartyValue(mon0, params[k], 0u);
        key[6 + k] = POKE_TOOL_GetPartyValue(mon1, params[k], 0u);
    }
}

static void compute_offspring_preview(void *fieldMap, void *mon0, void *mon1,
                                      PokebwGearDaycare *out)
{
    uint32_t cur_key[12];
    void *parents[2];
    struct PokebwPreviewEgg egg;
    uint32_t heap_id;
    uint32_t base_sp, i;

    build_preview_key(mon0, mon1, cur_key);
    /* Include move 4 in slot 5 & 11 XOR to avoid adding another array pass. */
    cur_key[5] ^= POKE_TOOL_GetPartyValue(mon0, P_MOVE1 + 3u, 0u) << 16;
    cur_key[11] ^= POKE_TOOL_GetPartyValue(mon1, P_MOVE1 + 3u, 0u) << 16;

    if (!g_dc_cache.valid || memcmp(g_dc_cache.key, cur_key, sizeof(cur_key)) != 0) {
        if (ov021_SODATEYA_SortParentsByGender == NULL ||
            ov021_SODATEYA_PutDittoSecond == NULL ||
            ov021_SODATEYA_InitEgg == NULL ||
            ov021_SODATEYA_SetBaseSpecies == NULL ||
            ov021_SODATEYA_GetSpeciesParent == NULL ||
            ov021_SODATEYA_ApplyPhioneRule == NULL ||
            ov021_SODATEYA_ApplyIncenseBabyRule == NULL ||
            ov021_SODATEYA_ResetEggForm == NULL ||
            ov021_SODATEYA_InheritFormShellos == NULL ||
            ov021_SODATEYA_InheritFormBurmy == NULL ||
            ov021_SODATEYA_InheritFormBasculin == NULL ||
            ov021_SODATEYA_AddLevelUpMoves == NULL ||
            ov021_SODATEYA_AddEggMoves == NULL ||
            ov021_SODATEYA_AddSharedMoves == NULL ||
            ov021_SODATEYA_AddTmMoves == NULL ||
            ov021_SODATEYA_AddVoltTackle == NULL) {
            return;
        }

        parents[0] = mon0;
        parents[1] = mon1;
        ov021_SODATEYA_SortParentsByGender(parents);
        ov021_SODATEYA_PutDittoSecond(parents);
        (void)ov021_SODATEYA_InitEgg(&egg);
        ov021_SODATEYA_SetBaseSpecies(parents, &egg);

        base_sp = POKE_TOOL_GetPartyValue(ov021_SODATEYA_GetSpeciesParent(parents), P_SPECIES, 0u);
        g_dc_cache.egg_alt_flag = 0u;
        if (base_sp == SPECIES_NIDORAN_F || (base_sp >= SPECIES_NIDORAN_M && base_sp <= SPECIES_NIDORAN_M + 2u)) {
            g_dc_cache.egg_alt_flag = 1u;
            egg.species = SPECIES_NIDORAN_F;
        } else if (base_sp == SPECIES_VOLBEAT || base_sp == SPECIES_ILLUMISE) {
            g_dc_cache.egg_alt_flag = 2u;
            egg.species = SPECIES_VOLBEAT;
        }

        ov021_SODATEYA_ApplyPhioneRule(parents, &egg);
        ov021_SODATEYA_ApplyIncenseBabyRule(parents, &egg);
        (void)ov021_SODATEYA_ResetEggForm(parents, &egg);
        ov021_SODATEYA_InheritFormShellos(parents, &egg);
        ov021_SODATEYA_InheritFormBurmy(parents, &egg);
        ov021_SODATEYA_InheritFormBasculin(parents, &egg);

        heap_id = (fieldMap != NULL && ov021_FIELDMAP_GetHeapId != NULL)
                      ? (uint32_t)ov021_FIELDMAP_GetHeapId(fieldMap)
                      : 4u;
        ov021_SODATEYA_AddLevelUpMoves(&egg);
        ov021_SODATEYA_AddEggMoves(parents, &egg, heap_id);
        ov021_SODATEYA_AddSharedMoves(parents, &egg);
        ov021_SODATEYA_AddTmMoves(parents, &egg, heap_id);
        ov021_SODATEYA_AddVoltTackle(parents, &egg);

        g_dc_cache.egg_species = (uint16_t)egg.species;
        g_dc_cache.egg_cycles = (POKE_TOOL_GetPersonalParam != NULL)
                                    ? (uint8_t)POKE_TOOL_GetPersonalParam(egg.species, egg.form, PERSONAL_EGG_CYCLES)
                                    : 20u;
        if (!pokebw_gear_msg_line(MSG_SPECIES_BANK, egg.species,
                                  g_dc_cache.egg_species_name,
                                  (int)sizeof(g_dc_cache.egg_species_name))) {
            snprintf(g_dc_cache.egg_species_name, sizeof(g_dc_cache.egg_species_name),
                     "#%u", (unsigned)egg.species);
        }
        g_dc_cache.egg_move_count = egg.moveCount > 4u ? 4u : (uint8_t)egg.moveCount;
        for (i = 0u; i < g_dc_cache.egg_move_count; i++) {
            g_dc_cache.egg_moves[i] = (uint16_t)egg.moves[i];
            if (!pokebw_gear_msg_line(MSG_MOVE_BANK, egg.moves[i],
                                      g_dc_cache.egg_move_names[i],
                                      (int)sizeof(g_dc_cache.egg_move_names[i]))) {
                snprintf(g_dc_cache.egg_move_names[i], sizeof(g_dc_cache.egg_move_names[i]),
                         "Move %u", (unsigned)egg.moves[i]);
            }
        }
        memcpy(g_dc_cache.key, cur_key, sizeof(cur_key));
        g_dc_cache.valid = 1;
    }

    out->egg_species = g_dc_cache.egg_species;
    out->egg_alt_flag = g_dc_cache.egg_alt_flag;
    out->egg_cycles = g_dc_cache.egg_cycles;
    out->egg_move_count = g_dc_cache.egg_move_count;
    snprintf(out->egg_species_name, sizeof(out->egg_species_name), "%s", g_dc_cache.egg_species_name);
    for (i = 0u; i < out->egg_move_count; i++) {
        out->egg_moves[i] = g_dc_cache.egg_moves[i];
        snprintf(out->egg_move_names[i], sizeof(out->egg_move_names[i]),
                 "%s", g_dc_cache.egg_move_names[i]);
    }
}

void pokebw_daycare_fill_preview(void *gd, void *fieldMap, PokebwGearDaycare *out)
{
    static const uint32_t iv_params[6] = {
        P_IV_HP, P_IV_ATK, P_IV_DEF, P_IV_SPA, P_IV_SPD, P_IV_SPE
    };
    void *sc;
    void *dc;
    void *sodateya;
    void *mons[2] = { NULL, NULL };
    uint32_t s;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->power_stat[0] = -1;
    out->power_stat[1] = -1;
    out->shiny_odds_setting = (uint8_t)bw_pokebw_setting(POKEBW_SET_SHINY_ODDS);

    if (gd == NULL || GAME_DATA_GetSaveControl == NULL ||
        DAYCARE_GetFromSave == NULL || DAYCARE_IsSlotInUse == NULL ||
        DAYCARE_GetSlotMon == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return;
    }

    sc = GAME_DATA_GetSaveControl(gd);
    if (sc == NULL) {
        return;
    }
    dc = DAYCARE_GetFromSave(sc);
    if (dc == NULL) {
        return;
    }

    out->valid = 1u;
    out->egg_ready = (DAYCARE_GetEggReady != NULL && DAYCARE_GetEggReady(dc) != 0u) ? 1u : 0u;
    sodateya = (fieldMap != NULL && bw_overlay_is_loaded(21) && ov021_FIELDMAP_GetSodateya != NULL)
                   ? ov021_FIELDMAP_GetSodateya(fieldMap)
                   : NULL;

    for (s = 0u; s < 2u; s++) {
        PokebwGearDaycareParent *dst = &out->parents[s];
        void *mon;
        uint16_t nick[24];
        uint32_t species, form, exp, stored_lv, cur_lv, gain, item;
        int k;

        if (DAYCARE_IsSlotInUse(dc, s) == 0u) {
            continue;
        }
        mon = DAYCARE_GetSlotMon(dc, (int32_t)s);
        if (mon == NULL) {
            continue;
        }
        species = POKE_TOOL_GetPartyValue(mon, P_SPECIES, 0u);
        if (species == 0u) {
            continue;
        }

        mons[s] = mon;
        out->parent_count++;
        dst->present = 1u;
        dst->species = (uint16_t)species;
        dst->gender = (uint8_t)POKE_TOOL_GetPartyValue(mon, P_GENDER, 0u);
        dst->nature = (uint8_t)POKE_TOOL_GetPartyValue(mon, P_NATURE, 0u);
        dst->language = (uint8_t)POKE_TOOL_GetPartyValue(mon, P_LANGUAGE, 0u);
        item = POKE_TOOL_GetPartyValue(mon, P_HELD_ITEM, 0u);
        dst->held_item = (uint16_t)item;

        if (sodateya != NULL && ov021_SODATEYA_GetCurrentLevel != NULL &&
            ov021_SODATEYA_GetLevelsGained != NULL) {
            int cl = ov021_SODATEYA_GetCurrentLevel(sodateya, s);
            int lg = ov021_SODATEYA_GetLevelsGained(sodateya, (int)s);
            cur_lv = cl > 0 ? (uint32_t)cl : 1u;
            gain = lg > 0 ? (uint32_t)lg : 0u;
        } else {
            form = POKE_TOOL_GetPartyValue(mon, P_FORM, 0u);
            stored_lv = POKE_TOOL_GetPartyValue(mon, P_LEVEL, 0u);
            exp = POKE_TOOL_GetPartyValue(mon, P_EXP, 0u);
            if (DAYCARE_GetSlotSteps != NULL) {
                exp += DAYCARE_GetSlotSteps(dc, s);
            }
            cur_lv = (POKE_TOOL_CalcLevelFromExp != NULL)
                         ? POKE_TOOL_CalcLevelFromExp((uint16_t)species, (uint16_t)form, exp)
                         : stored_lv;
            gain = (cur_lv > stored_lv) ? (cur_lv - stored_lv) : 0u;
        }
        dst->level = cur_lv > 100u ? 100u : (uint8_t)cur_lv;
        dst->levels_gained = gain > 100u ? 100u : (uint8_t)gain;

        for (k = 0; k < 6; k++) {
            dst->ivs[k] = (uint8_t)POKE_TOOL_GetPartyValue(mon, iv_params[k], 0u);
        }

        memset(nick, 0xFF, sizeof(nick));
        (void)POKE_TOOL_GetPartyValue(mon, P_NICKNAME, (uintptr_t)nick);
        dc_units_to_utf8(nick, 24, dst->name, (int)sizeof(dst->name));
        if (dst->name[0] == '\0') {
            if (!pokebw_gear_msg_line(MSG_SPECIES_BANK, species,
                                      dst->name, (int)sizeof(dst->name))) {
                snprintf(dst->name, sizeof(dst->name), "#%u", (unsigned)species);
            }
        }
        if (item != 0u) {
            if (!pokebw_gear_msg_line(MSG_ITEM_BANK, item,
                                      dst->item_name, (int)sizeof(dst->item_name))) {
                snprintf(dst->item_name, sizeof(dst->item_name), "Item %u", (unsigned)item);
            }
        }

        if (item == ITEM_EVERSTONE) {
            out->everstone_mask |= (uint8_t)(1u << s);
            out->everstone_nature[s] = dst->nature;
        }
        if (item >= ITEM_POWER_FIRST && item <= ITEM_POWER_LAST) {
            out->power_stat[s] = (int8_t)(item - ITEM_POWER_FIRST);
        }
    }

    if (out->parent_count == 2u && mons[0] != NULL && mons[1] != NULL &&
        bw_overlay_is_loaded(21) && ov021_SODATEYA_CalcCompatibility != NULL) {
        int compat = ov021_SODATEYA_CalcCompatibility(mons[0], mons[1]);
        out->compat_score = compat > 0 ? (uint8_t)compat : 0u;
        out->masuda_active = (out->parents[0].language != out->parents[1].language) ? 1u : 0u;
        if (out->compat_score > 0u) {
            compute_offspring_preview(fieldMap, mons[0], mons[1], out);
        }
    }
}
