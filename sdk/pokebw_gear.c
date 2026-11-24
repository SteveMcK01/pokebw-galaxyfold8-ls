/*
 * sdk/pokebw_gear.c -- Phase 2.3: Gear Companion & Unova Route Radar.
 *
 * Collects live party move-pill state, Pokédex seen/caught flags, active
 * save slot, zone name, season, and Unova encounter tables (Tall Grass,
 * Dark Grass, Phenomena, Surfing, Rippling Water, Fishing, Rippling Fishing)
 * and pushes compact fixed-layout snapshots to the native ImGui host overlay
 * via bw_host_gear_push().
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokebw_gear.h"
#include "pokebw_gear_state.h"
#include "pokebw_settings_ids.h"
#include "port.h"
#include "save_slots.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_GEAR_WEAK __attribute__((weak))
#else
#define POKEBW_GEAR_WEAK
#endif

#define P_SPECIES  0x05u
#define P_MOVE1    0x36u
#define P_PP1      0x3Au
#define P_MAXPP1   0x42u
#define P_IS_EGG   0x4Cu
#define P_NICKNAME 0x74u
#define P_STATUS   0x9Du
#define P_LEVEL    0x9Eu
#define P_HP       0xA0u
#define P_MAX_HP   0xA1u

#define MSG_ARC_PATH     "a/0/0/2"
#define MSG_SPECIES_BANK 70
#define MSG_PLACE_BANK   89
#define MSG_MOVE_BANK    203

struct GearEncountSlot {
    uint16_t species : 11;
    uint16_t form : 5;
    uint8_t  minLevel;
    uint8_t  maxLevel;
};

struct GearEncountData {
    uint8_t                rate[7];
    uint8_t                flags;
    struct GearEncountSlot ground[3][12];
    struct GearEncountSlot water[2][5];
    struct GearEncountSlot fishing[2][5];
};

typedef char pokebw_gear_enc_size_check[
    sizeof(struct GearEncountData) == 0xE8u ? 1 : -1];

extern void *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_GEAR_WEAK;
extern void *GAME_DATA_GetMyParty(const void *gd) POKEBW_GEAR_WEAK;
extern uint8_t GAME_DATA_GetSeason(const void *gd) POKEBW_GEAR_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_GEAR_WEAK;
extern void *POKEPARTY_GetMember(void *party, uint32_t pos) POKEBW_GEAR_WEAK;
extern uint32_t POKE_TOOL_GetPartyValue(void *pp, uint32_t paramId, uintptr_t arg) POKEBW_GEAR_WEAK;
extern uint32_t WAZA_TOOL_GetParam(uint32_t moveId, uint32_t paramId) POKEBW_GEAR_WEAK;
extern uint32_t ZONEDATA_GetPlaceNameId(uint32_t id) POKEBW_GEAR_WEAK;
extern void *ZUKAN_SAVE_GetFromGameData(void *gd) POKEBW_GEAR_WEAK;
extern uint32_t ZUKAN_SAVE_IsCaught(void *zukan, uint16_t species) POKEBW_GEAR_WEAK;
extern uint32_t ZUKAN_SAVE_IsSeen(void *zukan, uint16_t species) POKEBW_GEAR_WEAK;
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_GEAR_WEAK;

static struct GearEncountData g_enc_data;
static uint32_t g_enc_zone = 0xFFFFFFFFu;
static int g_enc_season = -1;
static int g_enc_valid = 0;
static int g_enc_dirty = 0;

static uint32_t g_last_zone = 0xFFFFFFFFu;
static uint32_t g_frame_counter = 0u;
static PokebwGearSnapshot g_snap;

/* ------------------------------------------------------------------ */
/* Message bank reader (a/0/0/2: species 70, places 89, moves 203)    */
/* ------------------------------------------------------------------ */

static BwArcFile g_msg_arc;
static int g_msg_arc_state = 0;
static struct {
    int member;
    uint8_t *data;
    uint32_t size;
} g_banks[3];

static uint16_t rd16_le(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t rd32_le(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static const uint8_t *msg_bank_get(int member, uint32_t *out_size)
{
    int i, free_slot = -1;
    long n;
    uint8_t *buf;

    for (i = 0; i < 3; i++) {
        if (g_banks[i].member == member) {
            *out_size = g_banks[i].size;
            return g_banks[i].data;
        }
        if (g_banks[i].member == 0 && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return NULL;
    }
    if (g_msg_arc_state == 0) {
        g_msg_arc_state = bw_arc_open(&g_msg_arc, MSG_ARC_PATH) == 0 ? 1 : -1;
    }
    g_banks[free_slot].member = member;
    if (g_msg_arc_state != 1 || (uint32_t)member >= g_msg_arc.count) {
        return NULL;
    }
    n = bw_arc_member_size(&g_msg_arc, (uint32_t)member);
    if (n <= 0) {
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(&g_msg_arc, (uint32_t)member, buf, (uint32_t)n) != n) {
        free(buf);
        return NULL;
    }
    g_banks[free_slot].data = buf;
    g_banks[free_slot].size = (uint32_t)n;
    *out_size = (uint32_t)n;
    return buf;
}

static int put_utf8(char *out, int at, int cap, uint32_t cp)
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

static void units_to_utf8(const uint16_t *u, int n, char *out, int cap)
{
    int i = 0, at = 0;

    if (cap <= 0) {
        return;
    }
    while (i < n && at < cap - 4) {
        uint32_t c = u[i++];

        if (c == 0xFFFFu) {
            break;
        }
        if (c == 0xF000u) {
            if (i + 1 < n) {
                uint16_t args = u[i + 1];
                i += 2 + (int)args;
            }
            continue;
        }
        if (c == 0xFFFEu) {
            at = put_utf8(out, at, cap, ' ');
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        at = put_utf8(out, at, cap, c);
    }
    out[at] = '\0';
}

static int gear_msg_line(int member, uint32_t idx, char *out, int cap)
{
    uint32_t size = 0, count, sect, off, chars, i;
    const uint8_t *b = msg_bank_get(member, &size);
    uint16_t units[128];
    uint16_t key;

    if (cap <= 0) {
        return 0;
    }
    out[0] = '\0';
    if (b == NULL || size < 20u) {
        return 0;
    }
    count = rd16_le(b + 2);
    sect = rd32_le(b + 12);
    if (idx >= count || sect + 4ull + 8ull * count > size) {
        return 0;
    }
    off = rd32_le(b + sect + 4u + 8u * idx);
    chars = rd16_le(b + sect + 4u + 8u * idx + 4u);
    if (chars == 0u || chars > 128u || sect + (uint64_t)off + 2ull * chars > size) {
        return 0;
    }
    key = (uint16_t)((idx + 3u) * 0x2983u);
    for (i = 0; i < chars; i++) {
        uint16_t v = rd16_le(b + sect + off + 2u * i);
        units[i] = (uint16_t)(v ^ key);
        key = (uint16_t)((key << 3) | (key >> 13));
    }
    units_to_utf8(units, (int)chars, out, cap);
    return out[0] != '\0';
}

/* ------------------------------------------------------------------ */
/* Encounter table aggregation & telemetry collection                  */
/* ------------------------------------------------------------------ */

static void add_habitat_slots(
    PokebwGearHabitat *hab,
    uint8_t rate,
    const struct GearEncountSlot *slots,
    const uint8_t *weights,
    int nslots,
    void *zukan)
{
    int i, j;

    memset(hab, 0, sizeof(*hab));
    hab->rate = rate;
    if (rate == 0u) {
        return;
    }

    for (i = 0; i < nslots; i++) {
        uint16_t sp = slots[i].species;
        uint8_t min_lv = slots[i].minLevel;
        uint8_t max_lv = slots[i].maxLevel;
        uint8_t pct = weights[i];
        int found = -1;

        if (sp == 0u || min_lv == 0u) {
            continue;
        }
        if (max_lv < min_lv) {
            max_lv = min_lv;
        }

        for (j = 0; j < (int)hab->count; j++) {
            if (hab->entries[j].species == sp) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            PokebwGearEncEntry *e = &hab->entries[found];
            uint16_t sum = (uint16_t)e->chance_pct + (uint16_t)pct;
            e->chance_pct = sum > 100u ? 100u : (uint8_t)sum;
            if (min_lv < e->min_lv) {
                e->min_lv = min_lv;
            }
            if (max_lv > e->max_lv) {
                e->max_lv = max_lv;
            }
        } else if (hab->count < POKEBW_GEAR_MAX_ENC) {
            PokebwGearEncEntry *e = &hab->entries[hab->count++];
            memset(e, 0, sizeof(*e));
            e->species = sp;
            e->min_lv = min_lv;
            e->max_lv = max_lv;
            e->chance_pct = pct;
            if (zukan != NULL) {
                if (ZUKAN_SAVE_IsCaught != NULL && ZUKAN_SAVE_IsCaught(zukan, sp) != 0u) {
                    e->dex_state = 2u;
                } else if (ZUKAN_SAVE_IsSeen != NULL && ZUKAN_SAVE_IsSeen(zukan, sp) != 0u) {
                    e->dex_state = 1u;
                }
            }
            if (!gear_msg_line(MSG_SPECIES_BANK, (uint32_t)sp, e->name, (int)sizeof(e->name))) {
                snprintf(e->name, sizeof(e->name), "#%u", (unsigned)sp);
            }
        }
    }

    /* Sort descending by encounter % so the common spawns appear at the top. */
    for (i = 0; i < (int)hab->count; i++) {
        for (j = i + 1; j < (int)hab->count; j++) {
            if (hab->entries[j].chance_pct > hab->entries[i].chance_pct) {
                PokebwGearEncEntry tmp = hab->entries[i];
                hab->entries[i] = hab->entries[j];
                hab->entries[j] = tmp;
            }
        }
    }
}

void pokebw_gear_on_encount_load(const void *encountData, uint32_t zoneId, int seasonIndex)
{
    if (encountData == NULL) {
        return;
    }
    memcpy(&g_enc_data, encountData, sizeof(g_enc_data));
    g_enc_zone = zoneId;
    g_enc_season = seasonIndex;
    g_enc_valid = 1;
    g_enc_dirty = 1;
}

void pokebw_gear_on_field_tick(void *gameSystem, void *fieldMap)
{
    static const uint8_t k_ground_pct[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
    static const uint8_t k_water_pct[5]   = { 60, 30, 5, 4, 1 };
    static const uint8_t k_fish_pct[5]    = { 40, 40, 15, 4, 1 };

    void *gd;
    void *party;
    void *zukan;
    uint32_t zone_id = 0u;
    uint32_t i, n;
    int h;

    if (gameSystem == NULL || fieldMap == NULL || GAMESYSTEM_GetGameData == NULL) {
        return;
    }
    if (ov021_FIELDMAP_GetZoneId != NULL) {
        zone_id = (uint32_t)ov021_FIELDMAP_GetZoneId(fieldMap);
    }

    g_frame_counter++;
    if (!g_enc_dirty && zone_id == g_last_zone && (g_frame_counter % 30u) != 1u) {
        return;
    }

    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        return;
    }

    g_last_zone = zone_id;
    g_enc_dirty = 0;

    memset(&g_snap, 0, sizeof(g_snap));
    g_snap.valid = 1u;
    g_snap.zone_id = zone_id;
    g_snap.catch_em_all_active = bw_pokebw_setting(POKEBW_SET_CATCH_EM_ALL) ? 1u : 0u;
    g_snap.active_save_slot = (uint8_t)bw_slots_current();

    if (GAME_DATA_GetSeason != NULL) {
        g_snap.season = (uint8_t)(GAME_DATA_GetSeason(gd) & 3u);
    } else if (g_enc_valid && g_enc_season >= 0) {
        g_snap.season = (uint8_t)(g_enc_season & 3);
    }

    if (ZONEDATA_GetPlaceNameId != NULL) {
        uint32_t place_id = ZONEDATA_GetPlaceNameId(zone_id);
        if (!gear_msg_line(MSG_PLACE_BANK, place_id, g_snap.zone_name, (int)sizeof(g_snap.zone_name))) {
            snprintf(g_snap.zone_name, sizeof(g_snap.zone_name), "Zone %u", (unsigned)zone_id);
        }
    } else {
        snprintf(g_snap.zone_name, sizeof(g_snap.zone_name), "Zone %u", (unsigned)zone_id);
    }

    /* Collect Party & Moves */
    party = (GAME_DATA_GetMyParty != NULL) ? GAME_DATA_GetMyParty(gd) : NULL;
    if (party != NULL && POKEPARTY_GetCount != NULL &&
        POKEPARTY_GetMember != NULL && POKE_TOOL_GetPartyValue != NULL) {
        n = POKEPARTY_GetCount(party);
        if (n > POKEBW_GEAR_MAX_PARTY) {
            n = POKEBW_GEAR_MAX_PARTY;
        }
        for (i = 0; i < n; i++) {
            void *pp = POKEPARTY_GetMember(party, i);
            PokebwGearMon *dst;
            uint16_t nick[48];
            uint32_t s;

            if (pp == NULL) {
                continue;
            }
            dst = &g_snap.party[g_snap.party_count++];
            memset(dst, 0, sizeof(*dst));

            dst->species = (uint16_t)POKE_TOOL_GetPartyValue(pp, P_SPECIES, 0u);
            dst->is_egg  = POKE_TOOL_GetPartyValue(pp, P_IS_EGG, 0u) != 0u ? 1u : 0u;
            dst->status  = (uint8_t)POKE_TOOL_GetPartyValue(pp, P_STATUS, 0u);
            dst->level   = (uint8_t)POKE_TOOL_GetPartyValue(pp, P_LEVEL, 0u);
            dst->hp      = (uint16_t)POKE_TOOL_GetPartyValue(pp, P_HP, 0u);
            dst->max_hp  = (uint16_t)POKE_TOOL_GetPartyValue(pp, P_MAX_HP, 0u);

            if (dst->is_egg) {
                snprintf(dst->name, sizeof(dst->name), "Egg");
                snprintf(dst->species_name, sizeof(dst->species_name), "Egg");
            } else {
                memset(nick, 0xFF, sizeof(nick));
                (void)POKE_TOOL_GetPartyValue(pp, P_NICKNAME, (uintptr_t)nick);
                units_to_utf8(nick, 48, dst->name, (int)sizeof(dst->name));
                if (!gear_msg_line(MSG_SPECIES_BANK, (uint32_t)dst->species,
                                   dst->species_name, (int)sizeof(dst->species_name))) {
                    snprintf(dst->species_name, sizeof(dst->species_name), "#%u", (unsigned)dst->species);
                }
                if (dst->name[0] == '\0') {
                    snprintf(dst->name, sizeof(dst->name), "%s", dst->species_name);
                }
            }

            for (s = 0; s < POKEBW_GEAR_MAX_MOVES && !dst->is_egg; s++) {
                uint16_t mid = (uint16_t)POKE_TOOL_GetPartyValue(pp, P_MOVE1 + s, 0u);
                PokebwGearMove *mv;

                if (mid == 0u) {
                    continue;
                }
                mv = &dst->moves[dst->move_count++];
                mv->id = mid;
                mv->pp = (uint8_t)POKE_TOOL_GetPartyValue(pp, P_PP1 + s, 0u);
                mv->max_pp = (uint8_t)POKE_TOOL_GetPartyValue(pp, P_MAXPP1 + s, 0u);
                if (WAZA_TOOL_GetParam != NULL) {
                    mv->type = (uint8_t)WAZA_TOOL_GetParam((uint32_t)mid, 0u);
                    mv->category = (uint8_t)WAZA_TOOL_GetParam((uint32_t)mid, 2u);
                } else {
                    mv->type = 17u;
                }
                if (!gear_msg_line(MSG_MOVE_BANK, (uint32_t)mid, mv->name, (int)sizeof(mv->name))) {
                    snprintf(mv->name, sizeof(mv->name), "Move %u", (unsigned)mid);
                }
            }
        }
    }

    /* Aggregate Route Encounters if this map has a loaded encounter table */
    zukan = (ZUKAN_SAVE_GetFromGameData != NULL) ? ZUKAN_SAVE_GetFromGameData(gd) : NULL;
    if (g_enc_valid && g_enc_zone == zone_id) {
        add_habitat_slots(&g_snap.habitats[0], g_enc_data.rate[0], g_enc_data.ground[0], k_ground_pct, 12, zukan);
        add_habitat_slots(&g_snap.habitats[1], g_enc_data.rate[1], g_enc_data.ground[1], k_ground_pct, 12, zukan);
        add_habitat_slots(&g_snap.habitats[2], g_enc_data.rate[2], g_enc_data.ground[2], k_ground_pct, 12, zukan);
        add_habitat_slots(&g_snap.habitats[3], g_enc_data.rate[3], g_enc_data.water[0],  k_water_pct,  5,  zukan);
        add_habitat_slots(&g_snap.habitats[4], g_enc_data.rate[4], g_enc_data.water[1],  k_water_pct,  5,  zukan);
        add_habitat_slots(&g_snap.habitats[5], g_enc_data.rate[5], g_enc_data.fishing[0],k_fish_pct,   5,  zukan);
        add_habitat_slots(&g_snap.habitats[6], g_enc_data.rate[6], g_enc_data.fishing[1],k_fish_pct,   5,  zukan);

        for (h = 0; h < POKEBW_GEAR_HABITATS; h++) {
            if (g_snap.habitats[h].count > 0u) {
                g_snap.has_encounters = 1u;
                break;
            }
        }
    }

#if defined(__wasm__)
    bw_host_gear_push(&g_snap, (unsigned)sizeof(g_snap));
#endif
}
