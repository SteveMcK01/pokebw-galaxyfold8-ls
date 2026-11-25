/*
 * sdk/pokebw_wild.c -- Phase 3.3: Native C Overworld Wild Pokémon Spawner.
 *
 * Spawns visible roaming wild Pokémon in tall grass, dark grass, caves, sand,
 * and surfing water from the zone's live EncountData tables (honoring season
 * overrides and Phase 1.4 Gotta Catch 'Em All!), renders live shiny sprites,
 * clips non-Flying water spawns by 30% so they swim naturally in water,
 * keeps off-screen spawns alive for a 7-second grace window, preserves surviving
 * spawns across battles and full-screen menus in the same zone, and starts wild
 * battles on contact or A-button press.
 *
 * Linked into eidos_boot only (CMakeLists.txt) via pokebw_follower / portable_pc
 * field ticks plus -Wl,--wrap=ov021_FLDMMDL_BLACT_AddRes and
 * -Wl,--wrap=ov021_FIELD_ENCOUNT_CreatePersonality.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pokebw_wild.h"
#include "pokebw_ow_sprites.h"
#include "pokebw_shiny.h"
#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_WILD_WEAK __attribute__((weak))
#else
#define POKEBW_WILD_WEAK
#endif

#define WILD_MAX_SLOTS              6u
#define WILD_OBJ_ID_BASE            0x00F6u /* F6..FB, clear of follower 0xFD */
#define TILE_FX                     0x10000
#define PLAYER_FORM_SURF            2
#define PLAYER_FORM_DIVE            3
#define TYPE_FLYING                 2u

#define WILD_SPAWN_INTERVAL         15
#define WILD_MOVE_FRAMES            20
#define WILD_DESPAWN_DIST_SQ        (10LL * 10LL)
#define WILD_SHINY_DIST_SQ          (13LL * 13LL)
#define WILD_HARD_DESPAWN_DIST_SQ   (26LL * 26LL)
#define WILD_OFFSCREEN_GRACE_FRAMES (7 * 60) /* 7 seconds at 60 fps */
#define WILD_LIFETIME_MIN           (35 * 60)
#define WILD_LIFETIME_MAX           (65 * 60)
#define WILD_COOLDOWN_FRAMES        90

struct PokebwWildVecFx32 {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct PokebwWildMmdlHeader {
    uint16_t objId;      /* 0x00 */
    uint16_t objCode;    /* 0x02 */
    uint16_t moveCode;   /* 0x04 */
    uint16_t eventType;  /* 0x06 */
    uint16_t eventFlag;  /* 0x08 */
    uint16_t eventId;    /* 0x0A */
    uint16_t dirHeader;  /* 0x0C */
    uint16_t param0;     /* 0x0E */
    uint16_t param1;     /* 0x10 */
    uint16_t param2;     /* 0x12 */
    int16_t  moveLimitX; /* 0x14 */
    int16_t  moveLimitZ; /* 0x16 */
    int32_t  posType;    /* 0x18 */
    uint16_t pos[4];     /* 0x1C */
};

struct PokebwWildMapHit {
    uint16_t kind;   /* 0x00 */
    uint16_t index;  /* 0x02 */
    uint16_t flags;  /* 0x04 */
    uint16_t pad06;  /* 0x06 */
    int32_t  attr;   /* 0x08 */
    int32_t  height; /* 0x0C */
};

struct PokebwWildEncountSlot {
    uint16_t species : 11;
    uint16_t form : 5;
    uint8_t  minLevel;
    uint8_t  maxLevel;
};

struct PokebwWildEncountData {
    uint8_t                     rate[7];        /* 0x00 */
    uint8_t                     effectKind : 7; /* 0x07 */
    uint8_t                     enabled : 1;
    struct PokebwWildEncountSlot ground[3][12];  /* 0x08 */
    struct PokebwWildEncountSlot water[2][5];    /* 0x98 */
    struct PokebwWildEncountSlot fishing[2][5];  /* 0xC0 */
};

struct PokebwWildFieldEncount {
    void *fieldMap;                            /* 0x00 */
    void *gameSystem;                          /* 0x04 */
    void *gameData;                            /* 0x08 */
    struct PokebwWildEncountData *encountData; /* 0x0C */
    void *effectEncount;                       /* 0x10 */
};

struct PokebwWildEncountMon {
    uint16_t species;      /* 0x00 */
    uint16_t heldItem;     /* 0x02 */
    uint8_t  level;        /* 0x04 */
    uint8_t  form;         /* 0x05 */
    uint8_t  shinyMode;    /* 0x06 */
    uint8_t  hiddenAbility;/* 0x07 */
    uint8_t  reserved08[8];/* 0x08 */
    uint8_t  forcedGender; /* 0x10 */
    uint8_t  reserved11[3];/* 0x11 */
};

struct PokebwWildEncountParam {
    void    *gameData;      /* 0x00 */
    void    *encountSave;   /* 0x04 */
    int32_t  encountType;   /* 0x08 */
    int32_t  triggerMode;   /* 0x0C */
    uint32_t slotTableKind; /* 0x10 */
    uint8_t  slotCount;     /* 0x14 */
    uint8_t  monCount;      /* 0x15 */
    uint16_t leadSpecies;   /* 0x16 */
    uint16_t leadItem;      /* 0x18 */
    uint8_t  leadIsEgg;     /* 0x1A */
    uint8_t  leadAbility;   /* 0x1B */
    uint8_t  leadGender;    /* 0x1C */
    uint8_t  leadNature;    /* 0x1D */
    uint8_t  leadLevel;     /* 0x1E */
    uint8_t  reserved1F;    /* 0x1F */
    uint32_t ownerId;       /* 0x20 */
    void    *myStatus;      /* 0x24 */
    uint32_t flags;         /* 0x28 */
};

typedef char pokebw_wild_vec_size_check[sizeof(struct PokebwWildVecFx32) == 12u ? 1 : -1];
typedef char pokebw_wild_hdr_size_check[sizeof(struct PokebwWildMmdlHeader) == 0x24u ? 1 : -1];
typedef char pokebw_wild_hit_size_check[sizeof(struct PokebwWildMapHit) == 16u ? 1 : -1];
typedef char pokebw_wild_enc_size_check[sizeof(struct PokebwWildEncountData) == 0xE8u ? 1 : -1];
typedef char pokebw_wild_mon_size_check[sizeof(struct PokebwWildEncountMon) == 0x14u ? 1 : -1];
typedef char pokebw_wild_param_size_check[
    (sizeof(void *) != 4 || sizeof(struct PokebwWildEncountParam) == 0x2Cu) ? 1 : -1];

struct WildSlot {
    int      active;
    void    *mmdl;
    uint16_t obj_id;
    uint16_t obj_code;
    uint16_t species;
    uint8_t  form;
    uint8_t  level;
    uint8_t  shiny;
    uint8_t  habitat; /* 0 ground/grass, 1 dark grass, 3 water */
    uint8_t  submerged;
    uint8_t  water_pool_slot;
    uint8_t  moving;
    uint8_t  move_step;
    uint16_t dir;
    int16_t  gx;
    int16_t  gz;
    int32_t  start_x;
    int32_t  start_y;
    int32_t  start_z;
    int32_t  target_x;
    int32_t  target_y;
    int32_t  target_z;
    struct PokebwWildVecFx32 pos;
    int      pause_frames;
    int      lifetime_frames;
    int      offscreen_frames;
    int      sparkle_frames;
};

struct SavedWildSlot {
    int      valid;
    uint16_t species;
    uint8_t  form;
    uint8_t  level;
    uint8_t  shiny;
    uint8_t  habitat;
    uint8_t  submerged;
    uint16_t dir;
    int16_t  gx;
    int16_t  gz;
    int32_t  y;
    int      lifetime_frames;
    int      offscreen_frames;
};

/* ARM9 symbols */
extern void    *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_WILD_WEAK;
extern void    *GAMESYSTEM_GetEvent(const void *gameSystem) POKEBW_WILD_WEAK;
extern int      GAMESYSTEM_IsEventActive(void *gameSystem) POKEBW_WILD_WEAK;
extern void    *GAMESYSTEM_GetGameComm(const void *gameSystem) POKEBW_WILD_WEAK;
extern void    *GAME_DATA_GetMyParty(const void *gd) POKEBW_WILD_WEAK;
extern uint8_t  GAME_DATA_IsSaving(const void *gd) POKEBW_WILD_WEAK;
extern uint8_t  GAME_DATA_GetSeason(const void *gd) POKEBW_WILD_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_WILD_WEAK;
extern uint32_t POKEPARTY_GetBattleReadyCount(const void *party) POKEBW_WILD_WEAK;
extern uint32_t WIPE_SYS_EndCheck(void) POKEBW_WILD_WEAK;
extern uint32_t GAME_COMM_GetKind(const void *comm) POKEBW_WILD_WEAK;
extern uint32_t ZONEDATA_IsNormalFieldZone(uint32_t id) POKEBW_WILD_WEAK;
extern uint32_t ZONEDATA_IsUnionRoom(uint32_t id) POKEBW_WILD_WEAK;
extern uint32_t ZONEDATA_IsNoGameEventZone(uint32_t id) POKEBW_WILD_WEAK;
extern uint32_t KEY_GetTrg(void) POKEBW_WILD_WEAK;
extern void    *BTL_SETUP_Create(uint32_t heap) POKEBW_WILD_WEAK;
extern uint32_t GFL_USE_GetRandomBelow(uint32_t limit) POKEBW_WILD_WEAK;
extern uint32_t POKE_TOOL_CreatePersonalRnd(uint32_t otId, int species, uint16_t form,
                                            int ratioMode, int shinyWanted, int mode) POKEBW_WILD_WEAK;
extern uint32_t POKE_TOOL_GetPersonalParam(uint16_t species, uint16_t form, uint32_t field) POKEBW_WILD_WEAK;
extern uint32_t POKE_PERSONAL_GetParam(const void *personal, int paramId) POKEBW_WILD_WEAK;
extern uint32_t POKE_PERSONAL_IsFixedGender(uint8_t ratio) POKEBW_WILD_WEAK;

/* ov010 symbols */
extern void *ov010_FLDMMDL_AddMmdl(void *sys, const struct PokebwWildMmdlHeader *header, int zoneId) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_DeleteMmdl(void *mmdl) POKEBW_WILD_WEAK;
extern void *ov010_FLDMMDL_SysGetFreeMmdl(void *sys) POKEBW_WILD_WEAK;
extern void *ov010_FLDMMDL_SysSearchObjId(void *sys, uint32_t objId) POKEBW_WILD_WEAK;
extern void *ov010_FLDMMDL_SysGetFieldMap(void *sys) POKEBW_WILD_WEAK;
extern int   ov010_FLDMMDL_SysCheckDrawInit(void *sys) POKEBW_WILD_WEAK;
extern int   ov010_FLDMMDL_SysIsDrawEnable(void *sys) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitVanish(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitFellowHit(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitHeightGetOff(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitAttrGetOff(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitZoneDeleteOff(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetMoveBitAttrEffectOff(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetMoveBitShadowVanish(void *mmdl, int flag) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetPos(void *mmdl, const struct PokebwWildVecFx32 *pos) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetDrawOffset(void *mmdl, const struct PokebwWildVecFx32 *offset) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetDirAll(void *mmdl, uint16_t dir) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetDrawStatus(void *mmdl, uint16_t drawStatus) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridNowX(void *mmdl, int16_t x) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridNowY(void *mmdl, int y) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridNowZ(void *mmdl, int z) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridOldX(void *mmdl, int16_t x) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridOldY(void *mmdl, int y) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetGridOldZ(void *mmdl, int z) POKEBW_WILD_WEAK;
extern void  ov010_FLDMMDL_SetZoneId(void *mmdl, uint16_t zoneId) POKEBW_WILD_WEAK;
extern void *ov010_EVENT_BATTLE_CreateScriptedBattle(
    void *gameSystem, void *battleSrc, void *btlSetup, int returnImmediately, int effectId) POKEBW_WILD_WEAK;

/* ov021 symbols */
extern void    *ov021_FIELDMAP_GetGameSystem(void *fieldMap) POKEBW_WILD_WEAK;
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) POKEBW_WILD_WEAK;
extern int      ov021_FIELDMAP_IsFieldPlayerCreated(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetG3dMapper(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetEncount(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetWeather(void *fieldMap) POKEBW_WILD_WEAK;
extern uint32_t ov021_FIELDMAP_GetMoveKind(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetSubscreen(void *fieldMap) POKEBW_WILD_WEAK;
extern void    *ov021_FIELDMAP_GetPlaceName(void *fieldMap) POKEBW_WILD_WEAK;
extern int      ov021_FIELDMAP_IsEventActive(void *fieldMap) POKEBW_WILD_WEAK;
extern int      ov021_FIELDMAP_IsMmdlDrawEnabled(int zoneId) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_PLAYER_GetPos(void *player, struct PokebwWildVecFx32 *out) POKEBW_WILD_WEAK;
extern uint16_t ov021_FIELD_PLAYER_GetDirDisp(void *player) POKEBW_WILD_WEAK;
extern int      ov021_FIELD_PLAYER_GetFormSlot(void *player) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_PLAYER_ResetGridHold(void *player, int a) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_PLAYER_StopGridForcedMove(void *player) POKEBW_WILD_WEAK;
extern uint32_t ov021_FIELD_SUBSCREEN_GetMode(void *subscreen) POKEBW_WILD_WEAK;
extern void    *ov021_FIELD_PLACE_NAME_Cancel(void *placeName) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent(void *gameSystem, void *fieldMap) POKEBW_WILD_WEAK;
extern int      ov021_FIELD_G3D_MAPPER_QueryNearestHit(
    void *mapper, const struct PokebwWildVecFx32 *pos, struct PokebwWildMapHit *outHit) POKEBW_WILD_WEAK;
extern int      ov021_MAP_ATTR_IsPresent(uint32_t attr) POKEBW_WILD_WEAK;
extern uint16_t ov021_MAP_ATTR_GetFlags(uint32_t attr) POKEBW_WILD_WEAK;
extern uint16_t ov021_MAP_ATTR_GetTerrainId(uint32_t attr) POKEBW_WILD_WEAK;
extern int      ov021_MAP_ATTR_IsAnyDoubleGrass(uint16_t terrainId) POKEBW_WILD_WEAK;
extern int      ov021_EFFECT_ENCOUNT_IsTileOccupied(
    void *encount, int16_t gridX, int16_t gridHeight, int16_t gridZ, int ignorePlayer) POKEBW_WILD_WEAK;
extern uint16_t ov021_WEATHER_GetWeatherId(void *weather) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_ENCOUNT_SetupParam(
    void *param, void *gameData, int encountType, int triggerMode, uint16_t weather) POKEBW_WILD_WEAK;
extern void     ov021_FIELD_ENCOUNT_SetupWildBattle(
    void *encount, void *param, void *btlSetup, uint16_t heapId, void *mons) POKEBW_WILD_WEAK;

extern uint32_t __real_ov021_FIELD_ENCOUNT_CreatePersonality(
    const void *param, void *personal, void *encMon);

static struct WildSlot      g_slots[WILD_MAX_SLOTS];
static struct SavedWildSlot g_saved_slots[WILD_MAX_SLOTS];
static uint16_t             g_saved_zone = 0xFFFFu;
static int                  g_has_saved_slots = 0;

static void    *g_sys = NULL;
static uint16_t g_zone = 0xFFFFu;
static int      g_spawn_timer = 0;
static int      g_post_battle_cooldown = 0;
static int      g_hidden = 0;
static uint32_t g_frame_phase = 0u;
static uint32_t g_rng_state = 0x5A17D15Cu;
static int      g_wild_force_shiny = 0;
static int      g_wild_from_overworld = 0;
static uint32_t g_water_pool_cursor = 0u;

static uint32_t wild_rng(void)
{
    uint32_t x = g_rng_state ^ (g_frame_phase * 0x9E3779B9u);
    if (x == 0u) {
        x = 0x13572468u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static uint32_t wild_rand_below(uint32_t limit)
{
    if (limit <= 1u) {
        return 0u;
    }
    if (GFL_USE_GetRandomBelow != NULL) {
        return GFL_USE_GetRandomBelow(limit);
    }
    return wild_rng() % limit;
}

static void dir_to_vec(uint16_t dir, int32_t *out_dx, int32_t *out_dz)
{
    switch (dir) {
    case 0u: *out_dx = 0;  *out_dz = -1; break;
    case 1u: *out_dx = 0;  *out_dz = 1;  break;
    case 2u: *out_dx = -1; *out_dz = 0;  break;
    case 3u: *out_dx = 1;  *out_dz = 0;  break;
    default: *out_dx = 0;  *out_dz = 1;  break;
    }
}

static void clear_saved_slots(void)
{
    memset(g_saved_slots, 0, sizeof(g_saved_slots));
    g_saved_zone = 0xFFFFu;
    g_has_saved_slots = 0;
}

static void slot_reset(struct WildSlot *s)
{
    memset(s, 0, sizeof(*s));
}

static void slot_despawn(void *sys, struct WildSlot *s)
{
    if (s->active && s->mmdl != NULL && sys != NULL &&
        ov010_FLDMMDL_SysSearchObjId != NULL && ov010_FLDMMDL_DeleteMmdl != NULL) {
        if (ov010_FLDMMDL_SysSearchObjId(sys, s->obj_id) == s->mmdl) {
            ov010_FLDMMDL_DeleteMmdl(s->mmdl);
        }
    }
    slot_reset(s);
}

static void wild_despawn_all(void *sys)
{
    uint32_t i;
    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        slot_despawn(sys, &g_slots[i]);
    }
    g_sys = NULL;
    g_zone = 0xFFFFu;
    g_spawn_timer = 0;
    g_hidden = 0;
}

static uint8_t should_submerge_water_species(uint16_t species, uint8_t form)
{
    if (species == 130u /* Gyarados */ ||
        species == 226u /* Mantine */ ||
        species == 458u /* Mantyke */) {
        return 1u;
    }
    if (POKE_TOOL_GetPersonalParam != NULL) {
        uint32_t t1 = POKE_TOOL_GetPersonalParam(species, (uint16_t)form, 6u);
        uint32_t t2 = POKE_TOOL_GetPersonalParam(species, (uint16_t)form, 7u);
        if (t1 == TYPE_FLYING || t2 == TYPE_FLYING) {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t pick_free_water_pool_slot(void)
{
    uint32_t step;
    for (step = 0u; step < POKEBW_OW_WATER_SLOTS; step++) {
        uint32_t cand = (g_water_pool_cursor + step) % POKEBW_OW_WATER_SLOTS;
        int in_use = 0;
        uint32_t i;
        for (i = 0u; i < WILD_MAX_SLOTS; i++) {
            if (g_slots[i].active && g_slots[i].submerged &&
                g_slots[i].water_pool_slot == (uint8_t)cand) {
                in_use = 1;
                break;
            }
        }
        if (!in_use) {
            g_water_pool_cursor = (cand + 1u) % POKEBW_OW_WATER_SLOTS;
            return cand;
        }
    }
    {
        uint32_t cand = g_water_pool_cursor % POKEBW_OW_WATER_SLOTS;
        g_water_pool_cursor = (cand + 1u) % POKEBW_OW_WATER_SLOTS;
        return cand;
    }
}

static uint16_t resolve_wild_objcode(
    uint16_t species, uint8_t form, int shiny, uint8_t submerged, uint32_t *out_pool_slot)
{
    if (submerged) {
        uint32_t ps = pick_free_water_pool_slot();
        uint16_t code = pokebw_ow_sprite_water_objcode(ps, species, form, shiny);
        if (code != 0u) {
            *out_pool_slot = ps;
            return code;
        }
    }
    *out_pool_slot = 0u;
    return pokebw_ow_sprite_objcode(species, form, shiny);
}

static void slot_sync_mmdl(struct WildSlot *s, uint16_t zoneId, int vanish)
{
    int16_t gx = (int16_t)(s->pos.x / TILE_FX);
    int16_t gy = (int16_t)(s->pos.y / TILE_FX);
    int16_t gz = (int16_t)(s->pos.z / TILE_FX);
    struct PokebwWildVecFx32 off = { 0, 0, 0 };

    if (ov010_FLDMMDL_SetStatusBitVanish != NULL) {
        ov010_FLDMMDL_SetStatusBitVanish(s->mmdl, vanish ? 1 : 0);
    }
    if (ov010_FLDMMDL_SetStatusBitFellowHit != NULL) {
        ov010_FLDMMDL_SetStatusBitFellowHit(s->mmdl, 0);
    }
    if (ov010_FLDMMDL_SetMoveBitShadowVanish != NULL) {
        ov010_FLDMMDL_SetMoveBitShadowVanish(s->mmdl, (vanish || s->submerged) ? 1 : 0);
    }
    if (ov010_FLDMMDL_SetZoneId != NULL) {
        ov010_FLDMMDL_SetZoneId(s->mmdl, zoneId);
    }
    if (ov010_FLDMMDL_SetPos != NULL) {
        ov010_FLDMMDL_SetPos(s->mmdl, &s->pos);
    }
    if (ov010_FLDMMDL_SetGridNowX != NULL) ov010_FLDMMDL_SetGridNowX(s->mmdl, gx);
    if (ov010_FLDMMDL_SetGridNowY != NULL) ov010_FLDMMDL_SetGridNowY(s->mmdl, gy);
    if (ov010_FLDMMDL_SetGridNowZ != NULL) ov010_FLDMMDL_SetGridNowZ(s->mmdl, gz);
    if (ov010_FLDMMDL_SetGridOldX != NULL) ov010_FLDMMDL_SetGridOldX(s->mmdl, s->gx);
    if (ov010_FLDMMDL_SetGridOldY != NULL) ov010_FLDMMDL_SetGridOldY(s->mmdl, gy);
    if (ov010_FLDMMDL_SetGridOldZ != NULL) ov010_FLDMMDL_SetGridOldZ(s->mmdl, s->gz);
    if (ov010_FLDMMDL_SetDirAll != NULL) {
        ov010_FLDMMDL_SetDirAll(s->mmdl, (s->dir <= 3u) ? s->dir : 1u);
    }
    if (ov010_FLDMMDL_SetDrawStatus != NULL) {
        ov010_FLDMMDL_SetDrawStatus(s->mmdl, s->moving ? 1u : 0u);
    }
    if (s->habitat == 3u) {
        uint32_t phase = (g_frame_phase + (uint32_t)s->obj_id * 13u) & 31u;
        int32_t bob = (phase < 16u) ? ((int32_t)phase * 0x00C0) : ((int32_t)(32u - phase) * 0x00C0);
        if (s->submerged) {
            /* Bottom 30% of sprite is clipped by add-res hook; gentle bob on surface. */
            off.y = bob;
        } else {
            /* Flying-type water Pokémon hover cleanly above water with shadow visible. */
            off.y = 0x2800 + bob;
        }
    }
    if (s->shiny && s->sparkle_frames > 0) {
        int f = s->sparkle_frames;
        int t = 12 - f;
        off.y += (int32_t)(t * f) * 0x0180;
    }
    if (ov010_FLDMMDL_SetDrawOffset != NULL) {
        ov010_FLDMMDL_SetDrawOffset(s->mmdl, &off);
    }
}

/* Returns -1 = unspawnable, 0 = normal grass/cave, 1 = dark grass, 3 = water. */
static int classify_tile_habitat(uint32_t attr)
{
    uint16_t flags, terrain;

    if (ov021_MAP_ATTR_IsPresent == NULL ||
        ov021_MAP_ATTR_GetFlags == NULL ||
        ov021_MAP_ATTR_GetTerrainId == NULL) {
        return -1;
    }
    if (ov021_MAP_ATTR_IsPresent(attr) == 0) {
        return -1;
    }
    flags = ov021_MAP_ATTR_GetFlags(attr);
    terrain = ov021_MAP_ATTR_GetTerrainId(attr);
    if ((flags & 4u) == 0u || terrain == 0x20u /* NO_ENCOUNTER */) {
        return -1;
    }
    if (flags & 2u) {
        if (terrain == 0x40u /* WATERFALL */) {
            return -1;
        }
        return 3;
    }
    if (flags & 1u) {
        return -1; /* Blocked */
    }
    if (ov021_MAP_ATTR_IsAnyDoubleGrass != NULL && ov021_MAP_ATTR_IsAnyDoubleGrass(terrain)) {
        return 1;
    }
    return 0;
}

static int query_tile_at(void *mapper, int16_t gx, int16_t gz, int32_t ref_y,
                         int32_t *out_y, int *out_hab)
{
    struct PokebwWildVecFx32 qpos;
    struct PokebwWildMapHit hit;
    int hab;

    if (mapper == NULL || ov021_FIELD_G3D_MAPPER_QueryNearestHit == NULL) {
        return 0;
    }
    qpos.x = (int32_t)gx * TILE_FX + (TILE_FX / 2);
    qpos.y = ref_y;
    qpos.z = (int32_t)gz * TILE_FX + (TILE_FX / 2);
    memset(&hit, 0, sizeof(hit));
    if (ov021_FIELD_G3D_MAPPER_QueryNearestHit(mapper, &qpos, &hit) != 1) {
        return 0;
    }
    if (hit.height - ref_y > 0x28000 || ref_y - hit.height > 0x28000) {
        return 0;
    }
    hab = classify_tile_habitat((uint32_t)hit.attr);
    if (hab < 0) {
        return 0;
    }
    *out_y = hit.height;
    *out_hab = hab;
    return 1;
}

static int roll_slot_for_habitat(
    const struct PokebwWildEncountData *enc, const void *gd, int hab,
    uint16_t *out_species, uint8_t *out_form, uint8_t *out_level, uint8_t *out_shiny)
{
    static const uint8_t k_ground_w[12] = { 20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1 };
    static const uint8_t k_water_w[5]   = { 60, 30, 5, 4, 1 };
    const struct PokebwWildEncountSlot *table;
    const uint8_t *weights;
    uint32_t count, i, roll, acc = 0u, pick = 0u;
    uint16_t sp;
    uint8_t fm, lv, lo, hi, sh;

    if (enc == NULL || !enc->enabled) {
        return 0;
    }
    if (hab < 0 || hab >= 7 || enc->rate[hab] == 0u) {
        return 0;
    }
    if (hab == 0 || hab == 1) {
        table = enc->ground[hab];
        weights = k_ground_w;
        count = 12u;
    } else if (hab == 3) {
        table = enc->water[0];
        weights = k_water_w;
        count = 5u;
    } else {
        return 0;
    }

    roll = wild_rand_below(100u);
    for (i = 0u; i < count; i++) {
        acc += weights[i];
        if (roll < acc) {
            pick = i;
            break;
        }
    }

    sp = (uint16_t)table[pick].species;
    if (sp == 0u || sp > POKEBW_OW_SPECIES_MAX) {
        return 0;
    }
    fm = (uint8_t)table[pick].form;
    if (fm == 0x1Fu) {
        if ((sp == 585u || sp == 586u) && gd != NULL && GAME_DATA_GetSeason != NULL) {
            fm = (uint8_t)(GAME_DATA_GetSeason(gd) & 3u);
        } else {
            fm = 0u;
        }
    }
    lo = table[pick].minLevel;
    hi = table[pick].maxLevel;
    if (lo == 0u) lo = 1u;
    if (hi < lo) hi = lo;
    lv = (uint8_t)(lo + wild_rand_below((uint32_t)(hi - lo) + 1u));
    sh = (uint8_t)pokebw_shiny_roll();
    if (pokebw_ow_sprite_objcode(sp, fm, sh) == 0u) {
        return 0;
    }

    *out_species = sp;
    *out_form = fm;
    *out_level = lv;
    *out_shiny = sh;
    return 1;
}

static int tile_blocked_by_wild(int self_idx, int16_t gx, int16_t gz)
{
    uint32_t i;
    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        if ((int)i == self_idx || !g_slots[i].active) {
            continue;
        }
        if (g_slots[i].gx == gx && g_slots[i].gz == gz) {
            return 1;
        }
        if (g_slots[i].moving) {
            int16_t tgx = (int16_t)(g_slots[i].target_x / TILE_FX);
            int16_t tgz = (int16_t)(g_slots[i].target_z / TILE_FX);
            if (tgx == gx && tgz == gz) {
                return 1;
            }
        }
    }
    return 0;
}

static int spawn_wild_in_slot(
    void *sys, uint16_t zoneId, int slot_idx,
    uint16_t species, uint8_t form, uint8_t level, uint8_t shiny,
    uint8_t hab, uint8_t submerged, uint16_t dir,
    int16_t cx, int16_t cz, int32_t ty,
    int lifetime_frames, int offscreen_frames)
{
    struct PokebwWildMmdlHeader hdr;
    uint32_t water_pool_slot = 0u;
    uint16_t obj_code;
    void *mmdl;

    if (slot_idx < 0 || slot_idx >= (int)WILD_MAX_SLOTS ||
        ov010_FLDMMDL_AddMmdl == NULL ||
        ov010_FLDMMDL_SysGetFreeMmdl == NULL ||
        ov010_FLDMMDL_SysSearchObjId == NULL) {
        return 0;
    }
    if (ov010_FLDMMDL_SysGetFreeMmdl(sys) == NULL) {
        return 0;
    }
    {
        uint16_t obj_id = (uint16_t)(WILD_OBJ_ID_BASE + (uint16_t)slot_idx);
        if (ov010_FLDMMDL_SysSearchObjId(sys, obj_id) != NULL) {
            return 0;
        }
    }

    obj_code = resolve_wild_objcode(species, form, shiny, submerged, &water_pool_slot);
    if (obj_code == 0u) {
        return 0;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.objId = (uint16_t)(WILD_OBJ_ID_BASE + (uint16_t)slot_idx);
    hdr.objCode = obj_code;
    hdr.moveCode = 0u;
    hdr.eventId = 0xFFFFu;
    hdr.dirHeader = (dir <= 3u) ? dir : 1u;
    hdr.posType = 0;
    hdr.pos[0] = (uint16_t)cx;
    hdr.pos[1] = (uint16_t)cz;
    memcpy(&hdr.pos[2], &ty, sizeof(ty));

    mmdl = ov010_FLDMMDL_AddMmdl(sys, &hdr, (int)zoneId);
    if (mmdl == NULL) {
        return 0;
    }
    if (ov010_FLDMMDL_SetStatusBitFellowHit != NULL) {
        ov010_FLDMMDL_SetStatusBitFellowHit(mmdl, 0);
    }
    if (ov010_FLDMMDL_SetStatusBitHeightGetOff != NULL) {
        ov010_FLDMMDL_SetStatusBitHeightGetOff(mmdl, 1);
    }
    if (ov010_FLDMMDL_SetStatusBitAttrGetOff != NULL) {
        ov010_FLDMMDL_SetStatusBitAttrGetOff(mmdl, 1);
    }
    if (ov010_FLDMMDL_SetMoveBitAttrEffectOff != NULL) {
        ov010_FLDMMDL_SetMoveBitAttrEffectOff(mmdl, 1);
    }
    if (ov010_FLDMMDL_SetStatusBitZoneDeleteOff != NULL) {
        ov010_FLDMMDL_SetStatusBitZoneDeleteOff(mmdl, 1);
    }

    {
        struct WildSlot *s = &g_slots[slot_idx];
        slot_reset(s);
        s->active = 1;
        s->mmdl = mmdl;
        s->obj_id = hdr.objId;
        s->obj_code = obj_code;
        s->species = species;
        s->form = form;
        s->level = level;
        s->shiny = shiny;
        s->habitat = hab;
        s->submerged = submerged;
        s->water_pool_slot = (uint8_t)water_pool_slot;
        s->dir = hdr.dirHeader;
        s->gx = cx;
        s->gz = cz;
        s->pos.x = (int32_t)cx * TILE_FX + (TILE_FX / 2);
        s->pos.y = ty;
        s->pos.z = (int32_t)cz * TILE_FX + (TILE_FX / 2);
        s->start_x = s->target_x = s->pos.x;
        s->start_y = s->target_y = s->pos.y;
        s->start_z = s->target_z = s->pos.z;
        s->pause_frames = 30 + (int)wild_rand_below(60u);
        s->lifetime_frames = lifetime_frames;
        s->offscreen_frames = offscreen_frames;
        s->sparkle_frames = shiny ? 12 : 0;
        slot_sync_mmdl(s, zoneId, 0);
    }
    return 1;
}

static void restore_saved_wilds(
    void *sys, void *mapper, void *enc_sys, uint16_t zoneId, int16_t px, int16_t pz)
{
    uint32_t i;

    if (!g_has_saved_slots) {
        return;
    }
    if (g_saved_zone != zoneId) {
        clear_saved_slots();
        return;
    }

    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        const struct SavedWildSlot *sv = &g_saved_slots[i];
        int32_t ty = sv->y;
        int hab = (int)sv->habitat;
        int16_t gy;

        if (!sv->valid) {
            continue;
        }
        if (sv->gx == px && sv->gz == pz) {
            continue;
        }
        if (query_tile_at(mapper, sv->gx, sv->gz, sv->y, &ty, &hab)) {
            if (hab != (int)sv->habitat) {
                continue;
            }
        }
        gy = (int16_t)(ty / TILE_FX);
        if (ov021_EFFECT_ENCOUNT_IsTileOccupied != NULL &&
            ov021_EFFECT_ENCOUNT_IsTileOccupied(enc_sys, sv->gx, gy, sv->gz, 0) != 0) {
            continue;
        }
        (void)spawn_wild_in_slot(
            sys, zoneId, (int)i,
            sv->species, sv->form, sv->level, sv->shiny,
            sv->habitat, sv->submerged, sv->dir,
            sv->gx, sv->gz, ty,
            sv->lifetime_frames, sv->offscreen_frames);
    }
    clear_saved_slots();
}

static void try_spawn_wild(
    void *sys, void *mapper, void *encount_sys,
    const struct PokebwWildEncountData *enc, const void *gd,
    uint16_t zoneId, const struct PokebwWildVecFx32 *ppos,
    int slot_idx)
{
    int16_t px = (int16_t)(ppos->x / TILE_FX);
    int16_t pz = (int16_t)(ppos->z / TILE_FX);
    int attempt;

    for (attempt = 0; attempt < 16; attempt++) {
        int ox = (int)wild_rand_below(17u) - 8;
        int oz = (int)wild_rand_below(17u) - 8;
        int dist_sq = ox * ox + oz * oz;
        int16_t cx, cz, gy;
        int32_t ty = 0;
        int hab = -1;
        int too_close = 0;
        uint32_t k;
        uint16_t species = 0u;
        uint8_t form = 0u, level = 1u, shiny = 0u, submerged = 0u;
        uint16_t dir;
        int life;

        if (dist_sq < 9 || dist_sq > 64) {
            continue;
        }
        cx = (int16_t)(px + ox);
        cz = (int16_t)(pz + oz);

        if (!query_tile_at(mapper, cx, cz, ppos->y, &ty, &hab)) {
            continue;
        }
        if (enc->rate[hab] == 0u) {
            continue;
        }
        gy = (int16_t)(ty / TILE_FX);
        if (ov021_EFFECT_ENCOUNT_IsTileOccupied != NULL &&
            ov021_EFFECT_ENCOUNT_IsTileOccupied(encount_sys, cx, gy, cz, 0) != 0) {
            continue;
        }
        for (k = 0u; k < WILD_MAX_SLOTS; k++) {
            if (g_slots[k].active) {
                int dx = (int)g_slots[k].gx - (int)cx;
                int dz = (int)g_slots[k].gz - (int)cz;
                if (dx * dx + dz * dz < 4) {
                    too_close = 1;
                    break;
                }
            }
        }
        if (too_close) {
            continue;
        }
        if (!roll_slot_for_habitat(enc, gd, hab, &species, &form, &level, &shiny)) {
            continue;
        }
        submerged = (hab == 3) ? should_submerge_water_species(species, form) : 0u;
        dir = (uint16_t)wild_rand_below(4u);
        life = WILD_LIFETIME_MIN + (int)wild_rand_below((uint32_t)(WILD_LIFETIME_MAX - WILD_LIFETIME_MIN));

        if (spawn_wild_in_slot(sys, zoneId, slot_idx, species, form, level, shiny,
                               (uint8_t)hab, submerged, dir, cx, cz, ty, life, 0)) {
            return;
        }
        return;
    }
}

static int wild_api_ready(void)
{
    return (ov010_FLDMMDL_SysGetFieldMap != NULL &&
            ov010_FLDMMDL_SysCheckDrawInit != NULL &&
            ov010_FLDMMDL_SysIsDrawEnable != NULL &&
            ov010_FLDMMDL_SysSearchObjId != NULL &&
            ov021_FIELDMAP_GetGameSystem != NULL &&
            ov021_FIELDMAP_GetZoneId != NULL &&
            ov021_FIELDMAP_GetFieldPlayer != NULL &&
            ov021_FIELDMAP_IsFieldPlayerCreated != NULL &&
            ov021_FIELDMAP_GetG3dMapper != NULL &&
            ov021_FIELDMAP_GetEncount != NULL &&
            ov021_FIELD_PLAYER_GetPos != NULL &&
            GAMESYSTEM_GetGameData != NULL &&
            GAME_DATA_GetMyParty != NULL &&
            POKEPARTY_GetBattleReadyCount != NULL);
}

void pokebw_wild_tick(void *sys)
{
    void *fieldMap, *gsys, *gd, *party, *comm, *player, *mapper, *enc_sys;
    struct PokebwWildEncountData *enc;
    struct PokebwWildVecFx32 ppos;
    int16_t px, pz;
    uint16_t zoneId;
    int hide = 0;
    int density;
    int active_count = 0;
    int first_free = -1;
    uint32_t i;

    g_frame_phase++;

    if (sys == NULL || !wild_api_ready()) {
        return;
    }
    if (g_sys != NULL && g_sys != sys) {
        wild_despawn_all(NULL);
    }
    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        if (g_slots[i].active &&
            ov010_FLDMMDL_SysSearchObjId(sys, g_slots[i].obj_id) != g_slots[i].mmdl) {
            slot_reset(&g_slots[i]);
        }
    }

    fieldMap = ov010_FLDMMDL_SysGetFieldMap(sys);
    if (fieldMap == NULL || ov021_FIELDMAP_IsFieldPlayerCreated(fieldMap) != 1) {
        return;
    }
    if (ov010_FLDMMDL_SysCheckDrawInit(sys) != 1 || ov010_FLDMMDL_SysIsDrawEnable(sys) != 1) {
        return;
    }

    gsys = ov021_FIELDMAP_GetGameSystem(fieldMap);
    gd = (gsys != NULL) ? GAMESYSTEM_GetGameData(gsys) : NULL;
    if (gsys == NULL || gd == NULL) {
        return;
    }
    if (GAME_DATA_IsSaving != NULL && GAME_DATA_IsSaving(gd) != 0) {
        return;
    }

    zoneId = ov021_FIELDMAP_GetZoneId(fieldMap);
    if (g_zone != 0xFFFFu && g_zone != zoneId) {
        wild_despawn_all(sys);
        clear_saved_slots();
    }

    party = GAME_DATA_GetMyParty(gd);
    comm = (GAMESYSTEM_GetGameComm != NULL) ? GAMESYSTEM_GetGameComm(gsys) : NULL;
    enc_sys = ov021_FIELDMAP_GetEncount(fieldMap);
    enc = (enc_sys != NULL) ? ((struct PokebwWildFieldEncount *)enc_sys)->encountData : NULL;
    mapper = ov021_FIELDMAP_GetG3dMapper(fieldMap);

    if (bw_pokebw_setting(POKEBW_SET_OVERWORLD_WILDS) == 0 ||
        party == NULL || POKEPARTY_GetBattleReadyCount(party) == 0u ||
        enc == NULL || !enc->enabled || mapper == NULL ||
        (ov021_FIELDMAP_GetMoveKind != NULL && ov021_FIELDMAP_GetMoveKind(fieldMap) != 0u) ||
        (ZONEDATA_IsNormalFieldZone != NULL && ZONEDATA_IsNormalFieldZone(zoneId) == 0u) ||
        (ZONEDATA_IsUnionRoom != NULL && ZONEDATA_IsUnionRoom(zoneId) != 0u) ||
        (ZONEDATA_IsNoGameEventZone != NULL && ZONEDATA_IsNoGameEventZone(zoneId) != 0u) ||
        (ov021_FIELDMAP_IsMmdlDrawEnabled != NULL && ov021_FIELDMAP_IsMmdlDrawEnabled((int)zoneId) == 0) ||
        (comm != NULL && GAME_COMM_GetKind != NULL && GAME_COMM_GetKind(comm) != 0u)) {
        wild_despawn_all(sys);
        if (bw_pokebw_setting(POKEBW_SET_OVERWORLD_WILDS) == 0) {
            clear_saved_slots();
        }
        return;
    }

    player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
    if (player == NULL) {
        return;
    }
    ov021_FIELD_PLAYER_GetPos(player, &ppos);
    px = (int16_t)(ppos.x / TILE_FX);
    pz = (int16_t)(ppos.z / TILE_FX);

    if ((GAMESYSTEM_GetEvent != NULL && GAMESYSTEM_GetEvent(gsys) != NULL) ||
        (GAMESYSTEM_IsEventActive != NULL && GAMESYSTEM_IsEventActive(gsys) != 0) ||
        (ov021_FIELDMAP_IsEventActive != NULL && ov021_FIELDMAP_IsEventActive(fieldMap) != 0) ||
        (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0u)) {
        hide = 1;
    }
    if (!hide && g_post_battle_cooldown > 0) {
        g_post_battle_cooldown--;
    }

    density = bw_pokebw_setting(POKEBW_SET_OVERWORLD_WILD_DENSITY);
    if (density < 1) density = 1;
    if (density > (int)WILD_MAX_SLOTS) density = (int)WILD_MAX_SLOTS;

    g_sys = sys;
    g_zone = zoneId;
    g_hidden = hide;

    if (!hide && g_has_saved_slots) {
        restore_saved_wilds(sys, mapper, enc_sys, zoneId, px, pz);
    }

    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        struct WildSlot *s = &g_slots[i];
        int64_t dx, dz, dist_sq, limit_sq;

        if (!s->active) {
            if (first_free < 0) {
                first_free = (int)i;
            }
            continue;
        }
        dx = (int64_t)s->gx - (int64_t)px;
        dz = (int64_t)s->gz - (int64_t)pz;
        dist_sq = dx * dx + dz * dz;
        limit_sq = s->shiny ? WILD_SHINY_DIST_SQ : WILD_DESPAWN_DIST_SQ;

        if (!hide) {
            if (dist_sq > WILD_HARD_DESPAWN_DIST_SQ) {
                slot_despawn(sys, s);
                if (first_free < 0) {
                    first_free = (int)i;
                }
                continue;
            }
            if (dist_sq > limit_sq) {
                if (++s->offscreen_frames >= WILD_OFFSCREEN_GRACE_FRAMES) {
                    slot_despawn(sys, s);
                    if (first_free < 0) {
                        first_free = (int)i;
                    }
                    continue;
                }
            } else {
                s->offscreen_frames = 0;
            }
            if (!s->shiny && --s->lifetime_frames <= 0) {
                slot_despawn(sys, s);
                if (first_free < 0) {
                    first_free = (int)i;
                }
                continue;
            }
        }
        active_count++;

        if (!hide) {
            if (s->shiny) {
                if (s->sparkle_frames > 0) {
                    s->sparkle_frames--;
                } else if (((g_frame_phase + i * 7u) % 50u) == 0u) {
                    s->sparkle_frames = 12;
                }
            }

            if (s->moving) {
                int step = ++s->move_step;
                s->pos.x = s->start_x + (int32_t)(((int64_t)(s->target_x - s->start_x) * step) / WILD_MOVE_FRAMES);
                s->pos.y = s->start_y + (int32_t)(((int64_t)(s->target_y - s->start_y) * step) / WILD_MOVE_FRAMES);
                s->pos.z = s->start_z + (int32_t)(((int64_t)(s->target_z - s->start_z) * step) / WILD_MOVE_FRAMES);
                if (step >= WILD_MOVE_FRAMES) {
                    s->moving = 0u;
                    s->move_step = 0u;
                    s->pos.x = s->target_x;
                    s->pos.y = s->target_y;
                    s->pos.z = s->target_z;
                    s->gx = (int16_t)(s->target_x / TILE_FX);
                    s->gz = (int16_t)(s->target_z / TILE_FX);
                    s->pause_frames = 45 + (int)wild_rand_below(90u);
                }
            } else if (--s->pause_frames <= 0) {
                uint16_t d = (uint16_t)wild_rand_below(4u);
                int32_t vx = 0, vz = 0;
                int16_t nx, nz, ngy;
                int32_t ny = 0;
                int nhab = -1;

                dir_to_vec(d, &vx, &vz);
                s->dir = d;
                nx = (int16_t)(s->gx + vx);
                nz = (int16_t)(s->gz + vz);
                if ((nx != px || nz != pz) &&
                    !tile_blocked_by_wild((int)i, nx, nz) &&
                    query_tile_at(mapper, nx, nz, s->pos.y, &ny, &nhab) &&
                    nhab == (int)s->habitat) {
                    ngy = (int16_t)(ny / TILE_FX);
                    if (ov021_EFFECT_ENCOUNT_IsTileOccupied == NULL ||
                        ov021_EFFECT_ENCOUNT_IsTileOccupied(enc_sys, nx, ngy, nz, 0) == 0) {
                        s->start_x = s->pos.x;
                        s->start_y = s->pos.y;
                        s->start_z = s->pos.z;
                        s->target_x = (int32_t)nx * TILE_FX + (TILE_FX / 2);
                        s->target_y = ny;
                        s->target_z = (int32_t)nz * TILE_FX + (TILE_FX / 2);
                        s->moving = 1u;
                        s->move_step = 0u;
                    } else {
                        s->pause_frames = 35 + (int)wild_rand_below(50u);
                    }
                } else {
                    s->pause_frames = 35 + (int)wild_rand_below(50u);
                }
            }
        }
        slot_sync_mmdl(s, zoneId, hide);
    }

    if (!hide && g_post_battle_cooldown == 0 && active_count < density && first_free >= 0) {
        if (++g_spawn_timer >= WILD_SPAWN_INTERVAL) {
            g_spawn_timer = 0;
            try_spawn_wild(sys, mapper, enc_sys, enc, gd, zoneId, &ppos, first_free);
        }
    }
}

void pokebw_wild_on_delete_sys(void *sys)
{
    uint32_t i;
    int saved_any = 0;

    if (g_zone != 0xFFFFu && bw_pokebw_setting(POKEBW_SET_OVERWORLD_WILDS) != 0) {
        clear_saved_slots();
        for (i = 0u; i < WILD_MAX_SLOTS; i++) {
            const struct WildSlot *s = &g_slots[i];
            if (!s->active) {
                continue;
            }
            g_saved_slots[i].valid = 1;
            g_saved_slots[i].species = s->species;
            g_saved_slots[i].form = s->form;
            g_saved_slots[i].level = s->level;
            g_saved_slots[i].shiny = s->shiny;
            g_saved_slots[i].habitat = s->habitat;
            g_saved_slots[i].submerged = s->submerged;
            g_saved_slots[i].dir = s->dir;
            g_saved_slots[i].gx = s->gx;
            g_saved_slots[i].gz = s->gz;
            g_saved_slots[i].y = s->pos.y;
            g_saved_slots[i].lifetime_frames = s->lifetime_frames;
            g_saved_slots[i].offscreen_frames = s->offscreen_frames;
            saved_any = 1;
        }
        if (saved_any) {
            g_saved_zone = g_zone;
            g_has_saved_slots = 1;
        }
    } else {
        clear_saved_slots();
    }

    wild_despawn_all(sys);
}

void *pokebw_wild_check_encounter(void *gameSystem, void *fieldMap)
{
    void *gd, *party, *comm, *player, *enc_sys, *subscreen;
    struct PokebwWildVecFx32 ppos;
    uint16_t pdir;
    uint32_t trg;
    int formSlot = 0;
    int32_t vx = 0, vz = 0;
    int hit_idx = -1;
    uint32_t i;

    if (bw_pokebw_setting(POKEBW_SET_OVERWORLD_WILDS) == 0) {
        return NULL;
    }
    if (gameSystem == NULL || fieldMap == NULL || g_sys == NULL || g_hidden != 0 ||
        g_post_battle_cooldown > 0) {
        return NULL;
    }
    if (GAMESYSTEM_GetGameData == NULL || GAME_DATA_GetMyParty == NULL ||
        POKEPARTY_GetBattleReadyCount == NULL ||
        ov021_FIELDMAP_GetEncount == NULL || ov021_FIELDMAP_GetFieldPlayer == NULL ||
        ov021_FIELD_PLAYER_GetPos == NULL || ov021_FIELD_PLAYER_GetDirDisp == NULL ||
        BTL_SETUP_Create == NULL || ov021_FIELD_ENCOUNT_SetupParam == NULL ||
        ov021_FIELD_ENCOUNT_SetupWildBattle == NULL ||
        ov010_EVENT_BATTLE_CreateScriptedBattle == NULL) {
        return NULL;
    }
    if (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0u) {
        return NULL;
    }
    if (GAMESYSTEM_GetEvent != NULL && GAMESYSTEM_GetEvent(gameSystem) != NULL) {
        return NULL;
    }
    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL || (GAME_DATA_IsSaving != NULL && GAME_DATA_IsSaving(gd) != 0)) {
        return NULL;
    }
    if (GAMESYSTEM_GetGameComm != NULL && GAME_COMM_GetKind != NULL) {
        comm = GAMESYSTEM_GetGameComm(gameSystem);
        if (comm != NULL && GAME_COMM_GetKind(comm) != 0u) {
            return NULL;
        }
    }
    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL || POKEPARTY_GetBattleReadyCount(party) == 0u) {
        return NULL;
    }
    if (ov021_FIELDMAP_GetSubscreen != NULL && ov021_FIELD_SUBSCREEN_GetMode != NULL) {
        subscreen = ov021_FIELDMAP_GetSubscreen(fieldMap);
        if (subscreen != NULL && ov021_FIELD_SUBSCREEN_GetMode(subscreen) == 6u) {
            return NULL;
        }
    }
    enc_sys = ov021_FIELDMAP_GetEncount(fieldMap);
    player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
    if (enc_sys == NULL || player == NULL) {
        return NULL;
    }
    if (ov021_FIELD_PLAYER_GetFormSlot != NULL) {
        formSlot = ov021_FIELD_PLAYER_GetFormSlot(player);
        if (formSlot == PLAYER_FORM_DIVE) {
            return NULL;
        }
    }

    ov021_FIELD_PLAYER_GetPos(player, &ppos);
    pdir = ov021_FIELD_PLAYER_GetDirDisp(player);
    dir_to_vec(pdir, &vx, &vz);
    trg = (KEY_GetTrg != NULL) ? KEY_GetTrg() : 0u;

    for (i = 0u; i < WILD_MAX_SLOTS; i++) {
        struct WildSlot *s = &g_slots[i];
        int64_t dx, dy, dz;

        if (!s->active) {
            continue;
        }
        if ((s->habitat == 3u) != (formSlot == PLAYER_FORM_SURF)) {
            continue;
        }
        dx = (int64_t)s->pos.x - (int64_t)ppos.x;
        dy = (int64_t)s->pos.y - (int64_t)ppos.y;
        dz = (int64_t)s->pos.z - (int64_t)ppos.z;
        if (dy < 0) dy = -dy;
        if (dy > 0x18000) {
            continue;
        }
        if ((dx < 0 ? -dx : dx) <= 0xA800 && (dz < 0 ? -dz : dz) <= 0xA800) {
            hit_idx = (int)i;
            break;
        }
        if (trg & 1u) {
            int64_t fwd = dx * (int64_t)vx + dz * (int64_t)vz;
            int64_t lat = dx * (int64_t)vz - dz * (int64_t)vx;
            if (lat < 0) lat = -lat;
            if (fwd >= 0x4000 && fwd <= 0x1E000 && lat <= 0xC000) {
                hit_idx = (int)i;
                break;
            }
        }
    }

    if (hit_idx < 0) {
        return NULL;
    }

    {
        struct WildSlot picked = g_slots[hit_idx];
        struct PokebwWildEncountParam param;
        struct PokebwWildEncountMon mons[2];
        uint16_t weather = 0u;
        void *btlSetup;
        void *ev;

        slot_despawn(g_sys, &g_slots[hit_idx]);
        g_post_battle_cooldown = WILD_COOLDOWN_FRAMES;

        if (ov021_FIELDMAP_GetPlaceName != NULL && ov021_FIELD_PLACE_NAME_Cancel != NULL) {
            void *placeName = ov021_FIELDMAP_GetPlaceName(fieldMap);
            if (placeName != NULL) {
                (void)ov021_FIELD_PLACE_NAME_Cancel(placeName);
            }
        }
        if (ov021_FIELD_PLAYER_ResetGridHold != NULL) {
            ov021_FIELD_PLAYER_ResetGridHold(player, 1);
        }
        if (ov021_FIELD_PLAYER_StopGridForcedMove != NULL) {
            ov021_FIELD_PLAYER_StopGridForcedMove(player);
        }
        if (ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent != NULL) {
            ov021_FIELD_EVENT_CHECK_StopSoundAfterEvent(gameSystem, fieldMap);
        }

        if (ov021_FIELDMAP_GetWeather != NULL && ov021_WEATHER_GetWeatherId != NULL) {
            void *wsys = ov021_FIELDMAP_GetWeather(fieldMap);
            if (wsys != NULL) {
                weather = ov021_WEATHER_GetWeatherId(wsys);
            }
        }

        memset(&param, 0, sizeof(param));
        ov021_FIELD_ENCOUNT_SetupParam(&param, gd, (int)picked.habitat, 0, weather);
        param.flags &= ~(1u << 5); /* force single battle for triggered wild */
        param.monCount = 1u;

        memset(mons, 0, sizeof(mons));
        mons[0].species = picked.species;
        mons[0].level = picked.level;
        mons[0].form = picked.form;
        mons[0].shinyMode = picked.shiny ? 1u : 2u;

        btlSetup = BTL_SETUP_Create(4u);
        if (btlSetup == NULL) {
            return NULL;
        }
        g_wild_from_overworld = 1;
        g_wild_force_shiny = picked.shiny ? 1 : 0;
        ov021_FIELD_ENCOUNT_SetupWildBattle(enc_sys, &param, btlSetup, 4u, mons);
        g_wild_force_shiny = 0;
        g_wild_from_overworld = 0;

        ev = ov010_EVENT_BATTLE_CreateScriptedBattle(gameSystem, fieldMap, btlSetup, 0, 0);
        if (ev != NULL) {
            fprintf(stderr,
                    "eidos: wild: battle species=%u form=%u lv=%u shiny=%u hab=%u submerged=%u\n",
                    (unsigned)picked.species, (unsigned)picked.form,
                    (unsigned)picked.level, (unsigned)picked.shiny,
                    (unsigned)picked.habitat, (unsigned)picked.submerged);
        }
        return ev;
    }
}

uint32_t __wrap_ov021_FIELD_ENCOUNT_CreatePersonality(
    const void *param_ptr, void *personal, void *enc_ptr)
{
    if (g_wild_force_shiny && param_ptr != NULL && enc_ptr != NULL &&
        POKE_TOOL_CreatePersonalRnd != NULL && POKE_PERSONAL_GetParam != NULL &&
        POKE_PERSONAL_IsFixedGender != NULL) {
        const struct PokebwWildEncountParam *param =
            (const struct PokebwWildEncountParam *)param_ptr;
        const struct PokebwWildEncountMon *em =
            (const struct PokebwWildEncountMon *)enc_ptr;
        uint32_t gender = 2u;

        g_wild_force_shiny = 0;
        if (POKE_PERSONAL_IsFixedGender((uint8_t)POKE_PERSONAL_GetParam(personal, 0x14)) != 1u) {
            if ((param->flags & (1u << 11)) != 0u) { /* cuteCharm */
                if (param->leadGender == 0u) gender = 1u;
                else if (param->leadGender == 1u) gender = 0u;
            }
            if (em->forcedGender == 1u) gender = 0u;
            else if (em->forcedGender == 2u) gender = 1u;
        }
        return POKE_TOOL_CreatePersonalRnd(
            param->ownerId, (int)em->species, (uint16_t)em->form,
            (int)gender, (int)wild_rand_below(2u), 1);
    }
    {
        uint32_t pid;
        pokebw_shiny_enter_wild();
        pid = __real_ov021_FIELD_ENCOUNT_CreatePersonality(param_ptr, personal, enc_ptr);
        pokebw_shiny_leave_wild();

        if (!g_wild_from_overworld && param_ptr != NULL && pokebw_shiny_is_boosted()) {
            const struct PokebwWildEncountParam *param =
                (const struct PokebwWildEncountParam *)param_ptr;
            if (!pokebw_shiny_is_pid_shiny(param->ownerId, pid) &&
                pokebw_shiny_roll_boosted()) {
                return pokebw_shiny_force_pid(param->ownerId, pid);
            }
        }
        return pid;
    }
}
