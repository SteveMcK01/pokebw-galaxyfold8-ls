/*
 * sdk/pokebw_follower.c -- Phase 3.2: Native C Following Pokémon Controller.
 *
 * Spawns and animates the player's first non-Egg conscious party Pokémon as a
 * billboard Mmdl following the player's smooth arc-length breadcrumb trail across
 * grid maps, rail maps, curved Unova cameras, and 3D bridges. Automatically
 * hides during events/cutscenes/wipes/Surfing/Diving, reacts with a cry + hop
 * on A-button talk, and tears down cleanly on every map/MmdlSys exit.
 *
 * Linked into eidos_boot only (CMakeLists.txt) via:
 *   -Wl,--wrap=ov010_FLDMMDL_UpdateSysProc
 *   -Wl,--wrap=ov021_FIELDMAP_DeleteMmdlSys
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pokebw_follower.h"
#include "pokebw_ow_sprites.h"
#include "pokebw_settings_ids.h"

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_FOL_WEAK __attribute__((weak))
#else
#define POKEBW_FOL_WEAK
#endif

#define FOLLOWER_OBJ_ID        0x00FDu
#define TRAIL_CAP              64u
#define TILE_FX                0x10000
#define FOLLOW_DIST_STD        0x10000u /* 1.00 tile (16x16 / 32x32 sprites) */
#define FOLLOW_DIST_BIG        0x15999u /* 1.35 tiles (64x64 mdlSize==2 sprites) */
#define WARP_DIST_THRESHOLD    0x38000u /* 3.50 tiles -> re-anchor trail */
#define MIN_SAMPLE_STEP        0x0800u  /* 0.5 px minimum step to record */
#define HOP_FRAMES_TOTAL       14
#define TALK_COOLDOWN_FRAMES   45

#define POKE_PARAM_SPECIES     0x05u
#define POKE_PARAM_IS_EGG      0x4Cu
#define POKE_PARAM_FORM        0x6Fu
#define POKE_PARAM_HP          0xA0u

#define PLAYER_FORM_SURF       2
#define PLAYER_FORM_DIVE       3

/* ABI-matched view of sdk_fx.h VecFx32 (12 bytes). */
struct PokebwVecFx32 {
    int32_t x;
    int32_t y;
    int32_t z;
};

/* ABI-matched view of ov010/fldmmdl_types.h MmdlHeader (0x24 bytes). */
struct PokebwMmdlHeader {
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

/* ABI-matched view of ov021/fldmmdl_blact_types.h BlactAnimeTable. */
struct PokebwBlactAnimeTable {
    void *cmdLists;
    uint32_t listCount;
};

typedef char pokebw_fol_vec_size_check[
    sizeof(struct PokebwVecFx32) == 12u ? 1 : -1];
typedef char pokebw_fol_hdr_size_check[
    sizeof(struct PokebwMmdlHeader) == 0x24u ? 1 : -1];

struct TrailSample {
    int32_t  x;
    int32_t  y;
    int32_t  z;
    uint64_t cum_dist;
    uint16_t dir;
};

/* ARM9 symbols */
extern void *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_FOL_WEAK;
extern void *GAMESYSTEM_GetEvent(const void *gameSystem) POKEBW_FOL_WEAK;
extern int   GAMESYSTEM_IsEventActive(void *gameSystem) POKEBW_FOL_WEAK;
extern void *GAMESYSTEM_GetGameComm(const void *gameSystem) POKEBW_FOL_WEAK;
extern void *GAME_DATA_GetMyParty(const void *gd) POKEBW_FOL_WEAK;
extern uint8_t GAME_DATA_IsSaving(const void *gd) POKEBW_FOL_WEAK;
extern uint32_t POKEPARTY_GetCount(const void *party) POKEBW_FOL_WEAK;
extern void *POKEPARTY_GetMember(void *party, uint32_t pos) POKEBW_FOL_WEAK;
extern uint32_t POKE_TOOL_GetPartyValue(void *pp, uint32_t paramId, uintptr_t arg) POKEBW_FOL_WEAK;
extern uint32_t POKE_TOOL_IsShiny(void *pp) POKEBW_FOL_WEAK;
extern uint32_t WIPE_SYS_EndCheck(void) POKEBW_FOL_WEAK;
extern uint32_t GAME_COMM_GetKind(const void *comm) POKEBW_FOL_WEAK;
extern uint32_t ZONEDATA_IsNormalFieldZone(uint32_t id) POKEBW_FOL_WEAK;
extern uint32_t ZONEDATA_IsUnionRoom(uint32_t id) POKEBW_FOL_WEAK;
extern uint32_t KEY_GetTrg(void) POKEBW_FOL_WEAK;
extern uint32_t PM_VOICE_Play(uint32_t species, uint32_t form, uint32_t pan, uint32_t dual,
                              uint32_t volumeBiasB, uint32_t frequencyOffsetB,
                              uint32_t reverse, void **recordedVoice) POKEBW_FOL_WEAK;

/* ov010 symbols */
extern void *ov010_FLDMMDL_AddMmdl(void *sys, const struct PokebwMmdlHeader *header, int zoneId) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_DeleteMmdl(void *mmdl) POKEBW_FOL_WEAK;
extern void *ov010_FLDMMDL_SysGetFreeMmdl(void *sys) POKEBW_FOL_WEAK;
extern void *ov010_FLDMMDL_SysSearchObjId(void *sys, uint32_t objId) POKEBW_FOL_WEAK;
extern void *ov010_FLDMMDL_SysGetFieldMap(void *sys) POKEBW_FOL_WEAK;
extern int   ov010_FLDMMDL_SysCheckDrawInit(void *sys) POKEBW_FOL_WEAK;
extern int   ov010_FLDMMDL_SysIsDrawEnable(void *sys) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitVanish(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitFellowHit(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitHeightGetOff(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitAttrGetOff(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetStatusBitZoneDeleteOff(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetMoveBitAttrEffectOff(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetMoveBitShadowVanish(void *mmdl, int flag) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetPos(void *mmdl, const struct PokebwVecFx32 *pos) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetDrawOffset(void *mmdl, const struct PokebwVecFx32 *offset) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetDirAll(void *mmdl, uint16_t dir) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetDrawStatus(void *mmdl, uint16_t drawStatus) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridNowX(void *mmdl, int16_t x) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridNowY(void *mmdl, int y) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridNowZ(void *mmdl, int z) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridOldX(void *mmdl, int16_t x) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridOldY(void *mmdl, int16_t y) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetGridOldZ(void *mmdl, int16_t z) POKEBW_FOL_WEAK;
extern void  ov010_FLDMMDL_SetZoneId(void *mmdl, uint16_t zoneId) POKEBW_FOL_WEAK;

/* ov021 symbols */
extern void    *ov021_FIELDMAP_GetGameSystem(void *fieldMap) POKEBW_FOL_WEAK;
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_FOL_WEAK;
extern void    *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) POKEBW_FOL_WEAK;
extern int      ov021_FIELDMAP_IsFieldPlayerCreated(void *fieldMap) POKEBW_FOL_WEAK;
extern void    *ov021_FIELDMAP_GetMmdlSys(void *fieldMap) POKEBW_FOL_WEAK;
extern int      ov021_FIELDMAP_IsEventActive(void *fieldMap) POKEBW_FOL_WEAK;
extern int      ov021_FIELDMAP_IsMmdlDrawEnabled(int zoneId) POKEBW_FOL_WEAK;
extern void     ov021_FIELD_PLAYER_GetPos(void *player, struct PokebwVecFx32 *out) POKEBW_FOL_WEAK;
extern uint16_t ov021_FIELD_PLAYER_GetDirDisp(void *player) POKEBW_FOL_WEAK;
extern int      ov021_FIELD_PLAYER_GetFormSlot(void *player) POKEBW_FOL_WEAK;

extern void __real_ov010_FLDMMDL_UpdateSysProc(void *sys);
extern int  __real_ov021_FIELDMAP_DeleteMmdlSys(void *fieldMap);

static struct TrailSample g_trail[TRAIL_CAP];
static uint32_t g_trail_head = 0u;
static uint32_t g_trail_len = 0u;

static void    *g_sys = NULL;
static void    *g_mmdl = NULL;
static uint16_t g_obj_code = 0u;
static uint16_t g_species = 0u;
static uint8_t  g_form = 0u;
static uint8_t  g_shiny = 0u;
static uint32_t g_follow_dist = FOLLOW_DIST_STD;
static int      g_hidden = 0;

static struct PokebwVecFx32 g_last_pos = { 0, 0, 0 };
static uint16_t g_last_dir = 1u;
static int      g_hop_frames = 0;
static int      g_talk_face_frames = 0;
static uint16_t g_talk_dir = 1u;
static int      g_cry_cooldown = 0;

static void dir_to_vec(uint16_t dir, int32_t *out_dx, int32_t *out_dz)
{
    switch (dir) {
    case 0u: *out_dx = 0;  *out_dz = -1; break; /* Up */
    case 1u: *out_dx = 0;  *out_dz = 1;  break; /* Down */
    case 2u: *out_dx = -1; *out_dz = 0;  break; /* Left */
    case 3u: *out_dx = 1;  *out_dz = 0;  break; /* Right */
    default: *out_dx = 0;  *out_dz = 1;  break;
    }
}

static uint16_t opposite_dir(uint16_t dir)
{
    switch (dir) {
    case 0u: return 1u;
    case 1u: return 0u;
    case 2u: return 3u;
    case 3u: return 2u;
    default: return 1u;
    }
}

static uint16_t vec_to_dir(int32_t dx, int32_t dz, uint16_t fallback)
{
    int64_t ax = dx < 0 ? -(int64_t)dx : (int64_t)dx;
    int64_t az = dz < 0 ? -(int64_t)dz : (int64_t)dz;

    if (ax == 0 && az == 0) {
        return (fallback <= 3u) ? fallback : 1u;
    }
    if (ax > az) {
        return (dx >= 0) ? 3u : 2u;
    }
    return (dz >= 0) ? 1u : 0u;
}

static uint32_t isqrt64(uint64_t n)
{
    uint64_t x = n;
    uint64_t y;

    if (x <= 1u) {
        return (uint32_t)x;
    }
    y = (x + 1u) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (uint32_t)x;
}

static uint32_t vec_dist3d(const struct PokebwVecFx32 *a, const struct PokebwVecFx32 *b)
{
    int64_t dx = (int64_t)a->x - (int64_t)b->x;
    int64_t dy = (int64_t)a->y - (int64_t)b->y;
    int64_t dz = (int64_t)a->z - (int64_t)b->z;
    uint64_t sq = (uint64_t)(dx * dx + dy * dy + dz * dz);
    return isqrt64(sq);
}

static struct TrailSample *trail_at(uint32_t idx)
{
    return &g_trail[(g_trail_head + idx) % TRAIL_CAP];
}

static void trail_clear(void)
{
    g_trail_head = 0u;
    g_trail_len = 0u;
}

static void trail_append(struct TrailSample s)
{
    if (g_trail_len < TRAIL_CAP) {
        g_trail[(g_trail_head + g_trail_len) % TRAIL_CAP] = s;
        g_trail_len++;
    } else {
        g_trail[g_trail_head] = s;
        g_trail_head = (g_trail_head + 1u) % TRAIL_CAP;
    }
}

static void trail_seed_behind(const struct PokebwVecFx32 *ppos, uint16_t pdir, uint32_t dist)
{
    int32_t vx, vz;
    uint16_t d = (pdir <= 3u) ? pdir : 1u;
    struct TrailSample s0, s1;

    dir_to_vec(d, &vx, &vz);
    trail_clear();

    s0.x = ppos->x - (int32_t)((int64_t)vx * (int64_t)dist);
    s0.y = ppos->y;
    s0.z = ppos->z - (int32_t)((int64_t)vz * (int64_t)dist);
    s0.cum_dist = 0u;
    s0.dir = d;

    s1.x = ppos->x;
    s1.y = ppos->y;
    s1.z = ppos->z;
    s1.cum_dist = (uint64_t)dist;
    s1.dir = d;

    trail_append(s0);
    trail_append(s1);
}

static void trail_push(const struct PokebwVecFx32 *ppos, uint16_t pdir, uint32_t dist)
{
    const struct TrailSample *newest;
    struct PokebwVecFx32 prev;
    uint32_t step;
    struct TrailSample s;

    if (g_trail_len == 0u) {
        trail_seed_behind(ppos, pdir, dist);
        return;
    }

    newest = trail_at(g_trail_len - 1u);
    prev.x = newest->x;
    prev.y = newest->y;
    prev.z = newest->z;
    step = vec_dist3d(ppos, &prev);

    if (step > WARP_DIST_THRESHOLD) {
        trail_seed_behind(ppos, pdir, dist);
        return;
    }
    if (step < MIN_SAMPLE_STEP) {
        return;
    }

    s.x = ppos->x;
    s.y = ppos->y;
    s.z = ppos->z;
    s.cum_dist = newest->cum_dist + (uint64_t)step;
    s.dir = vec_to_dir(ppos->x - prev.x, ppos->z - prev.z, pdir);
    trail_append(s);
}

static void trail_sample(
    const struct PokebwVecFx32 *ppos,
    uint16_t pdir,
    uint32_t dist,
    struct PokebwVecFx32 *out_pos,
    uint16_t *out_dir)
{
    const struct TrailSample *newest;
    uint64_t want;
    uint32_t i;

    if (g_trail_len == 0u) {
        trail_seed_behind(ppos, pdir, dist);
    }

    newest = trail_at(g_trail_len - 1u);
    if (newest->cum_dist <= (uint64_t)dist) {
        const struct TrailSample *oldest = trail_at(0u);
        out_pos->x = oldest->x;
        out_pos->y = oldest->y;
        out_pos->z = oldest->z;
        *out_dir = vec_to_dir(ppos->x - out_pos->x, ppos->z - out_pos->z, oldest->dir);
        return;
    }

    want = newest->cum_dist - (uint64_t)dist;
    for (i = g_trail_len - 1u; i >= 1u; i--) {
        const struct TrailSample *a = trail_at(i - 1u);
        const struct TrailSample *b = trail_at(i);
        if (a->cum_dist <= want && want <= b->cum_dist) {
            uint64_t span = b->cum_dist - a->cum_dist;
            int64_t frac = (span > 0u) ? (int64_t)(((want - a->cum_dist) * 65536u) / span) : 0;

            out_pos->x = a->x + (int32_t)(((int64_t)(b->x - a->x) * frac) >> 16);
            out_pos->y = a->y + (int32_t)(((int64_t)(b->y - a->y) * frac) >> 16);
            out_pos->z = a->z + (int32_t)(((int64_t)(b->z - a->z) * frac) >> 16);
            *out_dir = vec_to_dir(b->x - a->x, b->z - a->z,
                                  vec_to_dir(ppos->x - out_pos->x, ppos->z - out_pos->z, a->dir));
            return;
        }
    }

    {
        const struct TrailSample *oldest = trail_at(0u);
        out_pos->x = oldest->x;
        out_pos->y = oldest->y;
        out_pos->z = oldest->z;
        *out_dir = oldest->dir;
    }
}

static int find_lead_party_mon(
    const void *gd, uint16_t *out_species, uint8_t *out_form, uint8_t *out_shiny)
{
    void *party;
    uint32_t count, i;

    if (gd == NULL || GAME_DATA_GetMyParty == NULL ||
        POKEPARTY_GetCount == NULL || POKEPARTY_GetMember == NULL ||
        POKE_TOOL_GetPartyValue == NULL) {
        return 0;
    }

    party = GAME_DATA_GetMyParty(gd);
    if (party == NULL) {
        return 0;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    for (i = 0u; i < count; i++) {
        void *mon = POKEPARTY_GetMember(party, i);
        uint32_t sp, hp, is_egg;

        if (mon == NULL) {
            continue;
        }
        is_egg = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_IS_EGG, 0u);
        if (is_egg != 0u) {
            continue;
        }
        hp = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_HP, 0u);
        if (hp == 0u) {
            continue;
        }
        sp = POKE_TOOL_GetPartyValue(mon, POKE_PARAM_SPECIES, 0u);
        if (sp == 0u || sp > POKEBW_OW_SPECIES_MAX) {
            continue;
        }
        *out_species = (uint16_t)sp;
        *out_form = (uint8_t)POKE_TOOL_GetPartyValue(mon, POKE_PARAM_FORM, 0u);
        *out_shiny = (POKE_TOOL_IsShiny != NULL && POKE_TOOL_IsShiny(mon) != 0u) ? 1u : 0u;
        return 1;
    }
    return 0;
}

static void follower_reset_state(int clear_trail)
{
    g_sys = NULL;
    g_mmdl = NULL;
    g_obj_code = 0u;
    g_species = 0u;
    g_form = 0u;
    g_shiny = 0u;
    g_follow_dist = FOLLOW_DIST_STD;
    g_hidden = 0;
    g_hop_frames = 0;
    g_talk_face_frames = 0;
    g_cry_cooldown = 0;
    if (clear_trail) {
        trail_clear();
    }
}

static void follower_despawn(void *sys, int clear_trail)
{
    if (g_mmdl != NULL && sys != NULL &&
        ov010_FLDMMDL_SysSearchObjId != NULL &&
        ov010_FLDMMDL_DeleteMmdl != NULL) {
        void *found = ov010_FLDMMDL_SysSearchObjId(sys, FOLLOWER_OBJ_ID);
        if (found == g_mmdl) {
            ov010_FLDMMDL_DeleteMmdl(g_mmdl);
        }
    }
    follower_reset_state(clear_trail);
}

static void follower_sync_mmdl(
    void *mmdl, uint16_t zoneId, const struct PokebwVecFx32 *pos,
    uint16_t dir, int vanish)
{
    int16_t gx = (int16_t)(pos->x / TILE_FX);
    int16_t gy = (int16_t)(pos->y / TILE_FX);
    int16_t gz = (int16_t)(pos->z / TILE_FX);

    if (ov010_FLDMMDL_SetStatusBitVanish != NULL) {
        ov010_FLDMMDL_SetStatusBitVanish(mmdl, vanish ? 1 : 0);
    }
    if (ov010_FLDMMDL_SetMoveBitShadowVanish != NULL) {
        ov010_FLDMMDL_SetMoveBitShadowVanish(mmdl, vanish ? 1 : 0);
    }
    if (ov010_FLDMMDL_SetZoneId != NULL) {
        ov010_FLDMMDL_SetZoneId(mmdl, zoneId);
    }
    if (ov010_FLDMMDL_SetPos != NULL) {
        ov010_FLDMMDL_SetPos(mmdl, pos);
    }
    if (ov010_FLDMMDL_SetGridNowX != NULL) ov010_FLDMMDL_SetGridNowX(mmdl, gx);
    if (ov010_FLDMMDL_SetGridNowY != NULL) ov010_FLDMMDL_SetGridNowY(mmdl, gy);
    if (ov010_FLDMMDL_SetGridNowZ != NULL) ov010_FLDMMDL_SetGridNowZ(mmdl, gz);
    if (ov010_FLDMMDL_SetGridOldX != NULL) ov010_FLDMMDL_SetGridOldX(mmdl, gx);
    if (ov010_FLDMMDL_SetGridOldY != NULL) ov010_FLDMMDL_SetGridOldY(mmdl, gy);
    if (ov010_FLDMMDL_SetGridOldZ != NULL) ov010_FLDMMDL_SetGridOldZ(mmdl, gz);
    if (ov010_FLDMMDL_SetDirAll != NULL) {
        ov010_FLDMMDL_SetDirAll(mmdl, (dir <= 3u) ? dir : 1u);
    }
    if (ov010_FLDMMDL_SetDrawStatus != NULL) {
        ov010_FLDMMDL_SetDrawStatus(mmdl, 0u);
    }
}

static void follower_spawn(
    void *sys, uint16_t zoneId, uint16_t species, uint8_t form,
    uint8_t shiny, uint16_t objCode, uint32_t dist,
    const struct PokebwVecFx32 *ppos, uint16_t pdir)
{
    struct PokebwMmdlHeader hdr;
    struct PokebwVecFx32 spos;
    uint16_t sdir;
    void *mmdl;

    if (ov010_FLDMMDL_AddMmdl == NULL ||
        ov010_FLDMMDL_SysGetFreeMmdl == NULL ||
        ov010_FLDMMDL_SysSearchObjId == NULL) {
        return;
    }
    if (ov010_FLDMMDL_SysSearchObjId(sys, FOLLOWER_OBJ_ID) != NULL) {
        return;
    }
    if (ov010_FLDMMDL_SysGetFreeMmdl(sys) == NULL) {
        return;
    }

    if (g_trail_len == 0u) {
        trail_seed_behind(ppos, pdir, dist);
    } else {
        const struct TrailSample *newest = trail_at(g_trail_len - 1u);
        struct PokebwVecFx32 prev;
        prev.x = newest->x;
        prev.y = newest->y;
        prev.z = newest->z;
        if (vec_dist3d(ppos, &prev) > WARP_DIST_THRESHOLD) {
            trail_seed_behind(ppos, pdir, dist);
        }
    }
    trail_sample(ppos, pdir, dist, &spos, &sdir);

    memset(&hdr, 0, sizeof(hdr));
    hdr.objId = FOLLOWER_OBJ_ID;
    hdr.objCode = objCode;
    hdr.moveCode = 0u;
    hdr.eventId = 0xFFFFu;
    hdr.dirHeader = (sdir <= 3u) ? sdir : 1u;
    hdr.posType = 0;
    hdr.pos[0] = (uint16_t)(int16_t)(spos.x / TILE_FX);
    hdr.pos[1] = (uint16_t)(int16_t)(spos.z / TILE_FX);
    {
        int32_t pos_y = spos.y;
        memcpy(&hdr.pos[2], &pos_y, sizeof(pos_y));
    }

    mmdl = ov010_FLDMMDL_AddMmdl(sys, &hdr, (int)zoneId);
    if (mmdl == NULL) {
        fprintf(stderr,
                "eidos: follower: AddMmdl failed (species=%u form=%u objCode=0x%X zone=%u)\n",
                (unsigned)species, (unsigned)form, (unsigned)objCode, (unsigned)zoneId);
        return;
    }
    fprintf(stderr,
            "eidos: follower: spawned species=%u form=%u shiny=%u objCode=0x%X mdlSize=%u zone=%u\n",
            (unsigned)species, (unsigned)form, (unsigned)shiny, (unsigned)objCode,
            (unsigned)pokebw_ow_sprite_mdl_size(objCode), (unsigned)zoneId);

    if (ov010_FLDMMDL_SetStatusBitFellowHit != NULL) {
        ov010_FLDMMDL_SetStatusBitFellowHit(mmdl, 1);
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

    g_sys = sys;
    g_mmdl = mmdl;
    g_obj_code = objCode;
    g_species = species;
    g_form = form;
    g_shiny = shiny;
    g_follow_dist = dist;
    g_hidden = 0;
    follower_sync_mmdl(mmdl, zoneId, &spos, sdir, 0);
}

static int follower_api_ready(void)
{
    return (ov010_FLDMMDL_SysGetFieldMap != NULL &&
            ov010_FLDMMDL_SysCheckDrawInit != NULL &&
            ov010_FLDMMDL_SysIsDrawEnable != NULL &&
            ov010_FLDMMDL_SysSearchObjId != NULL &&
            ov021_FIELDMAP_GetGameSystem != NULL &&
            ov021_FIELDMAP_GetZoneId != NULL &&
            ov021_FIELDMAP_GetFieldPlayer != NULL &&
            ov021_FIELDMAP_IsFieldPlayerCreated != NULL &&
            ov021_FIELD_PLAYER_GetPos != NULL &&
            ov021_FIELD_PLAYER_GetDirDisp != NULL &&
            GAMESYSTEM_GetGameData != NULL);
}

static void pokebw_follower_tick(void *sys)
{
    void *fieldMap;
    void *gsys;
    void *gd;
    void *comm;
    void *player;
    uint16_t zoneId;
    uint16_t species = 0u, wanted_code = 0u;
    uint8_t form = 0u, shiny = 0u, mdl_size = 0u;
    uint32_t dist;
    struct PokebwVecFx32 ppos, fpos;
    uint16_t pdir, fdir;
    int hide = 0;
    int formSlot = 0;

    if (sys == NULL || !follower_api_ready()) {
        return;
    }

    /* Validate existing follower pointer against live MmdlSys before using it. */
    if (g_mmdl != NULL) {
        if (g_sys != sys || ov010_FLDMMDL_SysSearchObjId(sys, FOLLOWER_OBJ_ID) != g_mmdl) {
            follower_reset_state(1);
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
    comm = (GAMESYSTEM_GetGameComm != NULL) ? GAMESYSTEM_GetGameComm(gsys) : NULL;

    if (bw_pokebw_setting(POKEBW_SET_FOLLOWING_POKEMON) == 0 ||
        (ZONEDATA_IsNormalFieldZone != NULL && ZONEDATA_IsNormalFieldZone(zoneId) == 0u) ||
        (ZONEDATA_IsUnionRoom != NULL && ZONEDATA_IsUnionRoom(zoneId) != 0u) ||
        (ov021_FIELDMAP_IsMmdlDrawEnabled != NULL && ov021_FIELDMAP_IsMmdlDrawEnabled((int)zoneId) == 0) ||
        (comm != NULL && GAME_COMM_GetKind != NULL && GAME_COMM_GetKind(comm) != 0u)) {
        if (g_mmdl != NULL) {
            follower_despawn(sys, 1);
        }
        return;
    }

    if (!find_lead_party_mon(gd, &species, &form, &shiny)) {
        if (g_mmdl != NULL) {
            follower_despawn(sys, 1);
        }
        return;
    }

    wanted_code = pokebw_ow_sprite_objcode(species, form, shiny);
    if (wanted_code == 0u) {
        if (g_mmdl != NULL) {
            follower_despawn(sys, 1);
        }
        return;
    }
    mdl_size = pokebw_ow_sprite_mdl_size(wanted_code);
    dist = (mdl_size == 2u) ? FOLLOW_DIST_BIG : FOLLOW_DIST_STD;

    player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
    if (player == NULL) {
        return;
    }
    ov021_FIELD_PLAYER_GetPos(player, &ppos);
    pdir = ov021_FIELD_PLAYER_GetDirDisp(player);
    g_last_pos = ppos;
    g_last_dir = (pdir <= 3u) ? pdir : 1u;

    if (ov021_FIELD_PLAYER_GetFormSlot != NULL) {
        formSlot = ov021_FIELD_PLAYER_GetFormSlot(player);
    }
    if ((GAMESYSTEM_GetEvent != NULL && GAMESYSTEM_GetEvent(gsys) != NULL) ||
        (GAMESYSTEM_IsEventActive != NULL && GAMESYSTEM_IsEventActive(gsys) != 0) ||
        (ov021_FIELDMAP_IsEventActive != NULL && ov021_FIELDMAP_IsEventActive(fieldMap) != 0) ||
        (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0u) ||
        formSlot == PLAYER_FORM_SURF || formSlot == PLAYER_FORM_DIVE) {
        hide = 1;
    }

    if (g_mmdl != NULL && g_obj_code != wanted_code) {
        follower_despawn(sys, 0);
    }

    if (g_mmdl == NULL) {
        if (hide) {
            trail_push(&ppos, pdir, dist);
            g_hidden = 1;
            return;
        }
        follower_spawn(sys, zoneId, species, form, shiny, wanted_code, dist, &ppos, pdir);
        return;
    }

    if (g_hidden && !hide) {
        const struct TrailSample *newest = (g_trail_len > 0u) ? trail_at(g_trail_len - 1u) : NULL;
        struct PokebwVecFx32 prev;
        if (newest == NULL ||
            (prev.x = newest->x, prev.y = newest->y, prev.z = newest->z,
             vec_dist3d(&ppos, &prev) > WARP_DIST_THRESHOLD)) {
            trail_seed_behind(&ppos, pdir, dist);
        }
    }
    g_hidden = hide;
    g_follow_dist = dist;

    trail_push(&ppos, pdir, dist);
    trail_sample(&ppos, pdir, dist, &fpos, &fdir);

    if (g_talk_face_frames > 0) {
        fdir = g_talk_dir;
        g_talk_face_frames--;
    }
    if (g_cry_cooldown > 0) {
        g_cry_cooldown--;
    }

    if (ov010_FLDMMDL_SetDrawOffset != NULL) {
        struct PokebwVecFx32 hop = { 0, 0, 0 };
        if (g_hop_frames > 0) {
            int rem = g_hop_frames--;
            int t = HOP_FRAMES_TOTAL - rem;
            /* Parabolic hop peaking at ~0x6000 (6 px) halfway through. */
            int32_t peak = (int32_t)(t * (HOP_FRAMES_TOTAL - t));
            hop.y = peak * 0x0200;
        }
        ov010_FLDMMDL_SetDrawOffset(g_mmdl, &hop);
    }

    follower_sync_mmdl(g_mmdl, zoneId, &fpos, fdir, hide);
}

void pokebw_follower_on_a_press(void *gameSystem, void *fieldMap)
{
    uint32_t trg;
    struct PokebwVecFx32 ppos, fpos, unused_pos;
    uint16_t pdir, unused_dir;
    void *player;
    int32_t vx, vz;
    int64_t dx, dz, fwd, lat;

    if (gameSystem == NULL || fieldMap == NULL || g_mmdl == NULL || g_sys == NULL ||
        g_hidden != 0 || g_cry_cooldown > 0 || g_species == 0u ||
        KEY_GetTrg == NULL || PM_VOICE_Play == NULL ||
        ov010_FLDMMDL_SysSearchObjId == NULL ||
        ov021_FIELDMAP_GetFieldPlayer == NULL ||
        ov021_FIELD_PLAYER_GetPos == NULL ||
        ov021_FIELD_PLAYER_GetDirDisp == NULL) {
        return;
    }

    trg = KEY_GetTrg();
    if ((trg & 1u) == 0u) {
        return;
    }
    if (GAMESYSTEM_GetEvent != NULL && GAMESYSTEM_GetEvent(gameSystem) != NULL) {
        return;
    }
    if (WIPE_SYS_EndCheck != NULL && WIPE_SYS_EndCheck() == 0u) {
        return;
    }
    if (ov010_FLDMMDL_SysSearchObjId(g_sys, FOLLOWER_OBJ_ID) != g_mmdl) {
        return;
    }

    player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
    if (player == NULL) {
        return;
    }
    ov021_FIELD_PLAYER_GetPos(player, &ppos);
    pdir = ov021_FIELD_PLAYER_GetDirDisp(player);
    trail_sample(&ppos, pdir, g_follow_dist, &fpos, &unused_dir);
    (void)unused_pos;

    dir_to_vec(pdir, &vx, &vz);
    dx = (int64_t)fpos.x - (int64_t)ppos.x;
    dz = (int64_t)fpos.z - (int64_t)ppos.z;
    fwd = dx * (int64_t)vx + dz * (int64_t)vz;
    lat = dx * (int64_t)vz - dz * (int64_t)vx;
    if (lat < 0) {
        lat = -lat;
    }

    if (fwd >= 0x4000 && fwd <= 0x1E000 && lat <= 0xC000) {
        g_talk_dir = opposite_dir(pdir);
        g_talk_face_frames = TALK_COOLDOWN_FRAMES;
        g_hop_frames = HOP_FRAMES_TOTAL;
        g_cry_cooldown = TALK_COOLDOWN_FRAMES;
        if (ov010_FLDMMDL_SetDirAll != NULL) {
            ov010_FLDMMDL_SetDirAll(g_mmdl, g_talk_dir);
        }
        (void)PM_VOICE_Play(g_species, g_form, 0u, 0u, 0u, 0u, 0u, NULL);
    }
}

void __wrap_ov010_FLDMMDL_UpdateSysProc(void *sys)
{
    pokebw_follower_tick(sys);
    __real_ov010_FLDMMDL_UpdateSysProc(sys);
}

int __wrap_ov021_FIELDMAP_DeleteMmdlSys(void *fieldMap)
{
    if (fieldMap != NULL && ov021_FIELDMAP_GetMmdlSys != NULL) {
        void *sys = ov021_FIELDMAP_GetMmdlSys(fieldMap);
        if (sys != NULL && g_mmdl != NULL && g_sys == sys) {
            follower_despawn(sys, 1);
        } else {
            follower_reset_state(1);
        }
    } else {
        follower_reset_state(1);
    }
    return __real_ov021_FIELDMAP_DeleteMmdlSys(fieldMap);
}
