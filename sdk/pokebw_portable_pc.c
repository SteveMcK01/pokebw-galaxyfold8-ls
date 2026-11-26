/*
 * sdk/pokebw_portable_pc.c -- Phase 1.6: Portable PC Box Access / PC Anywhere.
 *
 * Wraps ov021_FIELD_EVENT_CHECK_RunGrid and ov021_FIELD_EVENT_CHECK_RunRail via
 * -Wl,--wrap in CMakeLists.txt so a one-shot request from the host ImGui menu
 * launches Unova's native BoxApp (ov170, rendered natively by eidos's
 * engine/ui/recipe_pc.c) while free-roaming on normal overworld maps.
 *
 * Guardrails block launching during active events/cutscenes, saves, active
 * comm/Union Room sessions, wipes/transitions, mid-step movement, Dowsing
 * Machine mode, non-normal field zones, or with an empty party.
 *
 * Controlled live by POKEBW_SET_PORTABLE_PC. Linked into eidos_boot only.
 */

#include <stddef.h>
#include <stdint.h>

#include "pokebw_area_map.h"
#include "pokebw_dex_lookup.h"
#include "pokebw_follower.h"
#include "pokebw_gear.h"
#include "pokebw_hm_shortcut.h"
#include "pokebw_settings_ids.h"
#include "pokebw_sorter.h"
#include "pokebw_wild.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_PC_WEAK __attribute__((weak))
#else
#define POKEBW_PC_WEAK
#endif

/* ABI-matched view of ov170/box2_param_types.h BoxParam (0x2C bytes on ILP32). */
struct PokebwBoxParam {
    void *gameData;       /* 0x00 */
    void *boxSave;        /* 0x04 */
    void *party;          /* 0x08 */
    void *myItem;         /* 0x0C */
    uint8_t *myStatus;    /* 0x10 */
    int linkMode;         /* 0x14 */
    void *config;         /* 0x18 */
    uint32_t natDex;      /* 0x1C */
    uint8_t *pickerBuffer;/* 0x20 */
    int mode;             /* 0x24 */
    uint16_t result;      /* 0x28 */
    uint8_t pickedBox;    /* 0x2A */
    uint8_t pickedSlot;   /* 0x2B */
};

typedef char pokebw_box_param_size_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwBoxParam) == 0x2Cu) ? 1 : -1];
typedef char pokebw_box_param_mode_check[
    (sizeof(void *) != 4 || offsetof(struct PokebwBoxParam, mode) == 0x24u) ? 1 : -1];

/* ov170 ProcData symbol resolved at link time from ov021. */
extern void *bw_dsref_ov021_d_021EF78C POKEBW_PC_WEAK;

/* ARM9 symbols */
extern void *GFL_HEAP_Alloc(uint32_t heapId, uint32_t size, int clear,
                            const char *site, uint16_t tag) POKEBW_PC_WEAK;
extern void *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_PC_WEAK;
extern void *GAMESYSTEM_GetEvent(const void *gameSystem) POKEBW_PC_WEAK;
extern void *GAMESYSTEM_GetGameComm(const void *gameSystem) POKEBW_PC_WEAK;
extern void *GAME_DATA_GetBoxManager(const void *gd) POKEBW_PC_WEAK;
extern void *GAME_DATA_GetMyParty(const void *gd) POKEBW_PC_WEAK;
extern void *GAME_DATA_GetMyItem(const void *gd) POKEBW_PC_WEAK;
extern void *GAME_DATA_GetMyStatus(void *gd) POKEBW_PC_WEAK;
extern void *GAME_DATA_GetSaveControl(const void *gd) POKEBW_PC_WEAK;
extern uint8_t GAME_DATA_IsSaving(const void *gd) POKEBW_PC_WEAK;
extern void *SYSTEM_DATA_GetConfig(void *saveControl) POKEBW_PC_WEAK;
extern void *ZUKAN_SAVE_GetFromGameData(void *gd) POKEBW_PC_WEAK;
extern uint32_t ZUKAN_SAVE_IsNationalDexEnabled(void *zukan) POKEBW_PC_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_PC_WEAK;
extern uint32_t WIPE_SYS_EndCheck(void) POKEBW_PC_WEAK;
extern uint32_t GAME_COMM_GetKind(const void *comm) POKEBW_PC_WEAK;
extern uint32_t ZONEDATA_IsNormalFieldZone(uint32_t id) POKEBW_PC_WEAK;
extern uint32_t ZONEDATA_IsNoGameEventZone(uint32_t id) POKEBW_PC_WEAK;
extern void *FIELD_STATUS_CreateSubProcCallbackEvent(
    void *gsys, void *fieldMap, uint32_t overlayId, void *procData,
    void *procWork, void (*callback)(void *), void *callbackWork) POKEBW_PC_WEAK;

/* ov021 symbols (prefixed by tools/ovlink.py) */
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_PC_WEAK;
extern void *ov021_FIELDMAP_GetSubscreen(void *fieldMap) POKEBW_PC_WEAK;
extern void *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) POKEBW_PC_WEAK;
extern void *ov021_FIELDMAP_GetPlaceName(void *fieldMap) POKEBW_PC_WEAK;
extern uint32_t ov021_FIELD_SUBSCREEN_GetMode(void *subscreen) POKEBW_PC_WEAK;
extern uint32_t ov021_FIELD_PLAYER_GetStepPhase(void *player) POKEBW_PC_WEAK;
extern void *ov021_FIELD_PLACE_NAME_Cancel(void *placeName) POKEBW_PC_WEAK;
extern void ov021_FIELD_PLAYER_ResetGridHold(void *player, int a) POKEBW_PC_WEAK;
extern void ov021_FIELD_PLAYER_StopGridForcedMove(void *player) POKEBW_PC_WEAK;
extern void ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent(void *gameSystem, void *fieldMap) POKEBW_PC_WEAK;

extern void *__real_ov021_FIELD_EVENT_CHECK_RunGrid(void *gameSystem, void *fieldMapArg);
extern void *__real_ov021_FIELD_EVENT_CHECK_RunRail(void *gameSystem, void *fieldMapArg);

static void report_pc_result(int status)
{
#if defined(__wasm__)
    bw_host_pc_request_result(status);
#else
    (void)status;
#endif
}

static unsigned poll_pc_request(void)
{
#if defined(__wasm__)
    return bw_host_pc_request_poll();
#else
    return 0u;
#endif
}

static int pc_guardrails_ok(void *gameSystem, void *fieldMap, void **outGd, void **outParty)
{
    void *gd;
    void *party;
    void *comm;
    void *subscreen;
    void *player;

    if (bw_pokebw_setting(POKEBW_SET_PORTABLE_PC) == 0) {
        return 0;
    }
    if (gameSystem == NULL || fieldMap == NULL) {
        return 0;
    }
    if (&bw_dsref_ov021_d_021EF78C == NULL || bw_dsref_ov021_d_021EF78C == NULL) {
        return 0;
    }
    if (GFL_HEAP_Alloc == NULL || FIELD_STATUS_CreateSubProcCallbackEvent == NULL ||
        GAMESYSTEM_GetGameData == NULL || GAMESYSTEM_GetEvent == NULL ||
        GAME_DATA_GetBoxManager == NULL || GAME_DATA_GetMyParty == NULL ||
        GAME_DATA_GetMyItem == NULL || GAME_DATA_GetMyStatus == NULL ||
        GAME_DATA_GetSaveControl == NULL || SYSTEM_DATA_GetConfig == NULL ||
        POKEPARTY_GetCount == NULL) {
        return 0;
    }
    if (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0) {
        return 0;
    }
    if (GAMESYSTEM_GetEvent(gameSystem) != NULL) {
        return 0;
    }
    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        return 0;
    }
    if (GAME_DATA_IsSaving != NULL && GAME_DATA_IsSaving(gd) != 0) {
        return 0;
    }
    if (GAMESYSTEM_GetGameComm != NULL && GAME_COMM_GetKind != NULL) {
        comm = GAMESYSTEM_GetGameComm(gameSystem);
        if (comm != NULL && GAME_COMM_GetKind(comm) != 0) {
            return 0;
        }
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL || POKEPARTY_GetCount(party) == 0) {
        return 0;
    }
    if (ov021_FIELDMAP_GetZoneId != NULL) {
        uint16_t zoneId = ov021_FIELDMAP_GetZoneId(fieldMap);
        if (ZONEDATA_IsNormalFieldZone != NULL && ZONEDATA_IsNormalFieldZone(zoneId) == 0) {
            return 0;
        }
        if (ZONEDATA_IsNoGameEventZone != NULL && ZONEDATA_IsNoGameEventZone(zoneId) != 0) {
            return 0;
        }
    }
    if (ov021_FIELDMAP_GetSubscreen != NULL && ov021_FIELD_SUBSCREEN_GetMode != NULL) {
        subscreen = ov021_FIELDMAP_GetSubscreen(fieldMap);
        if (subscreen != NULL && ov021_FIELD_SUBSCREEN_GetMode(subscreen) == 6u) {
            return 0;
        }
    }
    if (ov021_FIELDMAP_GetFieldPlayer != NULL && ov021_FIELD_PLAYER_GetStepPhase != NULL) {
        player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
        if (player != NULL && ov021_FIELD_PLAYER_GetStepPhase(player) != 0u) {
            return 0;
        }
    }

    *outGd = gd;
    *outParty = party;
    return 1;
}

static void *pokebw_try_open_pc(void *gameSystem, void *fieldMap, int is_rail)
{
    unsigned req = poll_pc_request();
    void *gd = NULL;
    void *party = NULL;
    void *saveControl;
    void *zukan;
    struct PokebwBoxParam *boxp;
    void *ev;
    int mode;

    if (req == 0u) {
        return NULL;
    }
    if (!pc_guardrails_ok(gameSystem, fieldMap, &gd, &party)) {
        report_pc_result(-1);
        return NULL;
    }

    saveControl = GAME_DATA_GetSaveControl(gd);
    if (saveControl == NULL) {
        report_pc_result(-1);
        return NULL;
    }

    boxp = (struct PokebwBoxParam *)GFL_HEAP_Alloc(4u, (uint32_t)sizeof(*boxp), 1, "pokebw_pc", 0u);
    if (boxp == NULL) {
        report_pc_result(-1);
        return NULL;
    }

    mode = (int)(req - 1u);
    if (mode < 0 || mode > 3) {
        mode = 2; /* Organize Boxes */
    }

    boxp->gameData = gd;
    boxp->boxSave = GAME_DATA_GetBoxManager(gd);
    boxp->party = party;
    boxp->myItem = GAME_DATA_GetMyItem(gd);
    boxp->myStatus = (uint8_t *)GAME_DATA_GetMyStatus(gd);
    boxp->linkMode = 0;
    boxp->config = SYSTEM_DATA_GetConfig(saveControl);
    zukan = (ZUKAN_SAVE_GetFromGameData != NULL) ? ZUKAN_SAVE_GetFromGameData(gd) : NULL;
    boxp->natDex = (zukan != NULL && ZUKAN_SAVE_IsNationalDexEnabled != NULL)
                       ? ZUKAN_SAVE_IsNationalDexEnabled(zukan)
                       : 0u;
    boxp->mode = mode;

    if (ov021_FIELDMAP_GetPlaceName != NULL && ov021_FIELD_PLACE_NAME_Cancel != NULL) {
        void *placeName = ov021_FIELDMAP_GetPlaceName(fieldMap);
        if (placeName != NULL) {
            (void)ov021_FIELD_PLACE_NAME_Cancel(placeName);
        }
    }
    if (!is_rail && ov021_FIELDMAP_GetFieldPlayer != NULL) {
        void *player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
        if (player != NULL) {
            if (ov021_FIELD_PLAYER_ResetGridHold != NULL) {
                ov021_FIELD_PLAYER_ResetGridHold(player, 1);
            }
            if (ov021_FIELD_PLAYER_StopGridForcedMove != NULL) {
                ov021_FIELD_PLAYER_StopGridForcedMove(player);
            }
        }
    }
    if (ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent != NULL) {
        ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent(gameSystem, fieldMap);
    }

    /*
     * callbackWork = boxp with callback = NULL: FIELD_STATUS_SubProcCallbackEvent
     * automatically frees callbackWork via GFL_HEAP_Free after BoxApp exits.
     */
    ev = FIELD_STATUS_CreateSubProcCallbackEvent(
        gameSystem, fieldMap, 170u, bw_dsref_ov021_d_021EF78C, boxp, NULL, boxp);
    report_pc_result(ev != NULL ? 1 : -1);
    return ev;
}

void *__wrap_ov021_FIELD_EVENT_CHECK_RunGrid(void *gameSystem, void *fieldMapArg)
{
    void *ev = __real_ov021_FIELD_EVENT_CHECK_RunGrid(gameSystem, fieldMapArg);
    if (ev != NULL) {
        return ev;
    }
    pokebw_gear_on_field_tick(gameSystem, fieldMapArg);
    pokebw_sorter_on_field_tick(gameSystem, fieldMapArg);
    pokebw_dex_lookup_on_field_tick(gameSystem, fieldMapArg);
    pokebw_area_map_on_field_tick(gameSystem, fieldMapArg);
    pokebw_follower_on_a_press(gameSystem, fieldMapArg);
    {
        void *wild_ev = pokebw_wild_check_encounter(gameSystem, fieldMapArg);
        if (wild_ev != NULL) {
            return wild_ev;
        }
    }
    {
        void *hm_ev = pokebw_hm_shortcut_try_run(gameSystem, fieldMapArg, 0);
        if (hm_ev != NULL) {
            return hm_ev;
        }
    }
    return pokebw_try_open_pc(gameSystem, fieldMapArg, 0);
}

void *__wrap_ov021_FIELD_EVENT_CHECK_RunRail(void *gameSystem, void *fieldMapArg)
{
    void *ev = __real_ov021_FIELD_EVENT_CHECK_RunRail(gameSystem, fieldMapArg);
    if (ev != NULL) {
        return ev;
    }
    pokebw_gear_on_field_tick(gameSystem, fieldMapArg);
    pokebw_sorter_on_field_tick(gameSystem, fieldMapArg);
    pokebw_dex_lookup_on_field_tick(gameSystem, fieldMapArg);
    pokebw_area_map_on_field_tick(gameSystem, fieldMapArg);
    pokebw_follower_on_a_press(gameSystem, fieldMapArg);
    {
        void *hm_ev = pokebw_hm_shortcut_try_run(gameSystem, fieldMapArg, 1);
        if (hm_ev != NULL) {
            return hm_ev;
        }
    }
    return pokebw_try_open_pc(gameSystem, fieldMapArg, 1);
}
