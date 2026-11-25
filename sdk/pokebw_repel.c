/*
 * sdk/pokebw_repel.c -- Phase 4.3: "Use Another Repel?" Prompt.
 *
 * Backports the Pokemon Black 2 / White 2 Repel-expiry reuse prompt into
 * Pokemon Black 1 / White 1: when an active Repel wears off, checks the
 * player's Bag for another Repel (preferring the type that just wore off:
 * Max Repel / Super Repel / Repel, then falling back to any available Repel)
 * and opens a native BottomSysWin prompt + Yes/No choice menu.
 *
 * If the player says Yes, consumes one Repel, restarts the step cooldown,
 * plays the Repel spray SFX, and shows the remaining Bag count; if no Repels
 * remain or POKEBW_SET_REPEL_PROMPT is off, falls back seamlessly to the
 * vanilla script 0x2792 ("Repel's effect wore off...").
 *
 * Linked into eidos_boot only (CMakeLists.txt) via -Wl,--wrap on:
 *   MOVE_POKEMON_SAVE_SetCooldown
 *   MOVE_POKEMON_SAVE_TickCooldown
 *   ov010_SCRIPT_SYS_CreateScriptEventSimple
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_REPEL_WEAK __attribute__((weak))
#else
#define POKEBW_REPEL_WEAK
#endif

#define ITEM_SUPER_REPEL 0x004Cu
#define ITEM_MAX_REPEL   0x004Du
#define ITEM_REPEL       0x004Fu

#define REPEL_EXPIRE_SCRIPT_ID 0x2792u
#define REPEL_SE_DECIDE        0x0547u
#define REPEL_SE_SPRAY         0x07CDu

struct PokebwRepelWork {
    void    *gameSystem;
    void    *fieldMap;
    void    *msgBg;
    void    *sysWin;
    void    *yesNoMenu;
    void    *msgBuf;
    uint16_t heapId;
    uint16_t itemId;
    uint32_t qty;
    int      wait;
    int      paused;
};

/* ARM9 symbols */
extern void    *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_REPEL_WEAK;
extern void    *GAMESYSTEM_GetFieldMap(const void *gameSystem) POKEBW_REPEL_WEAK;
extern void    *GAME_DATA_GetMyItem(const void *gd) POKEBW_REPEL_WEAK;
extern void    *GAME_DATA_GetSaveControl(const void *gd) POKEBW_REPEL_WEAK;
extern void    *MOVE_POKEMON_SAVE_GetBlock(void *saveControl) POKEBW_REPEL_WEAK;
extern uint32_t MYITEM_SAVEDATA_GetItemQuantity(void *bag, uint16_t itemId, uint16_t heapId) POKEBW_REPEL_WEAK;
extern uint32_t MYITEM_SAVEDATA_RemoveItem(void *bag, uint16_t itemId, uint16_t quantity, uint16_t heapId) POKEBW_REPEL_WEAK;
extern uint32_t FILE_UTIL_GetItemParam(uint32_t item, uint32_t sel, uint32_t heap) POKEBW_REPEL_WEAK;
extern void    *GAME_EVENT_Create(void *gameSystem, void *parent,
                                   int (*func)(void *event, int *seq, void *work),
                                   uint32_t workSize) POKEBW_REPEL_WEAK;
extern void    *GAME_EVENT_GetWork(void *event) POKEBW_REPEL_WEAK;
extern void    *STRBUF_Create(uint32_t capacity, uint32_t heapId) POKEBW_REPEL_WEAK;
extern void     STRBUF_Delete(void *strbuf) POKEBW_REPEL_WEAK;
extern void     STRBUF_Clear(void *strbuf) POKEBW_REPEL_WEAK;
extern void     STRBUF_AppendChar(void *strbuf, uint16_t ch) POKEBW_REPEL_WEAK;
extern uint32_t KEY_GetTrg(void) POKEBW_REPEL_WEAK;
extern int      TOUCHPANEL_GetTrg(void) POKEBW_REPEL_WEAK;
extern void     GFL_USE_PlaySeDefaultVolume(unsigned int se) POKEBW_REPEL_WEAK;

/* ov010 symbols */
extern void ov010_FLDMMDL_SysPauseMove(void *mmdlSys) POKEBW_REPEL_WEAK;
extern void ov010_FLDMMDL_SysResumeMove(void *mmdlSys) POKEBW_REPEL_WEAK;

/* ov021 symbols */
extern void    *ov021_FIELDMAP_GetMsgBg(void *fieldMap) POKEBW_REPEL_WEAK;
extern void    *ov021_FIELDMAP_GetMmdlSys(void *fieldMap) POKEBW_REPEL_WEAK;
extern uint16_t ov021_FIELDMAP_GetHeapId(void *fieldMap) POKEBW_REPEL_WEAK;
extern void    *ov021_FIELD_MSGBG_CreateBottomSysWin(void *msgBg, void *msgData) POKEBW_REPEL_WEAK;
extern int      ov021_FIELD_MSGBG_DeleteSysWin(void *sysWin) POKEBW_REPEL_WEAK;
extern void     ov021_FIELD_MSGBG_ClearSysWin(void *sysWin) POKEBW_REPEL_WEAK;
extern void     ov021_FIELD_MSGBG_SysWinPrintStr(void *sysWin, uint32_t x, uint16_t y, void *str) POKEBW_REPEL_WEAK;
extern uint32_t ov021_FIELD_MSGBG_SysWinMain(void *sysWin) POKEBW_REPEL_WEAK;
extern void     ov021_FIELD_MSGBG_SysWinDrawCursor(void *sysWin) POKEBW_REPEL_WEAK;
extern void    *ov021_FIELD_MSGBG_CreateYesNoMenu(void *msgBg, uint32_t cursorPos) POKEBW_REPEL_WEAK;
extern int      ov021_FIELD_MSGBG_YesNoMenuMain(void *menu) POKEBW_REPEL_WEAK;
extern int      ov021_FIELD_MSGBG_DeleteMenuFunc(void *menu) POKEBW_REPEL_WEAK;

/* Linker __real_ targets */
extern void     __real_MOVE_POKEMON_SAVE_SetCooldown(void *pSave, uint8_t cooldown);
extern uint32_t __real_MOVE_POKEMON_SAVE_TickCooldown(void *pSave);
extern void    *__real_ov010_SCRIPT_SYS_CreateScriptEventSimple(
    void *fieldSys, uint16_t scriptId, void *target, int reserved);

static uint16_t g_last_repel_item = ITEM_REPEL;
static int      g_repel_just_expired = 0;

static const char *repel_item_name(uint16_t itemId)
{
    switch (itemId) {
    case ITEM_MAX_REPEL:   return "Max Repel";
    case ITEM_SUPER_REPEL: return "Super Repel";
    default:               return "Repel";
    }
}

static uint8_t repel_default_steps(uint16_t itemId)
{
    switch (itemId) {
    case ITEM_MAX_REPEL:   return 250u;
    case ITEM_SUPER_REPEL: return 200u;
    default:               return 100u;
    }
}

static void strbuf_set_ascii(void *strbuf, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (strbuf == NULL || STRBUF_Clear == NULL || STRBUF_AppendChar == NULL) {
        return;
    }
    STRBUF_Clear(strbuf);
    if (p == NULL) {
        return;
    }
    while (*p != '\0') {
        if (*p == '\n') {
            STRBUF_AppendChar(strbuf, 0xFFFEu);
        } else {
            STRBUF_AppendChar(strbuf, (uint16_t)(*p));
        }
        p++;
    }
}

static int repel_api_ready(void)
{
    return (GAMESYSTEM_GetGameData != NULL &&
            GAMESYSTEM_GetFieldMap != NULL &&
            GAME_DATA_GetMyItem != NULL &&
            GAME_DATA_GetSaveControl != NULL &&
            MOVE_POKEMON_SAVE_GetBlock != NULL &&
            MYITEM_SAVEDATA_GetItemQuantity != NULL &&
            MYITEM_SAVEDATA_RemoveItem != NULL &&
            FILE_UTIL_GetItemParam != NULL &&
            GAME_EVENT_Create != NULL &&
            GAME_EVENT_GetWork != NULL &&
            STRBUF_Create != NULL &&
            STRBUF_Delete != NULL &&
            STRBUF_Clear != NULL &&
            STRBUF_AppendChar != NULL &&
            KEY_GetTrg != NULL &&
            GFL_USE_PlaySeDefaultVolume != NULL &&
            ov010_FLDMMDL_SysPauseMove != NULL &&
            ov010_FLDMMDL_SysResumeMove != NULL &&
            ov021_FIELDMAP_GetMsgBg != NULL &&
            ov021_FIELDMAP_GetMmdlSys != NULL &&
            ov021_FIELDMAP_GetHeapId != NULL &&
            ov021_FIELD_MSGBG_CreateBottomSysWin != NULL &&
            ov021_FIELD_MSGBG_DeleteSysWin != NULL &&
            ov021_FIELD_MSGBG_ClearSysWin != NULL &&
            ov021_FIELD_MSGBG_SysWinPrintStr != NULL &&
            ov021_FIELD_MSGBG_SysWinMain != NULL &&
            ov021_FIELD_MSGBG_SysWinDrawCursor != NULL &&
            ov021_FIELD_MSGBG_CreateYesNoMenu != NULL &&
            ov021_FIELD_MSGBG_YesNoMenuMain != NULL &&
            ov021_FIELD_MSGBG_DeleteMenuFunc != NULL);
}

static uint16_t pick_available_repel(void *bag, uint16_t heapId, uint32_t *outQty)
{
    uint16_t order[3];
    size_t i;

    if (g_last_repel_item == ITEM_MAX_REPEL) {
        order[0] = ITEM_MAX_REPEL;
        order[1] = ITEM_SUPER_REPEL;
        order[2] = ITEM_REPEL;
    } else if (g_last_repel_item == ITEM_SUPER_REPEL) {
        order[0] = ITEM_SUPER_REPEL;
        order[1] = ITEM_MAX_REPEL;
        order[2] = ITEM_REPEL;
    } else {
        order[0] = ITEM_REPEL;
        order[1] = ITEM_SUPER_REPEL;
        order[2] = ITEM_MAX_REPEL;
    }

    for (i = 0u; i < 3u; i++) {
        uint32_t qty = MYITEM_SAVEDATA_GetItemQuantity(bag, order[i], heapId);
        if (qty > 0u) {
            if (outQty != NULL) {
                *outQty = qty;
            }
            return order[i];
        }
    }
    return 0u;
}

static int repel_prompt_event_main(void *event, int *seq, void *workPtr)
{
    struct PokebwRepelWork *w = (struct PokebwRepelWork *)workPtr;
    (void)event;

    if (w == NULL) {
        return 1;
    }

    switch (*seq) {
    case 0: {
        void *mmdlSys = ov021_FIELDMAP_GetMmdlSys(w->fieldMap);
        char line[128];

        if (mmdlSys != NULL) {
            ov010_FLDMMDL_SysPauseMove(mmdlSys);
            w->paused = 1;
        }
        w->msgBg = ov021_FIELDMAP_GetMsgBg(w->fieldMap);
        if (w->msgBg == NULL) {
            *seq = 6;
            break;
        }
        w->msgBuf = STRBUF_Create(0x100u, w->heapId);
        w->sysWin = ov021_FIELD_MSGBG_CreateBottomSysWin(w->msgBg, NULL);
        if (w->msgBuf == NULL || w->sysWin == NULL) {
            *seq = 6;
            break;
        }
        snprintf(line, sizeof(line), "Repel's effect wore off!\nUse another %s? (x%u)",
                 repel_item_name(w->itemId), (unsigned)w->qty);
        strbuf_set_ascii(w->msgBuf, line);
        ov021_FIELD_MSGBG_SysWinPrintStr(w->sysWin, 0u, 0u, w->msgBuf);
        *seq = 1;
        break;
    }
    case 1:
        if (ov021_FIELD_MSGBG_SysWinMain(w->sysWin) == 1u) {
            w->wait = 6;
            *seq = 2;
        }
        break;
    case 2:
        if (w->wait > 0) {
            w->wait--;
            break;
        }
        w->yesNoMenu = ov021_FIELD_MSGBG_CreateYesNoMenu(w->msgBg, 0u);
        if (w->yesNoMenu == NULL) {
            *seq = 6;
            break;
        }
        *seq = 3;
        break;
    case 3: {
        int ans = ov021_FIELD_MSGBG_YesNoMenuMain(w->yesNoMenu);
        if (ans == 0) {
            void *gd;
            void *bag;
            void *saveCtrl;
            void *repelSave;
            char line[128];

            ov021_FIELD_MSGBG_DeleteMenuFunc(w->yesNoMenu);
            w->yesNoMenu = NULL;

            gd = GAMESYSTEM_GetGameData(w->gameSystem);
            bag = (gd != NULL) ? GAME_DATA_GetMyItem(gd) : NULL;
            saveCtrl = (gd != NULL) ? GAME_DATA_GetSaveControl(gd) : NULL;
            repelSave = (saveCtrl != NULL) ? MOVE_POKEMON_SAVE_GetBlock(saveCtrl) : NULL;

            if (bag != NULL && repelSave != NULL &&
                MYITEM_SAVEDATA_RemoveItem(bag, w->itemId, 1u, w->heapId) != 0u) {
                uint32_t raw_steps = FILE_UTIL_GetItemParam(w->itemId, 2u, w->heapId);
                uint8_t steps = (raw_steps > 0u && raw_steps <= 255u)
                                    ? (uint8_t)raw_steps
                                    : repel_default_steps(w->itemId);
                uint32_t rem = (w->qty > 0u) ? (w->qty - 1u) : 0u;

                __real_MOVE_POKEMON_SAVE_SetCooldown(repelSave, steps);
                g_last_repel_item = w->itemId;
                GFL_USE_PlaySeDefaultVolume(REPEL_SE_SPRAY);

                ov021_FIELD_MSGBG_ClearSysWin(w->sysWin);
                if (rem > 0u) {
                    snprintf(line, sizeof(line), "Used %s!\n(%u left in Bag.)",
                             repel_item_name(w->itemId), (unsigned)rem);
                } else {
                    snprintf(line, sizeof(line), "Used %s!\n(None left in Bag.)",
                             repel_item_name(w->itemId));
                }
                strbuf_set_ascii(w->msgBuf, line);
                ov021_FIELD_MSGBG_SysWinPrintStr(w->sysWin, 0u, 0u, w->msgBuf);
                *seq = 4;
            } else {
                *seq = 6;
            }
        } else if (ans != 2) {
            ov021_FIELD_MSGBG_DeleteMenuFunc(w->yesNoMenu);
            w->yesNoMenu = NULL;
            *seq = 6;
        }
        break;
    }
    case 4:
        if (ov021_FIELD_MSGBG_SysWinMain(w->sysWin) == 1u) {
            w->wait = 10;
            *seq = 5;
        }
        break;
    case 5: {
        int touch = (TOUCHPANEL_GetTrg != NULL && TOUCHPANEL_GetTrg() != 0);
        ov021_FIELD_MSGBG_SysWinDrawCursor(w->sysWin);
        if (w->wait > 0) {
            w->wait--;
        } else if ((KEY_GetTrg() & 0x0003u) != 0u || touch) {
            GFL_USE_PlaySeDefaultVolume(REPEL_SE_DECIDE);
            *seq = 6;
        }
        break;
    }
    case 6:
    default: {
        if (w->yesNoMenu != NULL) {
            ov021_FIELD_MSGBG_DeleteMenuFunc(w->yesNoMenu);
            w->yesNoMenu = NULL;
        }
        if (w->sysWin != NULL) {
            ov021_FIELD_MSGBG_DeleteSysWin(w->sysWin);
            w->sysWin = NULL;
        }
        if (w->msgBuf != NULL) {
            STRBUF_Delete(w->msgBuf);
            w->msgBuf = NULL;
        }
        if (w->paused && w->fieldMap != NULL) {
            void *mmdlSys = ov021_FIELDMAP_GetMmdlSys(w->fieldMap);
            if (mmdlSys != NULL) {
                ov010_FLDMMDL_SysResumeMove(mmdlSys);
            }
            w->paused = 0;
        }
        return 1;
    }
    }
    return 0;
}

void __wrap_MOVE_POKEMON_SAVE_SetCooldown(void *pSave, uint8_t cooldown)
{
    __real_MOVE_POKEMON_SAVE_SetCooldown(pSave, cooldown);
    if (cooldown == 250u) {
        g_last_repel_item = ITEM_MAX_REPEL;
    } else if (cooldown == 200u) {
        g_last_repel_item = ITEM_SUPER_REPEL;
    } else if (cooldown == 100u) {
        g_last_repel_item = ITEM_REPEL;
    }
}

uint32_t __wrap_MOVE_POKEMON_SAVE_TickCooldown(void *pSave)
{
    uint32_t expired = __real_MOVE_POKEMON_SAVE_TickCooldown(pSave);
    if (expired != 0u) {
        g_repel_just_expired = 1;
    }
    return expired;
}

void *__wrap_ov010_SCRIPT_SYS_CreateScriptEventSimple(
    void *fieldSys, uint16_t scriptId, void *target, int reserved)
{
    int expired = g_repel_just_expired;
    g_repel_just_expired = 0;

    if (expired && scriptId == REPEL_EXPIRE_SCRIPT_ID && target == NULL &&
        bw_pokebw_setting(POKEBW_SET_REPEL_PROMPT) != 0 &&
        fieldSys != NULL && repel_api_ready()) {
        void *gd = GAMESYSTEM_GetGameData(fieldSys);
        void *fm = GAMESYSTEM_GetFieldMap(fieldSys);
        void *bag = (gd != NULL) ? GAME_DATA_GetMyItem(gd) : NULL;
        uint16_t heapId = (fm != NULL) ? ov021_FIELDMAP_GetHeapId(fm) : 0u;
        uint32_t qty = 0u;
        uint16_t itemId = (bag != NULL && fm != NULL)
                              ? pick_available_repel(bag, heapId, &qty)
                              : 0u;

        if (itemId != 0u) {
            void *ev = GAME_EVENT_Create(
                fieldSys, NULL, repel_prompt_event_main,
                (uint32_t)sizeof(struct PokebwRepelWork));
            if (ev != NULL) {
                struct PokebwRepelWork *w = (struct PokebwRepelWork *)GAME_EVENT_GetWork(ev);
                if (w != NULL) {
                    w->gameSystem = fieldSys;
                    w->fieldMap = fm;
                    w->heapId = heapId;
                    w->itemId = itemId;
                    w->qty = qty;
                }
                return ev;
            }
        }
    }
    return __real_ov010_SCRIPT_SYS_CreateScriptEventSimple(
        fieldSys, scriptId, target, reserved);
}
