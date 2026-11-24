/*
 * sdk/pokebw_hm_free.c -- Phase 1.3: HM Without Teaching / HM Freedom.
 *
 * Wraps overworld field-script HM lookup (ov010_SCRCMD_PROC_FindPartyMonWithMove)
 * and the party context menu (ov091_PLIST_MENU_BuildCommandList +
 * ov091_POKELIST_DispatchMenuChoice) via -Wl,--wrap so Cut, Fly, Surf,
 * Strength, Waterfall, Dive, and Flash can be used from any compatible
 * non-Egg party member without occupying a moveslot.
 *
 * Controlled live by POKEBW_SET_HM_FREEDOM and POKEBW_SET_HM_NO_BADGES.
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pokebw_settings_ids.h"

struct PokemonParam;
struct MsgBank;
struct Strbuf;

#define POKEBW_HM_MENU_BASE  24
#define POKEBW_HM_COUNT      7
#define POKEBW_PLIST_MAX     8
#define POKEBW_MSG_ARCHIVE   0xCBu /* English/EUR move names (plist_menu.c) */

struct PokebwHmEntry {
    uint16_t moveId;
    uint16_t itemId;
    int      exitCode;
};

static const struct PokebwHmEntry k_hm_moves[POKEBW_HM_COUNT] = {
    { 0x000Fu, 0x01A4u, 0x0B }, /* Cut (HM01) */
    { 0x0013u, 0x01A5u, 0x0F }, /* Fly (HM02) */
    { 0x0039u, 0x01A6u, 0x0C }, /* Surf (HM03) */
    { 0x0046u, 0x01A7u, 0x0E }, /* Strength (HM04) */
    { 0x007Fu, 0x01A8u, 0x0D }, /* Waterfall (HM05) */
    { 0x0123u, 0x01A9u, 0x15 }, /* Dive (HM06) */
    { 0x0094u, 0x018Du, 0x10 }, /* Flash (TM70) */
};

/* ABI-matched views of ov091/plist_menu_types.h and ov091/pokelist_types.h. */
struct PokebwPlistMenuEntry {
    struct Strbuf *label; /* 0x00 */
    uint16_t color;       /* 0x04 */
    uint8_t pad06[2];
    int disabled;         /* 0x08 */
};

struct PokebwPlistMenu {
    uint8_t count;                                       /* 0x00 */
    uint8_t pad01;
    uint16_t ids[POKEBW_PLIST_MAX];                      /* 0x02 */
    uint8_t pad12[2];
    struct PokebwPlistMenuEntry entries[POKEBW_PLIST_MAX]; /* 0x14 */
    void *taskMenu;                                      /* 0x74 */
    uint8_t pad78[4];
    uint32_t windowOut;                                  /* 0x7C */
    void *windowRes;                                     /* 0x80 */
};

struct PokebwPokelistArg {
    void *party;                   /* 0x00 */
    void *bag;                     /* 0x04 */
    void *mailBox;                 /* 0x08 */
    uint8_t pad0C[0x10];
    uint8_t fieldMoveParam[0x14];  /* 0x1C */
    uint16_t itemUseParam;         /* 0x30 */
    uint8_t pad32[2];
    int mode;                      /* 0x34 */
    uint8_t pad38[4];
    int selectedIndex;             /* 0x3C */
    int exitCode;                  /* 0x40 */
};

struct PokebwPokelistWork {
    uint16_t heapId;               /* 0x000 */
    uint8_t pad02[2];
    void *updateTask;              /* 0x004 */
    int touchMode;                 /* 0x008 */
    uint8_t phase;                 /* 0x00C */
    uint8_t nextPhase;             /* 0x00D */
    uint8_t inputStage;            /* 0x00E */
    uint8_t command;               /* 0x00F */
    uint8_t pad10[0x1C];
    int selectedSlot;              /* 0x02C */
    int swapSourceSlot;            /* 0x030 */
    int healSourceSlot;            /* 0x034 */
    struct PokemonParam *selectedMon; /* 0x038 */
    int menuChoice;                /* 0x03C */
    uint8_t pad40[0x240];
    struct PokebwPokelistArg *arg; /* 0x280 */
};

typedef char pokebw_plist_menu_size_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwPlistMenu) == 0x84u) ? 1 : -1];
typedef char pokebw_pokelist_arg_exit_check[
    (sizeof(void *) != 4 || offsetof(struct PokebwPokelistArg, exitCode) == 0x40u) ? 1 : -1];
typedef char pokebw_pokelist_work_arg_check[
    (sizeof(void *) != 4 || offsetof(struct PokebwPokelistWork, arg) == 0x280u) ? 1 : -1];

/* ARM9 symbols */
extern uint32_t POKE_TOOL_GetPartyValue(struct PokemonParam *pp, uint32_t paramId, uintptr_t arg);
extern uint32_t POKE_TOOL_CheckTmCompat(struct PokemonParam *pp, int tmIndex);
extern uint32_t FILE_UTIL_GetId(uint32_t machineId);
extern uint32_t MYITEM_SAVEDATA_HasItem(void *bag, uint16_t itemId, uint16_t quantity, uint16_t heapId);
extern void    *GAME_DATA_GetMyParty(void *gameData);
extern void    *GAME_DATA_GetMyItem(const void *gameData);
extern int      POKEPARTY_GetCount(void *party);
extern struct PokemonParam *POKEPARTY_GetMember(void *party, int index);
extern struct MsgBank *MSGDATA_CreateFromArchive(uint32_t resident, uint32_t arcId, uint16_t fileId, uint32_t heapId);
extern struct Strbuf  *MSGDATA_AllocString(struct MsgBank *bank, uint32_t index);
extern uint32_t MSGDATA_Delete(struct MsgBank *bank);

/* ov010 symbols */
int      __real_ov010_SCRCMD_PROC_FindPartyMonWithMove(void *vm, void *cmdWork);
extern void     *ov010_SCRCMD_WORK_GetGameData(void *cmdWork);
extern uint16_t *ov010_SCRIPT_WORK_ReadWorkPointer(void *vm, void *cmdWork);
extern int       ov010_SCRIPT_WORK_ReadWorkValue(void *vm, void *cmdWork);
extern int       ov010_SCRCMD_PROC_CheckMonKnowsMove(struct PokemonParam *mon, int moveId);
extern int       ov010_MOVE_POKEMON_IsSkillBlocked(int skillIndex, void *info);

/* ov091 symbols */
void __real_ov091_PLIST_MENU_BuildCommandList(struct PokebwPokelistWork *work,
                                              struct PokebwPlistMenu *menu,
                                              int *items);
void __real_ov091_POKELIST_DispatchMenuChoice(struct PokebwPokelistWork *work);

static const struct PokebwHmEntry *find_hm_by_move(int moveId)
{
    int i;
    for (i = 0; i < POKEBW_HM_COUNT; i++) {
        if ((int)k_hm_moves[i].moveId == moveId) {
            return &k_hm_moves[i];
        }
    }
    return NULL;
}

static int mon_knows_move(struct PokemonParam *mon, uint16_t moveId)
{
    uint32_t slot;
    for (slot = 0; slot < 4u; slot++) {
        if (POKE_TOOL_GetPartyValue(mon, slot + 0x36u, 0) == moveId) {
            return 1;
        }
    }
    return 0;
}

static int mon_can_use_hm(void *bag, struct PokemonParam *mon, uint16_t itemId, uint16_t heapId)
{
    uint32_t tmIdx;

    if (mon == NULL || POKE_TOOL_GetPartyValue(mon, 0x4Cu, 0) != 0) {
        return 0; /* Empty or Egg */
    }
    if (!bw_pokebw_setting(POKEBW_SET_HM_NO_BADGES)) {
        if (bag == NULL || MYITEM_SAVEDATA_HasItem(bag, itemId, 1, heapId) == 0) {
            return 0;
        }
    }
    tmIdx = FILE_UTIL_GetId(itemId);
    if (tmIdx == 0xFFu) {
        return 0;
    }
    return POKE_TOOL_CheckTmCompat(mon, (int)tmIdx) != 0;
}

int __wrap_ov010_SCRCMD_PROC_FindPartyMonWithMove(void *vm, void *cmdWork)
{
    void *gameData;
    void *party;
    uint16_t *resultVar;
    int moveId;
    int partyCount;
    int i;
    const struct PokebwHmEntry *hm;
    void *bag;

    if (!bw_pokebw_setting(POKEBW_SET_HM_FREEDOM)) {
        return __real_ov010_SCRCMD_PROC_FindPartyMonWithMove(vm, cmdWork);
    }

    gameData = ov010_SCRCMD_WORK_GetGameData(cmdWork);
    party = GAME_DATA_GetMyParty(gameData);
    resultVar = ov010_SCRIPT_WORK_ReadWorkPointer(vm, cmdWork);
    moveId = ov010_SCRIPT_WORK_ReadWorkValue(vm, cmdWork);
    partyCount = POKEPARTY_GetCount(party);

    *resultVar = 6;
    for (i = 0; i < partyCount; i++) {
        if (ov010_SCRCMD_PROC_CheckMonKnowsMove(POKEPARTY_GetMember(party, i), moveId) == 1) {
            *resultVar = (uint16_t)i;
            return 0;
        }
    }

    hm = find_hm_by_move(moveId);
    if (hm != NULL) {
        bag = GAME_DATA_GetMyItem(gameData);
        for (i = 0; i < partyCount; i++) {
            struct PokemonParam *mon = POKEPARTY_GetMember(party, i);
            if (mon_can_use_hm(bag, mon, hm->itemId, 4)) {
                *resultVar = (uint16_t)i;
                break;
            }
        }
    }

    return 0;
}

static int items_request_field_moves(const int *items)
{
    int i;
    for (i = 0; i < POKEBW_PLIST_MAX && items[i] != 16; i++) {
        if (items[i] == 1) {
            return 1;
        }
    }
    return 0;
}

void __wrap_ov091_PLIST_MENU_BuildCommandList(struct PokebwPokelistWork *work,
                                              struct PokebwPlistMenu *menu,
                                              int *items)
{
    struct MsgBank *bank = NULL;
    int k;

    __real_ov091_PLIST_MENU_BuildCommandList(work, menu, items);

    if (!bw_pokebw_setting(POKEBW_SET_HM_FREEDOM) ||
        work == NULL || menu == NULL || items == NULL ||
        work->arg == NULL || work->selectedMon == NULL ||
        work->arg->mode != 0 || !items_request_field_moves(items)) {
        return;
    }

    for (k = 0; k < POKEBW_HM_COUNT && menu->count < POKEBW_PLIST_MAX; k++) {
        const struct PokebwHmEntry *hm = &k_hm_moves[k];
        uint8_t pos;

        if (mon_knows_move(work->selectedMon, hm->moveId)) {
            continue;
        }
        if (ov010_MOVE_POKEMON_IsSkillBlocked(hm->exitCode - 11, work->arg->fieldMoveParam) != 0) {
            continue;
        }
        if (!mon_can_use_hm(work->arg->bag, work->selectedMon, hm->itemId, work->heapId)) {
            continue;
        }
        if (bank == NULL) {
            bank = MSGDATA_CreateFromArchive(0, 2, POKEBW_MSG_ARCHIVE, work->heapId);
            if (bank == NULL) {
                break;
            }
        }

        pos = menu->count;
        if (pos > 0 && menu->ids[pos - 1] == 15u) {
            menu->ids[pos] = menu->ids[pos - 1];
            menu->entries[pos] = menu->entries[pos - 1];
            pos--;
        }
        menu->ids[pos] = (uint16_t)(POKEBW_HM_MENU_BASE + k);
        menu->entries[pos].label = MSGDATA_AllocString(bank, hm->moveId);
        menu->entries[pos].color = 0x35E0u;
        menu->entries[pos].disabled = 0;
        menu->count++;
    }

    if (bank != NULL) {
        MSGDATA_Delete(bank);
    }
}

void __wrap_ov091_POKELIST_DispatchMenuChoice(struct PokebwPokelistWork *work)
{
    if (work != NULL && work->arg != NULL &&
        work->menuChoice >= POKEBW_HM_MENU_BASE &&
        work->menuChoice < POKEBW_HM_MENU_BASE + POKEBW_HM_COUNT) {
        work->phase = 19;
        work->arg->selectedIndex = work->selectedSlot;
        work->arg->exitCode = k_hm_moves[work->menuChoice - POKEBW_HM_MENU_BASE].exitCode;
        return;
    }
    __real_ov091_POKELIST_DispatchMenuChoice(work);
}
