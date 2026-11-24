/*
 * sdk/pokebw_exp_share.c -- Phase 1.2: Modern EXP Share (Gen 6+ party-wide EXP).
 *
 * Wraps ov093_BTL_SERVER_FLOW_CalcPartyExpShares at link time (-Wl,--wrap) so
 * when POKEBW_SET_MODERN_EXP_SHARE is enabled:
 *   - Every non-fainted battle participant receives 100% of defeat EXP (no split).
 *   - Every non-fainted bench party member receives 50% of defeat EXP.
 *   - Gen 5 level scaling, trainer 1.5x, traded/foreign OT 1.5x/1.7x, Lucky Egg,
 *     Pass Power, and defeat EV yields are applied consistently.
 *
 * When disabled, delegates directly to the vanilla Gen 5 EXP calculation.
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pokebw_settings_ids.h"

struct BtlServer;
struct BtlMain;
struct BtlSide;
struct BtlSideSlot;
struct BtlPokeParam;
struct PokemonParam;
struct MyStatus;

/* Prefix of BtlServerFlow (mainModule at +sizeof(void*)). */
struct PokebwBtlServerFlow {
    struct BtlServer *server;
    struct BtlMain *mainModule;
    struct BtlSide *pokeCon;
};

/* Matches ExpShareRecord (12 bytes) in aletheia/src/game/ov093/btl_server_flow.c. */
struct PokebwExpShareRecord {
    uint32_t exp;
    uint8_t hasBonusFlag;
    uint8_t pad[7];
};

typedef char pokebw_exp_share_record_size_check[
    sizeof(struct PokebwExpShareRecord) == 12u ? 1 : -1];

#define POKEBW_ITEM_LUCKY_EGG 0xE7u
#define POKEBW_BTL_LEVEL      15

void __real_ov093_BTL_SERVER_FLOW_CalcPartyExpShares(
    struct PokebwBtlServerFlow *flow,
    struct BtlSideSlot *party,
    struct BtlPokeParam *defeatedPoke,
    struct PokebwExpShareRecord *expTable);

extern uint32_t ov093_BTL_CALC_CalcDefeatExp(struct BtlPokeParam *bpp);
extern uint32_t ov093_BTL_CALC_MulRatio(uint32_t value, uint32_t ratio);
extern uint8_t  ov093_BTL_SLOT_GetCount(struct BtlSideSlot *slot);
extern struct BtlPokeParam *ov093_BTL_SLOT_GetPokeParam(struct BtlSideSlot *slot, uint8_t index);
extern int      ov093_BTL_POKEPARAM_IsFainted(struct BtlPokeParam *pp);
extern uint8_t  ov093_BTL_POKEPARAM_GetPokeId(struct BtlPokeParam *pp);
extern uint8_t  ov093_BTL_POKEPARAM_GetFoughtCount(struct BtlPokeParam *pp);
extern uint8_t  ov093_BTL_POKEPARAM_GetFoughtPokeId(struct BtlPokeParam *pp, uint32_t slot);
extern int      ov093_BTL_POKEPARAM_GetValue(struct BtlPokeParam *pp, int valueId);
extern uint16_t ov093_BTL_POKEPARAM_GetItem(struct BtlPokeParam *pp);
extern struct PokemonParam *ov093_BTL_POKEPARAM_GetMon(struct BtlPokeParam *pp);
extern uint32_t ov093_BTL_MAIN_GetCompetitor(struct BtlMain *main);
extern struct MyStatus *ov093_BTL_MAIN_GetPlayerStatus(struct BtlMain *main);
extern uint32_t ov093_BTL_SERVER_FLOW_CalcLevelScaledExp(
    struct BtlPokeParam *poke, uint32_t baseExp, uint16_t gainerLevel, uint16_t defeatedLevel);
extern void     ov093_BTL_SERVER_FLOW_AddDefeatEffortValues(
    struct BtlPokeParam *gainer, struct BtlPokeParam *defeated, uint8_t *expRecord);

extern int      POKE_TOOL_IsOwnedByPlayer(struct PokemonParam *pp, struct MyStatus *myStatus);
extern uint32_t POKE_TOOL_GetPartyValue(struct PokemonParam *pp, uint32_t paramId, uintptr_t arg);
extern uint8_t  MYSTATUS_GetLanguage(const struct MyStatus *myStatus);
extern uint32_t GPOWER_ApplyExpRate(uint32_t exp);

void __wrap_ov093_BTL_SERVER_FLOW_CalcPartyExpShares(
    struct PokebwBtlServerFlow *flow,
    struct BtlSideSlot *party,
    struct BtlPokeParam *defeatedPoke,
    struct PokebwExpShareRecord *expTable)
{
    uint32_t scale;
    uint16_t n;
    uint8_t foughtCount;
    struct MyStatus *playerStatus;
    uint16_t defeatedLvl;
    uint16_t j;

    if (!bw_pokebw_setting(POKEBW_SET_MODERN_EXP_SHARE) ||
        flow == NULL || party == NULL || defeatedPoke == NULL || expTable == NULL) {
        __real_ov093_BTL_SERVER_FLOW_CalcPartyExpShares(flow, party, defeatedPoke, expTable);
        return;
    }

    memset(expTable, 0, 6u * sizeof(*expTable));

    scale = ov093_BTL_CALC_CalcDefeatExp(defeatedPoke);
    if (ov093_BTL_MAIN_GetCompetitor(flow->mainModule) == 1u) {
        scale = scale * 15u / 10u;
    }

    n = ov093_BTL_SLOT_GetCount(party);
    if (n > 6u) {
        n = 6u;
    }
    foughtCount = ov093_BTL_POKEPARAM_GetFoughtCount(defeatedPoke);
    playerStatus = ov093_BTL_MAIN_GetPlayerStatus(flow->mainModule);
    defeatedLvl = (uint16_t)ov093_BTL_POKEPARAM_GetValue(defeatedPoke, POKEBW_BTL_LEVEL);

    for (j = 0; j < n; j++) {
        struct BtlPokeParam *t = ov093_BTL_SLOT_GetPokeParam(party, (uint8_t)j);
        struct PokemonParam *mon;
        uint8_t id;
        uint8_t q;
        int participated = 0;
        uint32_t base;
        uint32_t exp;
        uint16_t gainerLvl;

        if (t == NULL || ov093_BTL_POKEPARAM_IsFainted(t) != 0) {
            continue;
        }

        id = ov093_BTL_POKEPARAM_GetPokeId(t);
        for (q = 0; q < foughtCount; q++) {
            if (id == ov093_BTL_POKEPARAM_GetFoughtPokeId(defeatedPoke, q)) {
                participated = 1;
                break;
            }
        }

        base = participated ? scale : (scale >> 1);
        if (base == 0u) {
            base = 1u;
        }

        gainerLvl = (uint16_t)ov093_BTL_POKEPARAM_GetValue(t, POKEBW_BTL_LEVEL);
        exp = ov093_BTL_SERVER_FLOW_CalcLevelScaledExp(t, base, gainerLvl, defeatedLvl);

        mon = ov093_BTL_POKEPARAM_GetMon(t);
        if (mon != NULL && playerStatus != NULL &&
            POKE_TOOL_IsOwnedByPlayer(mon, playerStatus) == 0) {
            uint32_t monLang = POKE_TOOL_GetPartyValue(mon, 12u, 0u);
            uint32_t ratio = (monLang != (uint32_t)MYSTATUS_GetLanguage(playerStatus))
                                 ? 0x1B33u
                                 : 0x1800u;
            exp = ov093_BTL_CALC_MulRatio(exp, ratio);
            expTable[j].hasBonusFlag = 1u;
        }
        if ((uint32_t)ov093_BTL_POKEPARAM_GetItem(t) == POKEBW_ITEM_LUCKY_EGG) {
            exp = ov093_BTL_CALC_MulRatio(exp, 0x1800u);
            expTable[j].hasBonusFlag = 1u;
        }

        exp = GPOWER_ApplyExpRate(exp);
        expTable[j].exp = exp;
        if (exp != 0u) {
            ov093_BTL_SERVER_FLOW_AddDefeatEffortValues(t, defeatedPoke, (uint8_t *)&expTable[j]);
        }
    }
}
