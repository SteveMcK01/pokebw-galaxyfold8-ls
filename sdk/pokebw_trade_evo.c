/*
 * sdk/pokebw_trade_evo.c -- Phase 1.1: Trade Evolution Fixes.
 *
 * Wraps SHINKA_CHECK_CheckEvolution at link time (-Wl,--wrap) so trade
 * evolutions can trigger on level-up when POKEBW_SET_TRADE_EVO_FIX is enabled:
 *   - Method 5 (pure trade): level >= 36
 *   - Method 6 (trade + held item): holding the required item on level-up
 *     (consumes the held item via SHINKA_DEMO_ClearHeldItem since *methodOut = 6)
 *   - Method 7 (Karrablast 588 / Shelmet 616): counterpart in party OR level >= 36
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>

#include "pokebw_settings_ids.h"

struct Party;
struct PokemonParam;

/* Matches struct EvolutionRow in aletheia/src/game/arm9/shinka_check.c. */
struct PokebwEvoRow {
    uint16_t method;
    uint16_t param;
    uint16_t species;
};

uint32_t __real_SHINKA_CHECK_CheckEvolution(struct Party *party,
                                            struct PokemonParam *pp,
                                            uint32_t mode,
                                            uintptr_t arg,
                                            uint8_t timezone,
                                            uint32_t *methodOut,
                                            uint16_t heap);

uint32_t POKE_TOOL_GetPartyValue(struct PokemonParam *pp, uint32_t paramId, uintptr_t arg);
uint32_t FILE_UTIL_GetItemParam(uint32_t item, uint32_t sel, uint32_t heap);
void     SHINKA_CHECK_LoadData(uint32_t species, struct PokebwEvoRow *rows);
uint32_t POKEPARTY_HasSpecies(struct Party *party, uint32_t species);

uint32_t __wrap_SHINKA_CHECK_CheckEvolution(struct Party *party,
                                            struct PokemonParam *pp,
                                            uint32_t mode,
                                            uintptr_t arg,
                                            uint8_t timezone,
                                            uint32_t *methodOut,
                                            uint16_t heap)
{
    uint32_t result = __real_SHINKA_CHECK_CheckEvolution(
        party, pp, mode, arg, timezone, methodOut, heap);
    struct PokebwEvoRow rows[7];
    uint16_t species;
    uint16_t heldItem;
    uint8_t level;
    uint8_t holdEffect;
    int i;

    if (result != 0 || mode != 0 || pp == NULL) {
        return result;
    }
    if (!bw_pokebw_setting(POKEBW_SET_TRADE_EVO_FIX)) {
        return 0;
    }
    /* Skip eggs. */
    if (POKE_TOOL_GetPartyValue(pp, 0x4C, 0) != 0) {
        return 0;
    }

    species = (uint16_t)POKE_TOOL_GetPartyValue(pp, 5, 0);
    if (species == 0) {
        return 0;
    }
    heldItem = (uint16_t)POKE_TOOL_GetPartyValue(pp, 6, 0);
    level = (uint8_t)POKE_TOOL_GetPartyValue(pp, 0x9E, 0);

    /* Respect Everstone (Kadabra 0x40 keeps vanilla's Everstone exception). */
    holdEffect = (uint8_t)FILE_UTIL_GetItemParam(heldItem, 1, (uint32_t)heap);
    if (species != 0x40 && holdEffect == 0x40) {
        return 0;
    }

    SHINKA_CHECK_LoadData(species, rows);

    for (i = 0; i < 7; i++) {
        const struct PokebwEvoRow *row = &rows[i];

        switch (row->method) {
        case 5:
            if (level >= 36) {
                result = row->species;
            }
            break;
        case 6:
            if (heldItem != 0 && heldItem == row->param) {
                result = row->species;
            }
            break;
        case 7: {
            uint32_t counterpart = 0;
            if (species == 588) {
                counterpart = 616;
            } else if (species == 616) {
                counterpart = 588;
            }
            if ((party != NULL && counterpart != 0 &&
                 POKEPARTY_HasSpecies(party, counterpart) == 1) ||
                level >= 36) {
                result = row->species;
            }
            break;
        }
        default:
            break;
        }

        if (result != 0) {
            if (methodOut != NULL) {
                *methodOut = row->method;
            }
            break;
        }
    }

    return result;
}
