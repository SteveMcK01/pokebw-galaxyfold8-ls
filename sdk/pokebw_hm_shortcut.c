/*
 * sdk/pokebw_hm_shortcut.c -- Phase 4.4: In-Game HM/TM Quick-Use Shortcut.
 *
 * Computes live overworld availability for all 8 Gen 5 field moves
 * (Cut, Fly, Surf, Strength, Waterfall, Dive, Flash, Dig) across the party
 * (respecting both learned moves and Phase 1.3 HM Freedom / Badge Bypass),
 * exposes them via Gear snapshots (sdk/pokebw_gear.c -> ImGui Gear tab),
 * and dispatches one-tap execution in the overworld (including opening the
 * native Town Map ov072 Fly selector for Fly).
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pokebw_hm_shortcut.h"
#include "pokebw_settings_ids.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_HMS_WEAK __attribute__((weak))
#else
#define POKEBW_HMS_WEAK
#endif

struct PokebwFieldSkillEntry {
    uint16_t moveId;
    uint16_t itemId;
    uint8_t  skillIndex;
};

static const struct PokebwFieldSkillEntry k_field_skills[POKEBW_GEAR_FIELD_MOVES] = {
    { 0x000Fu, 0x01A4u, 0u  }, /* Cut (HM01) */
    { 0x0013u, 0x01A5u, 4u  }, /* Fly (HM02) */
    { 0x0039u, 0x01A6u, 1u  }, /* Surf (HM03) */
    { 0x0046u, 0x01A7u, 3u  }, /* Strength (HM04) */
    { 0x007Fu, 0x01A8u, 2u  }, /* Waterfall (HM05) */
    { 0x0123u, 0x01A9u, 10u }, /* Dive (HM06) */
    { 0x0094u, 0x018Du, 5u  }, /* Flash (TM70) */
    { 0x005Bu, 0x0163u, 7u  }, /* Dig (TM28) */
};

/* ABI-matched views (ILP32 / wasm32 layout). */
struct PokebwFieldSkillInfo {
    uint16_t zoneId;      /* 0x00 */
    uint16_t usableMask;  /* 0x02 */
    uint32_t playerState; /* 0x04 */
    void    *gameSys;     /* 0x08 */
    void    *playerObj;   /* 0x0C */
    void    *fieldMap;    /* 0x10 */
};

struct PokebwSkillTarget {
    uint16_t id;        /* 0x00: party slot */
    uint16_t variant;   /* 0x02: skillIndex */
    uint32_t targetObj; /* 0x04: Fly destination zoneId or 0 */
};

struct PokebwTownMapParam {
    uint32_t viewMode;     /* 0x00: 0 = Fly, 1 = Browse */
    uint32_t result;       /* 0x04: 2 = destination selected */
    uint16_t zoneId;       /* 0x08: current zone in, destination zone out */
    int16_t  escapeZoneId; /* 0x0A */
    uint32_t selectedX;    /* 0x0C */
    uint32_t selectedZ;    /* 0x10 */
    void    *gameSystem;   /* 0x14 */
};

struct PokebwShortcutProc {
    uint32_t overlayId;    /* 0x00 */
    void    *procTemplate; /* 0x04 */
    void    *init;         /* 0x08 */
    void    *finish;       /* 0x0C */
    void    *start;        /* 0x10 */
    void    *exit;         /* 0x14 */
};

struct PokebwHmFlyWork {
    void    *gameSystem;
    void    *fieldMap;
    struct PokebwTownMapParam *tmParam;
    uint16_t partySlot;
    uint16_t destZone;
    uint32_t townMapResult;
    int      paused;
};

typedef char pokebw_hms_skill_info_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwFieldSkillInfo) == 0x14u) ? 1 : -1];
typedef char pokebw_hms_target_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwSkillTarget) == 0x08u) ? 1 : -1];
typedef char pokebw_hms_townmap_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwTownMapParam) == 0x18u) ? 1 : -1];
typedef char pokebw_hms_proc_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwShortcutProc) == 0x18u) ? 1 : -1];

/* ov010 proc table containing TownMap (index 7: overlayId=72). */
extern void *bw_dsref_ov010_d_021712CC POKEBW_HMS_WEAK;

/* ARM9 symbols */
extern void    *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_HMS_WEAK;
extern void    *GAMESYSTEM_GetEvent(const void *gameSystem) POKEBW_HMS_WEAK;
extern void    *GAMESYSTEM_GetGameComm(const void *gameSystem) POKEBW_HMS_WEAK;
extern void    *GAMESYSTEM_GetFieldMap(const void *gameSystem) POKEBW_HMS_WEAK;
extern void     GAMESYSTEM_CallProc(void *gameSystem, uint32_t overlayId, const void *procData, void *param) POKEBW_HMS_WEAK;
extern uint32_t GAMESYSTEM_GetProcStatus(const void *gameSystem) POKEBW_HMS_WEAK;
extern void    *GAME_DATA_GetMyParty(const void *gd) POKEBW_HMS_WEAK;
extern void    *GAME_DATA_GetMyItem(const void *gd) POKEBW_HMS_WEAK;
extern void    *GAME_DATA_GetMyPlayerWork(const void *gd) POKEBW_HMS_WEAK;
extern uint16_t GAME_DATA_GetPlayerZoneId(const void *playerWork) POKEBW_HMS_WEAK;
extern void    *GAME_DATA_GetEscapeLocation(void *gd) POKEBW_HMS_WEAK;
extern uint8_t  GAME_DATA_IsSaving(const void *gd) POKEBW_HMS_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_HMS_WEAK;
extern void    *POKEPARTY_GetMember(void *party, uint32_t pos) POKEBW_HMS_WEAK;
extern uint32_t POKE_TOOL_GetPartyValue(void *pp, uint32_t paramId, uintptr_t arg) POKEBW_HMS_WEAK;
extern uint32_t POKE_TOOL_CheckTmCompat(void *pp, int tmIndex) POKEBW_HMS_WEAK;
extern uint32_t FILE_UTIL_GetId(uint32_t machineId) POKEBW_HMS_WEAK;
extern uint32_t MYITEM_SAVEDATA_HasItem(void *bag, uint16_t itemId, uint16_t quantity, uint16_t heapId) POKEBW_HMS_WEAK;
extern uint32_t WIPE_SYS_EndCheck(void) POKEBW_HMS_WEAK;
extern uint32_t GAME_COMM_GetKind(const void *comm) POKEBW_HMS_WEAK;
extern uint32_t ZONEDATA_IsNormalFieldZone(uint32_t id) POKEBW_HMS_WEAK;
extern uint32_t ZONEDATA_IsNoGameEventZone(uint32_t id) POKEBW_HMS_WEAK;
extern void    *GFL_HEAP_Alloc(uint32_t heapId, uint32_t size, int clear,
                               const char *site, uint16_t tag) POKEBW_HMS_WEAK;
extern uint32_t GFL_HEAP_Free(void *p) POKEBW_HMS_WEAK;
extern void    *GAME_EVENT_Create(void *gameSystem, void *parent,
                                   int (*func)(void *event, int *seq, void *work),
                                   uint32_t workSize) POKEBW_HMS_WEAK;
extern void    *GAME_EVENT_GetWork(void *event) POKEBW_HMS_WEAK;
extern void     GAME_EVENT_CallEvent(void *parent, void *child) POKEBW_HMS_WEAK;
extern void     GAME_EVENT_ChangeEvent(void *now, void *next) POKEBW_HMS_WEAK;
extern void    *FIELD_STATUS_CreateFieldCloseEvent(void *gameSystem, void *fieldMap) POKEBW_HMS_WEAK;
extern void    *FIELD_STATUS_CreateFieldOpenEvent(void *gameSystem) POKEBW_HMS_WEAK;

/* ov010 symbols */
extern void  ov010_MOVE_POKEMON_InitSkillInfo(void *fieldMap, struct PokebwFieldSkillInfo *out) POKEBW_HMS_WEAK;
extern int   ov010_MOVE_POKEMON_IsSkillBlocked(int skillIndex, struct PokebwFieldSkillInfo *info) POKEBW_HMS_WEAK;
extern void  ov010_MOVE_POKEMON_InitSkillTarget(struct PokebwSkillTarget *target, uint16_t id, uint16_t variant, uint32_t targetObj) POKEBW_HMS_WEAK;
extern void *ov010_MOVE_POKEMON_CreateSkillEvent(int skillIndex, struct PokebwSkillTarget *target, struct PokebwFieldSkillInfo *info) POKEBW_HMS_WEAK;
extern void  ov010_FLDMMDL_SysPauseMove(void *mmdlSys) POKEBW_HMS_WEAK;
extern void  ov010_FLDMMDL_SysResumeMove(void *mmdlSys) POKEBW_HMS_WEAK;

/* ov021 symbols */
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_HMS_WEAK;
extern void    *ov021_FIELDMAP_GetSubscreen(void *fieldMap) POKEBW_HMS_WEAK;
extern void    *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) POKEBW_HMS_WEAK;
extern void    *ov021_FIELDMAP_GetPlaceName(void *fieldMap) POKEBW_HMS_WEAK;
extern void    *ov021_FIELDMAP_GetMmdlSys(void *fieldMap) POKEBW_HMS_WEAK;
extern uint16_t ov021_FIELDMAP_GetHeapId(void *fieldMap) POKEBW_HMS_WEAK;
extern void     ov021_FIELDMAP_SetBgMode(void) POKEBW_HMS_WEAK;
extern void     ov021_FIELDMAP_ResetBgAffineShowMsg(void *fieldMap) POKEBW_HMS_WEAK;
extern uint32_t ov021_FIELD_SUBSCREEN_GetMode(void *subscreen) POKEBW_HMS_WEAK;
extern uint32_t ov021_FIELD_PLAYER_GetStepPhase(void *player) POKEBW_HMS_WEAK;
extern void    *ov021_FIELD_PLACE_NAME_Cancel(void *placeName) POKEBW_HMS_WEAK;
extern void     ov021_FIELD_PLAYER_ResetGridHold(void *player, int a) POKEBW_HMS_WEAK;
extern void     ov021_FIELD_PLAYER_StopGridForcedMove(void *player) POKEBW_HMS_WEAK;
extern void     ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent(void *gameSystem, void *fieldMap) POKEBW_HMS_WEAK;
extern void    *ov021_EVENT_FIELDMAP_CONTROL_StartFadeOut(void *gameSystem, void *fieldMap, int effect, int skipWait) POKEBW_HMS_WEAK;
extern void    *ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn(void *gameSystem, void *fieldMap, int effect, int skipWait, int restoreBg, uint8_t seasonShown, uint8_t seasonTarget) POKEBW_HMS_WEAK;

static unsigned poll_hm_request(void)
{
#if defined(__wasm__)
    return bw_host_hm_request_poll();
#else
    return 0u;
#endif
}

static void report_hm_result(int status)
{
#if defined(__wasm__)
    bw_host_hm_request_result(status);
#else
    (void)status;
#endif
}

static const struct PokebwFieldSkillEntry *find_entry_by_skill(int skillIndex)
{
    int i;
    for (i = 0; i < POKEBW_GEAR_FIELD_MOVES; i++) {
        if ((int)k_field_skills[i].skillIndex == skillIndex) {
            return &k_field_skills[i];
        }
    }
    return NULL;
}

static uint8_t find_party_user_for_move(void *gd, uint16_t moveId, uint16_t itemId, uint16_t heapId)
{
    void *party;
    void *bag;
    uint32_t count, i;
    int hm_free;

    if (gd == NULL || GAME_DATA_GetMyParty == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0xFFu;
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL) {
        return 0xFFu;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }

    /* Pass 1: a non-Egg party member that actually knows the move */
    for (i = 0u; i < count; i++) {
        void *mon = POKEPARTY_GetMember(party, i);
        uint32_t s;
        if (mon == NULL || POKE_TOOL_GetPartyValue(mon, 0x4Cu, 0u) != 0u) {
            continue;
        }
        for (s = 0u; s < 4u; s++) {
            if (POKE_TOOL_GetPartyValue(mon, 0x36u + s, 0u) == (uint32_t)moveId) {
                return (uint8_t)i;
            }
        }
    }

    /* Pass 2: HM Without Teaching (if enabled) */
    hm_free = bw_pokebw_setting(POKEBW_SET_HM_FREEDOM);
    if (hm_free != 0 && POKE_TOOL_CheckTmCompat != NULL && FILE_UTIL_GetId != NULL) {
        uint32_t tmIdx = FILE_UTIL_GetId(itemId);
        int badge_ok = bw_pokebw_setting(POKEBW_SET_HM_NO_BADGES);

        if (tmIdx != 0xFFu) {
            bag = (!badge_ok && GAME_DATA_GetMyItem != NULL) ? GAME_DATA_GetMyItem(gd) : NULL;
            if (badge_ok || (bag != NULL && MYITEM_SAVEDATA_HasItem != NULL &&
                             MYITEM_SAVEDATA_HasItem(bag, itemId, 1u, heapId) != 0u)) {
                for (i = 0u; i < count; i++) {
                    void *mon = POKEPARTY_GetMember(party, i);
                    if (mon == NULL || POKE_TOOL_GetPartyValue(mon, 0x4Cu, 0u) != 0u) {
                        continue;
                    }
                    if (POKE_TOOL_CheckTmCompat(mon, (int)tmIdx) != 0u) {
                        return (uint8_t)i;
                    }
                }
            }
        }
    }

    return 0xFFu;
}

void pokebw_hm_shortcut_fill(
    void *gameSystem, void *fieldMap,
    PokebwGearFieldMove out[POKEBW_GEAR_FIELD_MOVES])
{
    void *gd;
    uint16_t heapId;
    struct PokebwFieldSkillInfo info;
    int have_info = 0;
    int i;

    if (out == NULL) {
        return;
    }
    for (i = 0; i < POKEBW_GEAR_FIELD_MOVES; i++) {
        out[i].skill_index = k_field_skills[i].skillIndex;
        out[i].party_slot = 0xFFu;
        out[i].usable = 0u;
        out[i].blocked_code = 5u;
    }

    if (gameSystem == NULL || fieldMap == NULL || GAMESYSTEM_GetGameData == NULL) {
        return;
    }
    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        return;
    }
    heapId = (ov021_FIELDMAP_GetHeapId != NULL) ? ov021_FIELDMAP_GetHeapId(fieldMap) : 4u;

    if (ov010_MOVE_POKEMON_InitSkillInfo != NULL && ov010_MOVE_POKEMON_IsSkillBlocked != NULL) {
        memset(&info, 0, sizeof(info));
        ov010_MOVE_POKEMON_InitSkillInfo(fieldMap, &info);
        have_info = 1;
    }

    for (i = 0; i < POKEBW_GEAR_FIELD_MOVES; i++) {
        const struct PokebwFieldSkillEntry *e = &k_field_skills[i];
        uint8_t slot = find_party_user_for_move(gd, e->moveId, e->itemId, heapId);

        out[i].party_slot = slot;
        if (slot >= 6u) {
            out[i].usable = 0u;
            out[i].blocked_code = 5u;
        } else if (have_info) {
            int blk = ov010_MOVE_POKEMON_IsSkillBlocked((int)e->skillIndex, &info);
            if (blk == 0) {
                out[i].usable = 1u;
                out[i].blocked_code = 0u;
            } else {
                out[i].usable = 0u;
                out[i].blocked_code = (blk > 4) ? 1u : (uint8_t)blk;
            }
        }
    }
}

static int hm_guardrails_ok(void *gameSystem, void *fieldMap, void **outGd)
{
    void *gd;
    void *party;
    void *comm;
    void *subscreen;
    void *player;

    if (gameSystem == NULL || fieldMap == NULL) {
        return 0;
    }
    if (GAMESYSTEM_GetGameData == NULL || GAMESYSTEM_GetEvent == NULL ||
        GAME_DATA_GetMyParty == NULL || POKEPARTY_GetCount == NULL ||
        ov010_MOVE_POKEMON_InitSkillInfo == NULL ||
        ov010_MOVE_POKEMON_IsSkillBlocked == NULL ||
        ov010_MOVE_POKEMON_InitSkillTarget == NULL ||
        ov010_MOVE_POKEMON_CreateSkillEvent == NULL) {
        return 0;
    }
    if (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0u) {
        return 0;
    }
    if (GAMESYSTEM_GetEvent(gameSystem) != NULL) {
        return 0;
    }
    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        return 0;
    }
    if (GAME_DATA_IsSaving != NULL && GAME_DATA_IsSaving(gd) != 0u) {
        return 0;
    }
    if (GAMESYSTEM_GetGameComm != NULL && GAME_COMM_GetKind != NULL) {
        comm = GAMESYSTEM_GetGameComm(gameSystem);
        if (comm != NULL && GAME_COMM_GetKind(comm) != 0u) {
            return 0;
        }
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL || POKEPARTY_GetCount(party) == 0u) {
        return 0;
    }
    if (ov021_FIELDMAP_GetZoneId != NULL) {
        uint16_t zoneId = ov021_FIELDMAP_GetZoneId(fieldMap);
        if (ZONEDATA_IsNormalFieldZone != NULL && ZONEDATA_IsNormalFieldZone(zoneId) == 0u) {
            return 0;
        }
        if (ZONEDATA_IsNoGameEventZone != NULL && ZONEDATA_IsNoGameEventZone(zoneId) != 0u) {
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
    return 1;
}

static const struct PokebwShortcutProc *find_townmap_proc(void)
{
    const struct PokebwShortcutProc *procs;

    if (&bw_dsref_ov010_d_021712CC == NULL || bw_dsref_ov010_d_021712CC == NULL) {
        return NULL;
    }
    procs = (const struct PokebwShortcutProc *)bw_dsref_ov010_d_021712CC;
    if (procs[7].overlayId == 72u && procs[7].procTemplate != NULL) {
        return &procs[7];
    }
    return NULL;
}

static int fly_apis_ready(void)
{
    return (find_townmap_proc() != NULL &&
            GFL_HEAP_Alloc != NULL &&
            GFL_HEAP_Free != NULL &&
            GAME_EVENT_Create != NULL &&
            GAME_EVENT_GetWork != NULL &&
            GAME_EVENT_CallEvent != NULL &&
            GAME_EVENT_ChangeEvent != NULL &&
            GAMESYSTEM_GetFieldMap != NULL &&
            GAMESYSTEM_CallProc != NULL &&
            GAMESYSTEM_GetProcStatus != NULL &&
            GAME_DATA_GetMyPlayerWork != NULL &&
            GAME_DATA_GetPlayerZoneId != NULL &&
            GAME_DATA_GetEscapeLocation != NULL &&
            FIELD_STATUS_CreateFieldCloseEvent != NULL &&
            FIELD_STATUS_CreateFieldOpenEvent != NULL &&
            ov010_FLDMMDL_SysPauseMove != NULL &&
            ov010_FLDMMDL_SysResumeMove != NULL &&
            ov021_FIELDMAP_GetMmdlSys != NULL &&
            ov021_FIELDMAP_SetBgMode != NULL &&
            ov021_FIELDMAP_ResetBgAffineShowMsg != NULL &&
            ov021_EVENT_FIELDMAP_CONTROL_StartFadeOut != NULL &&
            ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn != NULL);
}

static int pokebw_hm_fly_event_main(void *event, int *seq, void *workPtr)
{
    struct PokebwHmFlyWork *w = (struct PokebwHmFlyWork *)workPtr;

    if (w == NULL) {
        return 1;
    }

    switch (*seq) {
    case 0: {
        void *mmdlSys = ov021_FIELDMAP_GetMmdlSys(w->fieldMap);
        if (mmdlSys != NULL) {
            ov010_FLDMMDL_SysPauseMove(mmdlSys);
            w->paused = 1;
        }
        GAME_EVENT_CallEvent(event, ov021_EVENT_FIELDMAP_CONTROL_StartFadeOut(w->gameSystem, w->fieldMap, 0, 0));
        *seq = 1;
        break;
    }
    case 1:
        GAME_EVENT_CallEvent(event, FIELD_STATUS_CreateFieldCloseEvent(w->gameSystem, w->fieldMap));
        *seq = 2;
        break;
    case 2: {
        const struct PokebwShortcutProc *proc = find_townmap_proc();
        void *gd = GAMESYSTEM_GetGameData(w->gameSystem);
        void *playerWork = (gd != NULL) ? GAME_DATA_GetMyPlayerWork(gd) : NULL;
        void *esc = (gd != NULL) ? GAME_DATA_GetEscapeLocation(gd) : NULL;
        struct PokebwTownMapParam *tm;

        if (proc == NULL || playerWork == NULL) {
            w->townMapResult = 0u;
            GAME_EVENT_CallEvent(event, FIELD_STATUS_CreateFieldOpenEvent(w->gameSystem));
            *seq = 4;
            break;
        }
        tm = (struct PokebwTownMapParam *)GFL_HEAP_Alloc(
            4u, (uint32_t)sizeof(*tm), 1, "pokebw_fly", 0u);
        if (tm == NULL) {
            w->townMapResult = 0u;
            GAME_EVENT_CallEvent(event, FIELD_STATUS_CreateFieldOpenEvent(w->gameSystem));
            *seq = 4;
            break;
        }
        tm->viewMode = 0u; /* Fly picker mode */
        tm->gameSystem = w->gameSystem;
        tm->zoneId = GAME_DATA_GetPlayerZoneId(playerWork);
        if (esc != NULL) {
            int16_t escZone = 0;
            memcpy(&escZone, (const uint8_t *)esc + 4, sizeof(escZone));
            tm->escapeZoneId = escZone;
        }
        w->tmParam = tm;
        GAMESYSTEM_CallProc(w->gameSystem, proc->overlayId, proc->procTemplate, tm);
        *seq = 3;
        break;
    }
    case 3:
        if (GAMESYSTEM_GetProcStatus(w->gameSystem) != 0u) {
            break;
        }
        if (w->tmParam != NULL) {
            w->townMapResult = w->tmParam->result;
            w->destZone = w->tmParam->zoneId;
            GFL_HEAP_Free(w->tmParam);
            w->tmParam = NULL;
        }
        GAME_EVENT_CallEvent(event, FIELD_STATUS_CreateFieldOpenEvent(w->gameSystem));
        *seq = 4;
        break;
    case 4: {
        void *fm = GAMESYSTEM_GetFieldMap(w->gameSystem);
        w->fieldMap = fm;
        if (fm != NULL) {
            ov021_FIELDMAP_SetBgMode();
            ov021_FIELDMAP_ResetBgAffineShowMsg(fm);
            GAME_EVENT_CallEvent(event, ov021_EVENT_FIELDMAP_CONTROL_StartFadeIn(
                w->gameSystem, fm, 0, 0, 0, 0u, 0u));
        }
        *seq = 5;
        break;
    }
    case 5: {
        void *fm = GAMESYSTEM_GetFieldMap(w->gameSystem);
        if (w->paused && fm != NULL) {
            void *mmdlSys = ov021_FIELDMAP_GetMmdlSys(fm);
            if (mmdlSys != NULL) {
                ov010_FLDMMDL_SysResumeMove(mmdlSys);
            }
            w->paused = 0;
        }
        if (w->townMapResult == 2u && fm != NULL) {
            struct PokebwFieldSkillInfo info;
            struct PokebwSkillTarget target;
            void *skillEv;

            memset(&info, 0, sizeof(info));
            ov010_MOVE_POKEMON_InitSkillInfo(fm, &info);
            ov010_MOVE_POKEMON_InitSkillTarget(&target, w->partySlot, 4u, (uint32_t)w->destZone);
            skillEv = ov010_MOVE_POKEMON_CreateSkillEvent(4, &target, &info);
            if (skillEv != NULL) {
                GAME_EVENT_ChangeEvent(event, skillEv);
                return 0;
            }
        }
        return 1;
    }
    default:
        return 1;
    }
    return 0;
}

void *pokebw_hm_shortcut_try_run(void *gameSystem, void *fieldMap, int is_rail)
{
    unsigned req = poll_hm_request();
    const struct PokebwFieldSkillEntry *entry;
    void *gd = NULL;
    uint16_t heapId;
    uint8_t slot;
    struct PokebwFieldSkillInfo info;
    struct PokebwSkillTarget target;
    void *ev = NULL;
    int skillIndex;

    if (req == 0u) {
        return NULL;
    }
    skillIndex = (int)(req - 1u);
    entry = find_entry_by_skill(skillIndex);
    if (entry == NULL || !hm_guardrails_ok(gameSystem, fieldMap, &gd)) {
        report_hm_result(-1);
        return NULL;
    }

    heapId = (ov021_FIELDMAP_GetHeapId != NULL) ? ov021_FIELDMAP_GetHeapId(fieldMap) : 4u;
    slot = find_party_user_for_move(gd, entry->moveId, entry->itemId, heapId);
    if (slot >= 6u) {
        report_hm_result(-1);
        return NULL;
    }

    memset(&info, 0, sizeof(info));
    ov010_MOVE_POKEMON_InitSkillInfo(fieldMap, &info);
    if (ov010_MOVE_POKEMON_IsSkillBlocked(skillIndex, &info) != 0) {
        report_hm_result(-1);
        return NULL;
    }

    if (skillIndex == 4 && !fly_apis_ready()) {
        report_hm_result(-1);
        return NULL;
    }

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

    if (skillIndex == 4) {
        ev = GAME_EVENT_Create(
            gameSystem, NULL, pokebw_hm_fly_event_main,
            (uint32_t)sizeof(struct PokebwHmFlyWork));
        if (ev != NULL) {
            struct PokebwHmFlyWork *w = (struct PokebwHmFlyWork *)GAME_EVENT_GetWork(ev);
            if (w != NULL) {
                w->gameSystem = gameSystem;
                w->fieldMap = fieldMap;
                w->partySlot = (uint16_t)slot;
            }
        }
    } else {
        ov010_MOVE_POKEMON_InitSkillTarget(&target, (uint16_t)slot, (uint16_t)skillIndex, 0u);
        ev = ov010_MOVE_POKEMON_CreateSkillEvent(skillIndex, &target, &info);
    }

    report_hm_result(ev != NULL ? 1 : -1);
    return ev;
}
