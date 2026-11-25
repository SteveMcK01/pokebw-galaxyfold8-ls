/*
 * sdk/pokebw_ball_carousel.c -- Phase 4.5: Poké Ball Quick-Throw Carousel.
 *
 * Exposes live Bag Poké Ball inventory to the One-Screen battle command menu
 * (engine/ui/recipe_battle.c) and intercepts ov093_BTLV_CORE_PollActionMenu
 * via -Wl,--wrap so a single tap on the battle carousel submits a Bag Poké Ball
 * action cleanly without opening the Bag subscreen.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dsmem.h"
#include "overlay.h"
#include "pokebw_ball_carousel.h"
#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_BALL_WEAK __attribute__((weak))
#else
#define POKEBW_BALL_WEAK
#endif

/* Gen 5 Bag Constants (aletheia/src/game/arm9/myitem_savedata.c). */
#define BAG_POCKET_ITEMS        0u
#define BAG_ITEMS_SLOT_COUNT    310u
#define ITEM_PARAM_BATTLE_USE   0x0Fu
#define ITEM_BATTLE_USE_BALL    4u
#define BTLV_BAG_POCKET_BALLS   2u

/* Verified LP32 offsets inside ov093 structs (BtlvCore / BtlClient). */
#define CORE_OFF_CONTEXT        0x000u /* BtlMain * */
#define CORE_OFF_CLIENT         0x004u /* BtlClient * */
#define CORE_OFF_MONTABLE       0x008u /* BtlSide * */

#define CLIENT_OFF_MAIN         0x000u
#define CLIENT_OFF_SIDE         0x004u
#define CLIENT_OFF_ACTIVEMON    0x008u
#define CLIENT_OFF_ACTIONCURSOR 0x00Cu
#define CLIENT_OFF_SLOTINDEX    0x11Du /* u8 */
#define CLIENT_OFF_SLOTSDONE    0x11Fu /* u8 */
#define CLIENT_OFF_HEAPID       0x19Au /* u16 */
#define CLIENT_OFF_ISCOMM       0x1AAu /* u8 */
#define CLIENT_OFF_FLAGS        0x1B2u /* u8 */

/* ov093 battle engine symbols */
extern uint32_t ov093_BTL_MAIN_GetCompetitor(void *main) POKEBW_BALL_WEAK;
extern int      ov093_BTL_MAIN_CheckSetupStatusFlag(void *main, int mask) POKEBW_BALL_WEAK;
extern void    *ov093_BTL_MAIN_GetMyItem(void *main) POKEBW_BALL_WEAK;
extern void    *ov093_BTL_MAIN_GetMapMatrix(void *main) POKEBW_BALL_WEAK;
extern uint8_t  ov093_BTL_SIDE_CountLivingOfClient(void *side, int clientId) POKEBW_BALL_WEAK;
extern uint32_t ov093_BTL_POKEPARAM_HasSick(void *mon, uint32_t sick) POKEBW_BALL_WEAK;
extern uint32_t ov093_BTL_CLIENT_IsLoneFoeSemiInvulnerable(void *client) POKEBW_BALL_WEAK;
extern uint32_t ov093_BTL_CALC_GetItemParam(uint32_t itemId, uint32_t paramId) POKEBW_BALL_WEAK;
extern void     ov093_BTL_CLIENT_SetSlotFlag(void *client, uint32_t i, uint32_t flag) POKEBW_BALL_WEAK;
extern void     ov093_BTL_ACTION_SetItem(void *action, uint32_t itemId, uint32_t targetIndex,
                                         uint32_t itemParam) POKEBW_BALL_WEAK;
extern void     ov093_BTL_CLIENT_SetSlotLinkWord(void *client, uint32_t i, uint32_t v) POKEBW_BALL_WEAK;
extern void     ov093_BTLV_CORE_AbortActionMenu(void *core) POKEBW_BALL_WEAK;
extern void     ov093_BTL_CLIENT_SetSubFn(void *client, void *fn) POKEBW_BALL_WEAK;
extern uint32_t ov093_BTL_CLIENT_StepAdvanceSlot(void *client, int *st) POKEBW_BALL_WEAK;
extern int      __real_ov093_BTLV_CORE_PollActionMenu(void *core);

/* arm9 item / bag save symbols */
extern void    *MYITEM_SAVEDATA_GetSlot(void *bag, uint32_t pocket, uint32_t index) POKEBW_BALL_WEAK;
extern uint32_t MYITEM_SAVEDATA_HasItem(void *bag, uint16_t itemId, uint16_t quantity,
                                        uint16_t heapId) POKEBW_BALL_WEAK;
extern uint16_t MYITEM_SAVEDATA_GetLastBattleItem(const void *cursor) POKEBW_BALL_WEAK;
extern void     MYITEM_SAVEDATA_SetLastBattleItem(void *cursor, uint16_t lastBattleItem,
                                                  uint16_t lastBattleItemPocket) POKEBW_BALL_WEAK;

static uint16_t g_pending_ball_item = 0u;
static uint8_t  g_ball_item_cache[640]; /* 0 = unknown, 1 = non-ball, 2 = Poké Ball */

static int gptr_ok(const void *p)
{
    return p != NULL && bw_ds_game_ptr(p) && ((uintptr_t)p & 3u) == 0u;
}

static uint16_t rd16(const uint8_t *p, uint32_t off)
{
    uint16_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
}

static uint32_t rd32(const uint8_t *p, uint32_t off)
{
    uint32_t v;
    memcpy(&v, p + off, sizeof(v));
    return v;
}

static uint8_t *rdp(const uint8_t *p, uint32_t off)
{
    return (uint8_t *)(uintptr_t)rd32(p, off);
}

static int is_pokeball_item(uint16_t item_id)
{
    if (item_id == 0u || item_id >= (uint16_t)(sizeof(g_ball_item_cache))) {
        return 0;
    }
    if (g_ball_item_cache[item_id] == 0u) {
        uint32_t btype = 0u;
        if (ov093_BTL_CALC_GetItemParam != NULL) {
            btype = ov093_BTL_CALC_GetItemParam((uint32_t)item_id, ITEM_PARAM_BATTLE_USE);
        }
        g_ball_item_cache[item_id] = (btype == ITEM_BATTLE_USE_BALL) ? 2u : 1u;
    }
    return g_ball_item_cache[item_id] == 2u;
}

static int can_quick_throw(const uint8_t *core, uint8_t **out_main,
                           uint8_t **out_client, uint8_t **out_myitem)
{
    uint8_t *main_ptr;
    uint8_t *client;
    uint8_t *mon_table;
    uint8_t *active_mon;
    uint8_t *action_cursor;
    uint8_t *myitem;

    if (bw_pokebw_setting(POKEBW_SET_BALL_CAROUSEL) == 0 || !bw_overlay_is_loaded(93)) {
        return 0;
    }
    if (!gptr_ok(core) ||
        ov093_BTL_MAIN_GetCompetitor == NULL ||
        ov093_BTL_MAIN_CheckSetupStatusFlag == NULL ||
        ov093_BTL_MAIN_GetMyItem == NULL ||
        ov093_BTL_SIDE_CountLivingOfClient == NULL ||
        ov093_BTL_POKEPARAM_HasSick == NULL ||
        ov093_BTL_CLIENT_IsLoneFoeSemiInvulnerable == NULL ||
        MYITEM_SAVEDATA_GetSlot == NULL) {
        return 0;
    }

    main_ptr = rdp(core, CORE_OFF_CONTEXT);
    client = rdp(core, CORE_OFF_CLIENT);
    mon_table = rdp(core, CORE_OFF_MONTABLE);
    if (!gptr_ok(main_ptr) || !gptr_ok(client) || !gptr_ok(mon_table)) {
        return 0;
    }

    active_mon = rdp(client, CLIENT_OFF_ACTIVEMON);
    action_cursor = rdp(client, CLIENT_OFF_ACTIONCURSOR);
    if (!gptr_ok(active_mon) || !gptr_ok(action_cursor)) {
        return 0;
    }

    /* Gen 5 wild-battle & capture eligibility rules (mirrors BTLV_CORE_StartBagMenu):
     *   - Wild battle (competitor == 0) & non-comm (isComm == 0)
     *   - Acting Pokémon not blocked (Sky Drop / sick 0x21)
     *   - Only valid on the last active ally slot (slotIndex == slotsDone)
     *   - Catching not blocked (PC Boxes full, capture-block setup flag 0x40)
     *   - Exactly 1 living wild opponent
     *   - Wild opponent not semi-invulnerable (Fly / Dig / Dive / Shadow Force)
     */
    if (ov093_BTL_MAIN_GetCompetitor(main_ptr) != 0u) {
        return 0;
    }
    if (client[CLIENT_OFF_ISCOMM] != 0u) {
        return 0;
    }
    if (ov093_BTL_POKEPARAM_HasSick(active_mon, 0x21u) != 0u) {
        return 0;
    }
    if (client[CLIENT_OFF_SLOTINDEX] != client[CLIENT_OFF_SLOTSDONE]) {
        return 0;
    }
    if (ov093_BTL_MAIN_CheckSetupStatusFlag(main_ptr, 0x40) != 0) {
        return 0;
    }
    if (ov093_BTL_SIDE_CountLivingOfClient(mon_table, 1) != 1u) {
        return 0;
    }
    if (ov093_BTL_CLIENT_IsLoneFoeSemiInvulnerable(client) != 0u) {
        return 0;
    }

    myitem = (uint8_t *)ov093_BTL_MAIN_GetMyItem(main_ptr);
    if (!gptr_ok(myitem)) {
        return 0;
    }

    if (out_main != NULL) {
        *out_main = main_ptr;
    }
    if (out_client != NULL) {
        *out_client = client;
    }
    if (out_myitem != NULL) {
        *out_myitem = myitem;
    }
    return 1;
}

int pokebw_ball_carousel_query(void *core_ptr, PokebwBallEntry *out,
                               int max_entries, uint16_t *out_last_ball)
{
    uint8_t *main_ptr = NULL;
    uint8_t *client = NULL;
    uint8_t *myitem = NULL;
    int count = 0;
    uint32_t j;

    if (out_last_ball != NULL) {
        *out_last_ball = 0u;
    }
    if (out == NULL || max_entries <= 0) {
        return 0;
    }
    if (!can_quick_throw((const uint8_t *)core_ptr, &main_ptr, &client, &myitem)) {
        return 0;
    }

    for (j = 0u; j < BAG_ITEMS_SLOT_COUNT; j++) {
        uint16_t *slot = (uint16_t *)MYITEM_SAVEDATA_GetSlot(myitem, BAG_POCKET_ITEMS, j);
        uint16_t item_id;
        uint16_t qty;

        if (slot == NULL || !bw_ds_game_ptr(slot)) {
            break;
        }
        item_id = slot[0];
        qty = slot[1];
        if (item_id == 0u || qty == 0u) {
            continue;
        }
        if (is_pokeball_item(item_id)) {
            out[count].item_id = item_id;
            out[count].qty = qty;
            count++;
            if (count >= max_entries) {
                break;
            }
        }
    }

    if (count > 0 && out_last_ball != NULL &&
        ov093_BTL_MAIN_GetMapMatrix != NULL &&
        MYITEM_SAVEDATA_GetLastBattleItem != NULL) {
        void *cursor = ov093_BTL_MAIN_GetMapMatrix(main_ptr);
        if (gptr_ok(cursor)) {
            uint16_t last = MYITEM_SAVEDATA_GetLastBattleItem(cursor);
            if (is_pokeball_item(last)) {
                *out_last_ball = last;
            }
        }
    }
    return count;
}

void pokebw_ball_carousel_request_throw(uint16_t item_id)
{
    g_pending_ball_item = item_id;
}

void pokebw_ball_carousel_clear_pending(void)
{
    g_pending_ball_item = 0u;
}

int __wrap_ov093_BTLV_CORE_PollActionMenu(void *core_ptr)
{
    if (g_pending_ball_item != 0u) {
        uint16_t item_id = g_pending_ball_item;
        uint8_t *main_ptr = NULL;
        uint8_t *client = NULL;
        uint8_t *myitem = NULL;

        g_pending_ball_item = 0u;

        if (can_quick_throw((const uint8_t *)core_ptr, &main_ptr, &client, &myitem) &&
            is_pokeball_item(item_id) &&
            MYITEM_SAVEDATA_HasItem != NULL &&
            ov093_BTL_CLIENT_SetSlotFlag != NULL &&
            ov093_BTL_ACTION_SetItem != NULL &&
            ov093_BTL_CLIENT_SetSlotLinkWord != NULL &&
            ov093_BTLV_CORE_AbortActionMenu != NULL &&
            ov093_BTL_CLIENT_SetSubFn != NULL &&
            ov093_BTL_CLIENT_StepAdvanceSlot != NULL) {
            uint16_t heap_id = rd16(client, CLIENT_OFF_HEAPID);
            if (MYITEM_SAVEDATA_HasItem(myitem, item_id, 1u, heap_id) != 0u) {
                uint8_t slot_idx = client[CLIENT_OFF_SLOTINDEX];
                void *action_cursor = rdp(client, CLIENT_OFF_ACTIONCURSOR);

                ov093_BTL_CLIENT_SetSlotFlag(client, (uint32_t)slot_idx, 0u);
                ov093_BTL_ACTION_SetItem(action_cursor, (uint32_t)item_id, 0u, 0u);
                if (ov093_BTL_MAIN_GetMapMatrix != NULL &&
                    MYITEM_SAVEDATA_SetLastBattleItem != NULL) {
                    void *cursor = ov093_BTL_MAIN_GetMapMatrix(main_ptr);
                    if (gptr_ok(cursor)) {
                        MYITEM_SAVEDATA_SetLastBattleItem(
                            cursor, item_id, BTLV_BAG_POCKET_BALLS);
                    }
                }
                client[CLIENT_OFF_FLAGS] |= 4u;
                ov093_BTL_CLIENT_SetSlotLinkWord(client, (uint32_t)slot_idx, (uint32_t)item_id);
                ov093_BTLV_CORE_AbortActionMenu(core_ptr);
                ov093_BTL_CLIENT_SetSubFn(
                    client, (void *)(uintptr_t)ov093_BTL_CLIENT_StepAdvanceSlot);
                return 0;
            }
        }
    }
    return __real_ov093_BTLV_CORE_PollActionMenu(core_ptr);
}
