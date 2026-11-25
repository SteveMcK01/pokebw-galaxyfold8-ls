/*
 * sdk/pokebw_sorter.c -- Phase 4.6: Gear PC Box & Bag Auto-Sorter.
 *
 * Polls sort requests from the host ImGui Gear tab via bw_host_sort_request_poll()
 * and sorts PC Boxes (by Dex #, Name, Level, Type, or Shiny First) or Bag pockets
 * (by Game Order, Name, Quantity, or Item ID) safely during overworld free-roam.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "pokebw_gear.h"
#include "pokebw_gear_state.h"
#include "pokebw_sorter.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_SORT_WEAK __attribute__((weak))
#else
#define POKEBW_SORT_WEAK
#endif

#define BOX_CAPACITY 30u
#define BOX_MON_SIZE 0x88u

#define PARAM_SPECIES        0x05u
#define PARAM_EXP            0x08u
#define PARAM_IS_EGG         0x4Cu
#define PARAM_FORM           0x6Fu
#define PARAM_NICKNAME       0x74u
#define PARAM_SPECIES_EXISTS 0xA9u
#define PARAM_TYPE1          0xAEu
#define PARAM_TYPE2          0xAFu

#define MSG_ITEM_BANK    54
#define MSG_SPECIES_BANK 70

struct PokebwBagSlot {
    uint16_t itemId;
    uint16_t quantity;
};

struct SortMonKey {
    uint32_t orig_idx;
    uint32_t exp;
    uint16_t species;
    uint16_t form;
    uint8_t  level;
    uint8_t  is_egg;
    uint8_t  is_shiny;
    uint8_t  type1;
    uint8_t  type2;
    char     name_upper[24];
};

struct SortBagKey {
    uint16_t itemId;
    uint16_t quantity;
    uint16_t orig_idx;
    char     name_upper[32];
};

extern void *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_SORT_WEAK;
extern void *GAME_DATA_GetBoxManager(const void *gd) POKEBW_SORT_WEAK;
extern void *GAME_DATA_GetMyItem(const void *gd) POKEBW_SORT_WEAK;
extern void *GAME_DATA_GetBagCursor(const void *gd) POKEBW_SORT_WEAK;

extern uint32_t BOX_SAVEDATA_GetCurrentBox(void *boxMgr) POKEBW_SORT_WEAK;
extern uint8_t  BOX_SAVEDATA_GetUsableBoxCount(void *boxMgr) POKEBW_SORT_WEAK;
extern void    *BOX_SAVEDATA_GetBoxPokemon(void *boxMgr, uint32_t box) POKEBW_SORT_WEAK;
extern void     BOX_SAVEDATA_ClearBoxPage(void *boxMons) POKEBW_SORT_WEAK;

extern uint32_t POKE_TOOL_GetBoxValue(void *box, uint32_t paramId, uintptr_t arg) POKEBW_SORT_WEAK;
extern uint32_t POKE_TOOL_CalcBoxLevel(void *box) POKEBW_SORT_WEAK;
extern uint32_t POKE_TOOL_IsBoxShiny(void *box) POKEBW_SORT_WEAK;

extern uint32_t MYITEM_SAVEDATA_GetPocket(void *bag, uint32_t pocket, void **slots,
                                          uint32_t *count) POKEBW_SORT_WEAK;
extern void MYITEM_SAVEDATA_ClearEmptyQuantities(void *slots, uint32_t count) POKEBW_SORT_WEAK;
extern void MYITEM_SAVEDATA_SortPocketById(void *slots, uint32_t count) POKEBW_SORT_WEAK;
extern void MYITEM_SAVEDATA_SortPocketByItemOrder(void *slots, uint32_t count) POKEBW_SORT_WEAK;
extern void MYITEM_SAVEDATA_SetFieldPocketCursor(void *cursor, int pocket,
                                                 int16_t scroll, int16_t position) POKEBW_SORT_WEAK;

static int g_pc_sort_mode = POKEBW_SORT_PC_DEX;
static int g_bag_sort_mode = POKEBW_SORT_BAG_NAME;

static void report_sort_result(int status, int count)
{
#if defined(__wasm__)
    bw_host_sort_request_result(status, count);
#else
    (void)status;
    (void)count;
#endif
}

static void fold_upper(char *dst, size_t cap, const char *src)
{
    size_t i = 0u;
    if (cap == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < cap) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - ('a' - 'A')) : (char)ch;
        i++;
    }
    dst[i] = '\0';
}

static int put_utf8_char(char *out, int at, int cap, uint32_t cp)
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

static void box_nick_to_utf8(const uint16_t *u, int n, char *out, int cap)
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
            at = put_utf8_char(out, at, cap, ' ');
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        at = put_utf8_char(out, at, cap, c);
    }
    out[at] = '\0';
}

static int cmp_pc_mon(const void *pa, const void *pb)
{
    const struct SortMonKey *a = (const struct SortMonKey *)pa;
    const struct SortMonKey *b = (const struct SortMonKey *)pb;

    /* Always keep Eggs sorted cleanly after hatched Pokémon. */
    if (a->is_egg != b->is_egg) {
        return (int)a->is_egg - (int)b->is_egg;
    }

    switch (g_pc_sort_mode) {
    case POKEBW_SORT_PC_NAME: {
        int c = strcmp(a->name_upper, b->name_upper);
        if (c != 0) return c;
        if (a->species != b->species) return (int)a->species - (int)b->species;
        if (a->level != b->level) return (int)b->level - (int)a->level;
        break;
    }
    case POKEBW_SORT_PC_LEVEL:
        if (a->level != b->level) return (int)b->level - (int)a->level;
        if (a->exp != b->exp) return a->exp > b->exp ? -1 : 1;
        if (a->species != b->species) return (int)a->species - (int)b->species;
        break;
    case POKEBW_SORT_PC_TYPE:
        if (a->type1 != b->type1) return (int)a->type1 - (int)b->type1;
        if (a->type2 != b->type2) return (int)a->type2 - (int)b->type2;
        if (a->species != b->species) return (int)a->species - (int)b->species;
        if (a->level != b->level) return (int)b->level - (int)a->level;
        break;
    case POKEBW_SORT_PC_SHINY:
        if (a->is_shiny != b->is_shiny) return (int)b->is_shiny - (int)a->is_shiny;
        if (a->species != b->species) return (int)a->species - (int)b->species;
        if (a->level != b->level) return (int)b->level - (int)a->level;
        break;
    case POKEBW_SORT_PC_DEX:
    default:
        if (a->species != b->species) return (int)a->species - (int)b->species;
        if (a->form != b->form) return (int)a->form - (int)b->form;
        if (a->level != b->level) return (int)b->level - (int)a->level;
        break;
    }

    return a->orig_idx < b->orig_idx ? -1 : (a->orig_idx > b->orig_idx ? 1 : 0);
}

static int cmp_bag_item(const void *pa, const void *pb)
{
    const struct SortBagKey *a = (const struct SortBagKey *)pa;
    const struct SortBagKey *b = (const struct SortBagKey *)pb;

    if (g_bag_sort_mode == POKEBW_SORT_BAG_QTY) {
        if (a->quantity != b->quantity) {
            return (int)b->quantity - (int)a->quantity;
        }
        {
            int c = strcmp(a->name_upper, b->name_upper);
            if (c != 0) return c;
        }
    } else {
        int c = strcmp(a->name_upper, b->name_upper);
        if (c != 0) return c;
        if (a->itemId != b->itemId) return (int)a->itemId - (int)b->itemId;
    }
    return a->orig_idx < b->orig_idx ? -1 : (a->orig_idx > b->orig_idx ? 1 : 0);
}

static void sort_pc_boxes(void *gd, int kind, int mode, int target)
{
    void *boxMgr;
    uint32_t usable;
    uint32_t start_box, end_box, b, s, idx, total = 0u;
    uint8_t *raw;
    struct SortMonKey *keys;

    if (GAME_DATA_GetBoxManager == NULL ||
        BOX_SAVEDATA_GetUsableBoxCount == NULL ||
        BOX_SAVEDATA_GetCurrentBox == NULL ||
        BOX_SAVEDATA_GetBoxPokemon == NULL ||
        BOX_SAVEDATA_ClearBoxPage == NULL ||
        POKE_TOOL_GetBoxValue == NULL ||
        POKE_TOOL_CalcBoxLevel == NULL) {
        report_sort_result(-1, 0);
        return;
    }

    boxMgr = GAME_DATA_GetBoxManager(gd);
    if (boxMgr == NULL) {
        report_sort_result(-1, 0);
        return;
    }

    usable = (uint32_t)BOX_SAVEDATA_GetUsableBoxCount(boxMgr);
    if (usable == 0u) {
        report_sort_result(0, 0);
        return;
    }
    if (usable > POKEBW_GEAR_MAX_BOXES) {
        usable = POKEBW_GEAR_MAX_BOXES;
    }

    if (kind == POKEBW_SORT_KIND_PC_BOX) {
        uint32_t box_idx = (target == 0xFF)
                               ? BOX_SAVEDATA_GetCurrentBox(boxMgr)
                               : (uint32_t)target;
        if (box_idx >= usable) {
            report_sort_result(-1, 0);
            return;
        }
        start_box = box_idx;
        end_box = box_idx;
    } else {
        start_box = 0u;
        end_box = usable - 1u;
    }

    /* Pass 1: count occupied slots across selected box range. */
    for (b = start_box; b <= end_box; b++) {
        uint8_t *boxMons = (uint8_t *)BOX_SAVEDATA_GetBoxPokemon(boxMgr, b);
        if (boxMons == NULL) {
            report_sort_result(-1, 0);
            return;
        }
        for (s = 0u; s < BOX_CAPACITY; s++) {
            void *mon = boxMons + s * BOX_MON_SIZE;
            if (POKE_TOOL_GetBoxValue(mon, PARAM_SPECIES_EXISTS, 0u) != 0u) {
                total++;
            }
        }
    }

    if (total == 0u) {
        report_sort_result(0, 0);
        return;
    }

    raw = (uint8_t *)malloc((size_t)total * BOX_MON_SIZE);
    keys = (struct SortMonKey *)calloc((size_t)total, sizeof(struct SortMonKey));
    if (raw == NULL || keys == NULL) {
        free(raw);
        free(keys);
        report_sort_result(-1, 0);
        return;
    }

    /* Pass 2: snapshot encrypted BoxPokemon bytes + extract sort metadata. */
    idx = 0u;
    for (b = start_box; b <= end_box; b++) {
        uint8_t *boxMons = (uint8_t *)BOX_SAVEDATA_GetBoxPokemon(boxMgr, b);
        for (s = 0u; s < BOX_CAPACITY; s++) {
            void *mon = boxMons + s * BOX_MON_SIZE;
            struct SortMonKey *k;
            char name_buf[24];

            if (POKE_TOOL_GetBoxValue(mon, PARAM_SPECIES_EXISTS, 0u) == 0u) {
                continue;
            }
            memcpy(raw + idx * BOX_MON_SIZE, mon, BOX_MON_SIZE);
            k = &keys[idx];
            k->orig_idx = idx;
            k->is_egg = POKE_TOOL_GetBoxValue(mon, PARAM_IS_EGG, 0u) != 0u ? 1u : 0u;
            k->species = (uint16_t)POKE_TOOL_GetBoxValue(mon, PARAM_SPECIES, 0u);
            k->form = (uint16_t)POKE_TOOL_GetBoxValue(mon, PARAM_FORM, 0u);
            k->exp = POKE_TOOL_GetBoxValue(mon, PARAM_EXP, 0u);
            k->level = k->is_egg ? 0u : (uint8_t)POKE_TOOL_CalcBoxLevel(mon);
            k->is_shiny = (!k->is_egg && POKE_TOOL_IsBoxShiny != NULL &&
                           POKE_TOOL_IsBoxShiny(mon) != 0u)
                              ? 1u
                              : 0u;
            if (k->is_egg) {
                k->type1 = 255u;
                k->type2 = 255u;
                snprintf(k->name_upper, sizeof(k->name_upper), "EGG");
            } else {
                uint16_t nick[24];
                k->type1 = (uint8_t)POKE_TOOL_GetBoxValue(mon, PARAM_TYPE1, 0u);
                k->type2 = (uint8_t)POKE_TOOL_GetBoxValue(mon, PARAM_TYPE2, 0u);
                memset(nick, 0xFF, sizeof(nick));
                name_buf[0] = '\0';
                (void)POKE_TOOL_GetBoxValue(mon, PARAM_NICKNAME, (uintptr_t)nick);
                box_nick_to_utf8(nick, 24, name_buf, (int)sizeof(name_buf));
                if (name_buf[0] == '\0') {
                    (void)pokebw_gear_msg_line(MSG_SPECIES_BANK, (uint32_t)k->species,
                                               name_buf, (int)sizeof(name_buf));
                }
                if (name_buf[0] == '\0') {
                    snprintf(name_buf, sizeof(name_buf), "#%03u", (unsigned)k->species);
                }
                fold_upper(k->name_upper, sizeof(k->name_upper), name_buf);
            }
            idx++;
        }
    }

    g_pc_sort_mode = mode;
    qsort(keys, (size_t)total, sizeof(struct SortMonKey), cmp_pc_mon);

    /* Pass 3: write back cleanly compacted boxes in sorted order. */
    for (b = start_box; b <= end_box; b++) {
        BOX_SAVEDATA_ClearBoxPage(BOX_SAVEDATA_GetBoxPokemon(boxMgr, b));
    }
    for (idx = 0u; idx < total; idx++) {
        uint32_t dst_box = start_box + (idx / BOX_CAPACITY);
        uint32_t dst_slot = idx % BOX_CAPACITY;
        uint8_t *dst_page = (uint8_t *)BOX_SAVEDATA_GetBoxPokemon(boxMgr, dst_box);
        memcpy(dst_page + dst_slot * BOX_MON_SIZE,
               raw + keys[idx].orig_idx * BOX_MON_SIZE,
               BOX_MON_SIZE);
    }

    free(keys);
    free(raw);
    pokebw_gear_mark_dirty();
    report_sort_result(1, (int)total);
}

static void sort_bag_pockets(void *gd, int kind, int mode, int target)
{
    void *myItem;
    void *cursor;
    uint32_t start_pk, end_pk, pk;
    int total_items = 0;

    if (GAME_DATA_GetMyItem == NULL ||
        MYITEM_SAVEDATA_GetPocket == NULL ||
        MYITEM_SAVEDATA_ClearEmptyQuantities == NULL ||
        MYITEM_SAVEDATA_SortPocketById == NULL ||
        MYITEM_SAVEDATA_SortPocketByItemOrder == NULL) {
        report_sort_result(-1, 0);
        return;
    }

    myItem = GAME_DATA_GetMyItem(gd);
    if (myItem == NULL) {
        report_sort_result(-1, 0);
        return;
    }

    if (kind == POKEBW_SORT_KIND_BAG_POCKET) {
        if (target < 0 || target >= POKEBW_GEAR_BAG_POCKETS) {
            report_sort_result(-1, 0);
            return;
        }
        start_pk = (uint32_t)target;
        end_pk = (uint32_t)target;
    } else {
        start_pk = 0u;
        end_pk = POKEBW_GEAR_BAG_POCKETS - 1u;
    }

    cursor = (GAME_DATA_GetBagCursor != NULL) ? GAME_DATA_GetBagCursor(gd) : NULL;

    for (pk = start_pk; pk <= end_pk; pk++) {
        void *slots_ptr = NULL;
        uint32_t slot_count = 0u;
        struct PokebwBagSlot *slots;
        uint32_t i, used = 0u;

        MYITEM_SAVEDATA_GetPocket(myItem, pk, &slots_ptr, &slot_count);
        if (slots_ptr == NULL || slot_count == 0u) {
            continue;
        }
        slots = (struct PokebwBagSlot *)slots_ptr;
        MYITEM_SAVEDATA_ClearEmptyQuantities(slots, slot_count);

        for (i = 0u; i < slot_count; i++) {
            if (slots[i].itemId != 0u && slots[i].quantity != 0u) {
                used++;
            }
        }
        if (used == 0u) {
            continue;
        }
        total_items += (int)used;

        if (mode == POKEBW_SORT_BAG_GAME) {
            MYITEM_SAVEDATA_SortPocketByItemOrder(slots, slot_count);
        } else if (mode == POKEBW_SORT_BAG_ID) {
            MYITEM_SAVEDATA_SortPocketById(slots, slot_count);
        } else {
            struct SortBagKey *entries =
                (struct SortBagKey *)calloc((size_t)used, sizeof(struct SortBagKey));
            uint32_t w = 0u;
            if (entries == NULL) {
                report_sort_result(-1, 0);
                return;
            }
            for (i = 0u; i < slot_count; i++) {
                char nm[32];
                if (slots[i].itemId == 0u || slots[i].quantity == 0u) {
                    continue;
                }
                entries[w].itemId = slots[i].itemId;
                entries[w].quantity = slots[i].quantity;
                entries[w].orig_idx = (uint16_t)w;
                nm[0] = '\0';
                (void)pokebw_gear_msg_line(MSG_ITEM_BANK, (uint32_t)entries[w].itemId,
                                           nm, (int)sizeof(nm));
                if (nm[0] == '\0') {
                    snprintf(nm, sizeof(nm), "ITEM %04u", (unsigned)entries[w].itemId);
                }
                fold_upper(entries[w].name_upper, sizeof(entries[w].name_upper), nm);
                w++;
            }
            g_bag_sort_mode = mode;
            qsort(entries, (size_t)w, sizeof(struct SortBagKey), cmp_bag_item);
            for (i = 0u; i < w; i++) {
                slots[i].itemId = entries[i].itemId;
                slots[i].quantity = entries[i].quantity;
            }
            if (w < slot_count) {
                memset(&slots[w], 0, (size_t)(slot_count - w) * sizeof(struct PokebwBagSlot));
            }
            free(entries);
        }

        if (cursor != NULL && MYITEM_SAVEDATA_SetFieldPocketCursor != NULL) {
            MYITEM_SAVEDATA_SetFieldPocketCursor(cursor, (int)pk, 0, 0);
        }
    }

    pokebw_gear_mark_dirty();
    report_sort_result(total_items > 0 ? 1 : 0, total_items);
}

void pokebw_sorter_on_field_tick(void *gameSystem, void *fieldMap)
{
    uint32_t req = 0u;
    int kind, mode, target;
    void *gd;

#if defined(__wasm__)
    req = bw_host_sort_request_poll();
#endif
    if (req == 0u) {
        return;
    }

    if (gameSystem == NULL || fieldMap == NULL || GAMESYSTEM_GetGameData == NULL ||
        bw_overlay_is_loaded(93) || bw_overlay_is_loaded(170) || bw_overlay_is_loaded(70)) {
        report_sort_result(-1, 0);
        return;
    }

    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        report_sort_result(-1, 0);
        return;
    }

    kind = (int)(req & 0xFu);
    mode = (int)((req >> 4) & 0xFu);
    target = (int)((req >> 8) & 0xFFu);

    if (kind == POKEBW_SORT_KIND_PC_BOX || kind == POKEBW_SORT_KIND_PC_ALL) {
        sort_pc_boxes(gd, kind, mode, target);
    } else if (kind == POKEBW_SORT_KIND_BAG_POCKET || kind == POKEBW_SORT_KIND_BAG_ALL) {
        sort_bag_pockets(gd, kind, mode, target);
    } else {
        report_sort_result(-1, 0);
    }
}
