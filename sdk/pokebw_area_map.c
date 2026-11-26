/*
 * sdk/pokebw_area_map.c -- Phase 4.9: Gear Overhead Area Item Map.
 *
 * Samples a 64x64 top-down terrain map centered on the player from ov021's
 * FieldG3dMapper / MapAttr (with terrain cache reuse across nearby steps),
 * collects all overworld Item Balls (eventId 7000..7999), Hidden Dowsing
 * Machine items (bgEvent type 2 + HiddenItemTable daily-respawn check), and
 * zone warps/exits (grid and rail maps), decodes the item ID & quantity from
 * archive 0x39 field scripts, checks live pickup flags, and pushes the
 * compact snapshot to the ImGui Gear tab via bw_host_map_push().
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pokebw_area_map.h"
#include "pokebw_gear.h"
#include "pokebw_map_state.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#if defined(__GNUC__) || defined(__clang__)
#define POKEBW_MAP_WEAK __attribute__((weak))
#else
#define POKEBW_MAP_WEAK
#endif

#if defined(__wasm__)

#define TILE_FX              0x10000
#define BW_SCRIPT_ARC        0x39u
#define BW_TEMP_HEAP         0x8015u
#define MSG_ITEM_BANK        54
#define MSG_PLACE_BANK       89
#define HIDDEN_ITEM_FLAG_BASE 900u

struct PokebwMapVecFx32 {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct PokebwMapRailLoc {
    uint16_t railIndex;
    uint8_t  type;
    uint8_t  key;
    int16_t  widthGrid;
    uint16_t lineGrid;
};

struct PokebwMapHit {
    uint16_t kind;
    uint16_t index;
    uint16_t flags;
    uint16_t pad06;
    int32_t  attr;
    int32_t  height;
};

struct PokebwMapMmdlHdr {
    uint16_t objId;
    uint16_t objCode;
    uint16_t moveCode;
    uint16_t eventType;
    uint16_t eventFlag;
    uint16_t eventId;
    uint16_t dirHeader;
    uint16_t param0;
    uint16_t param1;
    uint16_t param2;
    int16_t  moveLimitX;
    int16_t  moveLimitZ;
    int32_t  posType;
    uint16_t pos[4];
};

struct PokebwMapBgEvent {
    uint16_t scriptId;
    uint16_t type;
    uint16_t dir;
    uint16_t posType;
    int32_t  word[3];
};

struct PokebwMapConnect {
    uint16_t zoneId;
    uint16_t exitId;
    uint8_t  exitDir;
    uint8_t  exitType;
    uint16_t posType;
    union {
        struct {
            int16_t  x;
            int16_t  y;
            int16_t  z;
            uint16_t sizeX;
            uint16_t sizeZ;
        } grid;
        struct {
            uint16_t railIndex;
            uint16_t lineGrid;
            int16_t  widthGrid;
            uint16_t lineSize;
            uint16_t widthSize;
            uint16_t flipMode;
        } rail;
    } area;
};

struct PokebwMapEventDataSys {
    uint32_t reserved00;
    void    *eventArc;
    void    *encountArc;
    void    *auxArc;
    uint16_t zoneId;
    uint16_t bgCount;
    uint16_t npcCount;
    uint16_t connectCount;
    uint16_t posCount;
    uint16_t pad1A;
    const struct PokebwMapBgEvent *bgEvents;
    const struct PokebwMapMmdlHdr *npcHeaders;
    const struct PokebwMapConnect *connects;
};

struct PokebwMapHiddenEntry {
    uint16_t flagIndex;
    uint8_t  isDowsable;
    uint8_t  respawns;
    uint16_t zoneId;
    uint16_t gridX;
    uint16_t gridZ;
};

typedef char pokebw_map_vec_check[sizeof(struct PokebwMapVecFx32) == 12u ? 1 : -1];
typedef char pokebw_map_rail_check[sizeof(struct PokebwMapRailLoc) == 8u ? 1 : -1];
typedef char pokebw_map_hit_check[sizeof(struct PokebwMapHit) == 16u ? 1 : -1];
typedef char pokebw_map_hdr_check[sizeof(struct PokebwMapMmdlHdr) == 0x24u ? 1 : -1];
typedef char pokebw_map_bg_check[sizeof(struct PokebwMapBgEvent) == 0x14u ? 1 : -1];
typedef char pokebw_map_conn_check[sizeof(struct PokebwMapConnect) == 0x14u ? 1 : -1];
typedef char pokebw_map_hid_check[sizeof(struct PokebwMapHiddenEntry) == 10u ? 1 : -1];
typedef char pokebw_map_eds_check[
    (sizeof(void *) != 4 || offsetof(struct PokebwMapEventDataSys, connects) == 0x24u) ? 1 : -1];

/* ARM9 symbols */
extern void    *GAMESYSTEM_GetGameData(const void *gameSystem) POKEBW_MAP_WEAK;
extern void    *GAME_DATA_GetEventData(const void *gd) POKEBW_MAP_WEAK;
extern void    *GAME_DATA_GetEventWork(const void *gd) POKEBW_MAP_WEAK;
extern uint32_t ZONEDATA_CheckEventFlag(void *evWork, int32_t flagId) POKEBW_MAP_WEAK;
extern uint32_t ZONEDATA_GetPlaceNameId(uint32_t id) POKEBW_MAP_WEAK;
extern uint32_t ZONEDATA_IsRailMap(uint32_t id) POKEBW_MAP_WEAK;
extern uint32_t ARC_TOOL_GetMemberSize(uint32_t archiveID, uint32_t memberIdx) POKEBW_MAP_WEAK;
extern void    *ARC_TOOL_LoadMember(uint32_t archiveID, uint32_t memberIdx, uint16_t heapID) POKEBW_MAP_WEAK;
extern void     GFL_HEAP_Free(void *ptr) POKEBW_MAP_WEAK;

/* ov010 symbols */
extern uint16_t ov010_HIDDEN_ITEM_GetFlag(uint16_t scriptId) POKEBW_MAP_WEAK;
extern uint16_t ov010_HIDDEN_ITEM_GetIndex(uint32_t scriptId) POKEBW_MAP_WEAK;
extern const struct PokebwMapHiddenEntry *ov010_HIDDEN_ITEM_GetTable(uint16_t *count) POKEBW_MAP_WEAK;
extern uint16_t ov010_SCRIPT_SYS_GetScriptFileId(
    uint32_t zoneId, uint32_t scriptId,
    uint16_t *outFileId, uint16_t *outTextBank, uint16_t *outTextMsgId) POKEBW_MAP_WEAK;
extern int      ov010_SCRIPT_SYS_IsZoneScriptId(uint16_t scriptId) POKEBW_MAP_WEAK;

/* ov021 symbols */
extern uint16_t ov021_FIELDMAP_GetZoneId(void *fieldMap) POKEBW_MAP_WEAK;
extern void    *ov021_FIELDMAP_GetFieldPlayer(void *fieldMap) POKEBW_MAP_WEAK;
extern int      ov021_FIELDMAP_IsFieldPlayerCreated(void *fieldMap) POKEBW_MAP_WEAK;
extern void    *ov021_FIELDMAP_GetG3dMapper(void *fieldMap) POKEBW_MAP_WEAK;
extern void    *ov021_FIELDMAP_GetNogridMapper(void *fieldMap) POKEBW_MAP_WEAK;
extern void     ov021_FIELD_PLAYER_GetPos(void *player, struct PokebwMapVecFx32 *out) POKEBW_MAP_WEAK;
extern uint16_t ov021_FIELD_PLAYER_GetDirDisp(void *player) POKEBW_MAP_WEAK;
extern int      ov021_FIELD_G3D_MAPPER_QueryNearestHit(
    void *mapper, const struct PokebwMapVecFx32 *pos, struct PokebwMapHit *outHit) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsPresent(uint32_t attr) POKEBW_MAP_WEAK;
extern uint16_t ov021_MAP_ATTR_GetFlags(uint32_t attr) POKEBW_MAP_WEAK;
extern uint16_t ov021_MAP_ATTR_GetTerrainId(uint32_t attr) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsAnyDoubleGrass(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsAnySingleGrass(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsAnySand(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsSnowTile(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsLedgeNorth(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsLedgeSouth(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsLedgeEast(uint16_t terrainId) POKEBW_MAP_WEAK;
extern int      ov021_MAP_ATTR_IsLedgeWest(uint16_t terrainId) POKEBW_MAP_WEAK;
extern void    *ov021_FIELD_NOGRID_MAPPER_GetRailMan(void *mapper) POKEBW_MAP_WEAK;
extern void     ov021_FIELD_NOGRID_MAPPER_CalcZoneRailLocation(
    void *mapper, uint32_t zoneId, uint16_t railIndex, uint16_t lineGrid, uint16_t widthPos,
    struct PokebwMapRailLoc *outLoc, uint16_t heapId) POKEBW_MAP_WEAK;
extern void     ov021_FIELD_RAIL_GetLocationPos(
    void *railMan, struct PokebwMapRailLoc *loc, struct PokebwMapVecFx32 *outPos) POKEBW_MAP_WEAK;

/* Operand byte lengths for field script opcodes 0..608 (tools/assets/scripts.py). */
static const uint8_t k_field_op_width[609] = {
      0,   0,   0,   2,   4,   0,   2,   2,   2,   2,   2,   0,   0,   0,   0,   0,
      2,   2,   4,   4,   0,   4,   0,   0,   0,   4,   4,   6,  10,   0,   4,   5,
      5,   2,   2,   2,   2,   4,   4,   4,   4,   4,   4,   4,   4,   4,   0,   0,
      0,   0,   0,   2,   4,   4,   0,   2,   3,   0,   8,   2,  10,   8,   0,   0,
      4,   0,   0,   4,   0,   4,   0,   2,  12,  12,   3,   0,   1,   3,   6,   3,
      2,   2,   2,   2,   2,   2,   2,   3,   3,   2,   2,   2,   4,   2,   2,   2,
      2,   3,   3,   2,   6,   0,   4,   6,   4,  12,   4,   2,   2,  10,   2,   4,
     10,   6,   8,   4,   0,   2,   8,   0,   2,   6,   2,   8,   6,   8,   2,   4,
      2,   0,   4,   2,   2,   6,   8,   6,   6, 255,   4,   2,   0,   2,   0, 255,
    255, 255,   4,   4,   8,   2,   2,   4,   2, 255, 255,   4, 255, 255,   0,   2
};

static PokebwMapSnapshot g_map_snap;
static uint16_t g_cached_zone = 0xFFFFu;
static int16_t g_cached_origin_x = 0;
static int16_t g_cached_origin_z = 0;
static int g_cached_valid = 0;
static uint8_t g_cached_tiles[POKEBW_MAP_MAX_W * POKEBW_MAP_MAX_H];

static uint16_t rd16_le(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t rd32_le(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static int fx_to_grid(int32_t v)
{
    return (v >= 0) ? (v / TILE_FX) : -(((-v) + TILE_FX - 1) / TILE_FX);
}

static int resolve_rail_grid(
    void *nogridMapper, uint16_t zone_id,
    uint16_t rail_idx, uint16_t line_grid, int16_t width_grid,
    int16_t *out_gx, int16_t *out_gz)
{
    struct PokebwMapRailLoc loc;
    struct PokebwMapVecFx32 pos;
    void *railMan;

    if (nogridMapper == NULL ||
        ov021_FIELD_NOGRID_MAPPER_GetRailMan == NULL ||
        ov021_FIELD_NOGRID_MAPPER_CalcZoneRailLocation == NULL ||
        ov021_FIELD_RAIL_GetLocationPos == NULL) {
        return 0;
    }
    railMan = ov021_FIELD_NOGRID_MAPPER_GetRailMan(nogridMapper);
    if (railMan == NULL) {
        return 0;
    }
    memset(&loc, 0, sizeof(loc));
    memset(&pos, 0, sizeof(pos));
    ov021_FIELD_NOGRID_MAPPER_CalcZoneRailLocation(
        nogridMapper, (uint32_t)zone_id, rail_idx, line_grid,
        (uint16_t)width_grid, &loc, (uint16_t)BW_TEMP_HEAP);
    ov021_FIELD_RAIL_GetLocationPos(railMan, &loc, &pos);
    *out_gx = (int16_t)fx_to_grid(pos.x);
    *out_gz = (int16_t)fx_to_grid(pos.z);
    return 1;
}

static uint8_t classify_tile(void *mapper, int16_t gx, int16_t gz, int32_t ref_y)
{
    struct PokebwMapVecFx32 qpos;
    struct PokebwMapHit hit;
    uint32_t attr;
    uint16_t flags, terrain;

    if (mapper == NULL ||
        ov021_FIELD_G3D_MAPPER_QueryNearestHit == NULL ||
        ov021_MAP_ATTR_IsPresent == NULL ||
        ov021_MAP_ATTR_GetFlags == NULL ||
        ov021_MAP_ATTR_GetTerrainId == NULL) {
        return POKEBW_TILE_VOID;
    }

    qpos.x = ((int32_t)gx * TILE_FX) + (TILE_FX / 2);
    qpos.y = ref_y;
    qpos.z = ((int32_t)gz * TILE_FX) + (TILE_FX / 2);
    memset(&hit, 0, sizeof(hit));
    if (ov021_FIELD_G3D_MAPPER_QueryNearestHit(mapper, &qpos, &hit) == 0) {
        return POKEBW_TILE_VOID;
    }
    attr = (uint32_t)hit.attr;
    if (ov021_MAP_ATTR_IsPresent(attr) == 0) {
        return POKEBW_TILE_VOID;
    }
    flags = ov021_MAP_ATTR_GetFlags(attr);
    terrain = ov021_MAP_ATTR_GetTerrainId(attr);

    if ((flags & 2u) != 0u) {
        return POKEBW_TILE_WATER;
    }
    if ((ov021_MAP_ATTR_IsLedgeNorth != NULL && ov021_MAP_ATTR_IsLedgeNorth(terrain)) ||
        (ov021_MAP_ATTR_IsLedgeSouth != NULL && ov021_MAP_ATTR_IsLedgeSouth(terrain)) ||
        (ov021_MAP_ATTR_IsLedgeEast != NULL && ov021_MAP_ATTR_IsLedgeEast(terrain)) ||
        (ov021_MAP_ATTR_IsLedgeWest != NULL && ov021_MAP_ATTR_IsLedgeWest(terrain))) {
        return POKEBW_TILE_LEDGE;
    }
    if ((flags & 1u) != 0u) {
        return POKEBW_TILE_BLOCKED;
    }
    if (ov021_MAP_ATTR_IsAnyDoubleGrass != NULL && ov021_MAP_ATTR_IsAnyDoubleGrass(terrain)) {
        return POKEBW_TILE_DARK_GRASS;
    }
    if (ov021_MAP_ATTR_IsAnySingleGrass != NULL && ov021_MAP_ATTR_IsAnySingleGrass(terrain)) {
        return POKEBW_TILE_GRASS;
    }
    if ((ov021_MAP_ATTR_IsAnySand != NULL && ov021_MAP_ATTR_IsAnySand(terrain)) ||
        (ov021_MAP_ATTR_IsSnowTile != NULL && ov021_MAP_ATTR_IsSnowTile(terrain))) {
        return POKEBW_TILE_SAND_SNOW;
    }
    return POKEBW_TILE_WALKABLE;
}

static void refresh_terrain(void *mapper, uint16_t zone_id, int16_t player_gx, int16_t player_gz,
                            int32_t player_y, int force_full)
{
    int dx, dz;
    int need_resample = 0;

    if (!g_cached_valid || force_full || zone_id != g_cached_zone) {
        need_resample = 1;
    } else {
        int cx = (int)g_cached_origin_x + (POKEBW_MAP_MAX_W / 2);
        int cz = (int)g_cached_origin_z + (POKEBW_MAP_MAX_H / 2);
        int dist_x = (int)player_gx - cx;
        int dist_z = (int)player_gz - cz;
        if (dist_x < -4 || dist_x > 4 || dist_z < -4 || dist_z > 4) {
            need_resample = 1;
        }
    }

    if (need_resample) {
        g_cached_zone = zone_id;
        g_cached_origin_x = (int16_t)(player_gx - (POKEBW_MAP_MAX_W / 2));
        g_cached_origin_z = (int16_t)(player_gz - (POKEBW_MAP_MAX_H / 2));
        g_cached_valid = 1;
        for (dz = 0; dz < POKEBW_MAP_MAX_H; dz++) {
            for (dx = 0; dx < POKEBW_MAP_MAX_W; dx++) {
                g_cached_tiles[dz * POKEBW_MAP_MAX_W + dx] = classify_tile(
                    mapper,
                    (int16_t)(g_cached_origin_x + dx),
                    (int16_t)(g_cached_origin_z + dz),
                    player_y);
            }
        }
    }

    g_map_snap.origin_x = g_cached_origin_x;
    g_map_snap.origin_z = g_cached_origin_z;
    g_map_snap.grid_w = POKEBW_MAP_MAX_W;
    g_map_snap.grid_h = POKEBW_MAP_MAX_H;
    memcpy(g_map_snap.tiles, g_cached_tiles, sizeof(g_cached_tiles));
}

struct ScriptCache {
    uint16_t file_id;
    uint8_t *buf;
    uint32_t size;
};

static void script_cache_free(struct ScriptCache *sc)
{
    if (sc->buf != NULL && GFL_HEAP_Free != NULL) {
        GFL_HEAP_Free(sc->buf);
    }
    sc->buf = NULL;
    sc->size = 0u;
    sc->file_id = 0xFFFFu;
}

static int decode_script_item(
    struct ScriptCache *sc, uint16_t zone_id, uint16_t script_id,
    uint16_t *out_item_id, uint8_t *out_qty)
{
    uint16_t file_id = 0u, txt_bank = 0u, txt_msg = 0u;
    uint16_t entry_idx;
    uint32_t pc;
    uint16_t work0 = 0u, work1 = 0u;
    int steps;

    *out_item_id = 0u;
    *out_qty = 1u;

    if (script_id == 0u || script_id == 0xFFFFu ||
        ov010_SCRIPT_SYS_IsZoneScriptId == NULL ||
        ov010_SCRIPT_SYS_GetScriptFileId == NULL ||
        ARC_TOOL_GetMemberSize == NULL ||
        ARC_TOOL_LoadMember == NULL ||
        GFL_HEAP_Free == NULL) {
        return 0;
    }
    if (ov010_SCRIPT_SYS_IsZoneScriptId(script_id) == 0) {
        return 0;
    }

    entry_idx = ov010_SCRIPT_SYS_GetScriptFileId((uint32_t)zone_id, (uint32_t)script_id,
                                                 &file_id, &txt_bank, &txt_msg);
    (void)txt_bank;
    (void)txt_msg;

    if (sc->buf == NULL || sc->file_id != file_id) {
        uint32_t sz;
        script_cache_free(sc);
        sz = ARC_TOOL_GetMemberSize(BW_SCRIPT_ARC, (uint32_t)file_id);
        if (sz < 8u || sz > 262144u) {
            return 0;
        }
        sc->buf = (uint8_t *)ARC_TOOL_LoadMember(BW_SCRIPT_ARC, (uint32_t)file_id, (uint16_t)BW_TEMP_HEAP);
        if (sc->buf == NULL) {
            return 0;
        }
        sc->file_id = file_id;
        sc->size = sz;
    }

    {
        uint32_t hdr_off = (uint32_t)entry_idx * 4u;
        uint32_t rel;
        if (hdr_off + 4u > sc->size) {
            return 0;
        }
        rel = rd32_le(sc->buf + hdr_off);
        pc = hdr_off + 4u + rel;
        if (pc + 2u > sc->size) {
            return 0;
        }
    }

    for (steps = 0; steps < 12 && pc + 2u <= sc->size; steps++) {
        uint16_t op = rd16_le(sc->buf + pc);
        pc += 2u;

        if (op == 0x0002u || op == 0x001Du || op == 0x001Eu) {
            break;
        }
        if ((op == 40u || op == 42u) && pc + 4u <= sc->size) {
            uint16_t var_id = rd16_le(sc->buf + pc);
            uint16_t val = rd16_le(sc->buf + pc + 2u);
            if (var_id == 0x8000u) {
                work0 = val;
            } else if (var_id == 0x8001u) {
                work1 = val;
            }
            pc += 4u;
            continue;
        }
        /* Known safe early opcodes we can skip past cleanly. */
        if (op < 160u && k_field_op_width[op] != 255u) {
            uint32_t w = k_field_op_width[op];
            if (pc + w > sc->size) {
                break;
            }
            pc += w;
            continue;
        }
        /* Common item/give common-script opcodes (call std / common). */
        if (work0 != 0u) {
            break;
        }
        break;
    }

    if (work0 == 0u || work0 >= 700u) {
        return 0;
    }
    *out_item_id = work0;
    if (work1 >= 1u && work1 <= 99u) {
        *out_qty = (uint8_t)work1;
    }
    return 1;
}

static void format_item_name(uint16_t item_id, uint8_t kind, char *out, int cap)
{
    if (item_id != 0u && pokebw_gear_msg_line(MSG_ITEM_BANK, (uint32_t)item_id, out, cap)) {
        return;
    }
    if (kind == POKEBW_ITEM_BALL) {
        snprintf(out, (size_t)cap, "Item Ball");
    } else {
        snprintf(out, (size_t)cap, "Hidden Item");
    }
}

static void add_item_entry(
    int16_t gx, int16_t gz, uint16_t item_id, uint16_t script_id, uint16_t flag_id,
    uint8_t kind, uint8_t collected, uint8_t qty, uint8_t on_rail)
{
    PokebwMapItem *it;

    if (g_map_snap.item_count >= POKEBW_MAP_MAX_ITEMS) {
        return;
    }
    it = &g_map_snap.items[g_map_snap.item_count++];
    memset(it, 0, sizeof(*it));
    it->gx = gx;
    it->gz = gz;
    it->item_id = item_id;
    it->script_id = script_id;
    it->flag_id = flag_id;
    it->kind = kind;
    it->collected = collected;
    it->qty = qty;
    it->on_rail = on_rail;
    format_item_name(item_id, kind, it->name, (int)sizeof(it->name));

    if (!collected) {
        if (kind == POKEBW_ITEM_BALL) {
            if (g_map_snap.uncollected_balls < 255u) {
                g_map_snap.uncollected_balls++;
            }
        } else {
            if (g_map_snap.uncollected_hidden < 255u) {
                g_map_snap.uncollected_hidden++;
            }
        }
    }
}

void pokebw_area_map_on_field_tick(void *gameSystem, void *fieldMap)
{
    uint32_t req;
    int force_full;
    void *gd, *evWork, *player, *mapper, *nogrid;
    const struct PokebwMapEventDataSys *eds;
    struct PokebwMapVecFx32 ppos;
    uint16_t zone_id, place_id, tbl_cnt = 0u;
    int16_t p_gx, p_gz;
    const struct PokebwMapHiddenEntry *tbl;
    struct ScriptCache sc;
    uint16_t i;

    if (gameSystem == NULL || fieldMap == NULL) {
        return;
    }

    req = bw_host_map_request_poll();
    if (req == 0u) {
        return;
    }
    force_full = (req >= 2u) ? 1 : 0;

    if (GAMESYSTEM_GetGameData == NULL ||
        GAME_DATA_GetEventData == NULL ||
        GAME_DATA_GetEventWork == NULL ||
        ov021_FIELDMAP_GetZoneId == NULL ||
        ov021_FIELDMAP_IsFieldPlayerCreated == NULL ||
        ov021_FIELDMAP_GetFieldPlayer == NULL ||
        ov021_FIELD_PLAYER_GetPos == NULL) {
        return;
    }
    if (ov021_FIELDMAP_IsFieldPlayerCreated(fieldMap) == 0) {
        return;
    }

    gd = GAMESYSTEM_GetGameData(gameSystem);
    if (gd == NULL) {
        return;
    }
    evWork = GAME_DATA_GetEventWork(gd);
    eds = (const struct PokebwMapEventDataSys *)GAME_DATA_GetEventData(gd);
    player = ov021_FIELDMAP_GetFieldPlayer(fieldMap);
    if (player == NULL || eds == NULL) {
        return;
    }

    zone_id = ov021_FIELDMAP_GetZoneId(fieldMap);
    ov021_FIELD_PLAYER_GetPos(player, &ppos);
    p_gx = (int16_t)fx_to_grid(ppos.x);
    p_gz = (int16_t)fx_to_grid(ppos.z);

    memset(&g_map_snap, 0, sizeof(g_map_snap));
    g_map_snap.valid = 1u;
    g_map_snap.zone_id = zone_id;
    g_map_snap.player_gx = p_gx;
    g_map_snap.player_gz = p_gz;
    g_map_snap.player_dir = (ov021_FIELD_PLAYER_GetDirDisp != NULL)
                                ? (uint8_t)(ov021_FIELD_PLAYER_GetDirDisp(player) & 3u)
                                : 1u;
    g_map_snap.is_rail_map = (ZONEDATA_IsRailMap != NULL && ZONEDATA_IsRailMap(zone_id) != 0u) ? 1u : 0u;

    place_id = (ZONEDATA_GetPlaceNameId != NULL) ? (uint16_t)ZONEDATA_GetPlaceNameId(zone_id) : 0u;
    if (!pokebw_gear_msg_line(MSG_PLACE_BANK, (uint32_t)place_id,
                              g_map_snap.zone_name, (int)sizeof(g_map_snap.zone_name))) {
        snprintf(g_map_snap.zone_name, sizeof(g_map_snap.zone_name), "Zone %u", (unsigned)zone_id);
    }

    mapper = (ov021_FIELDMAP_GetG3dMapper != NULL) ? ov021_FIELDMAP_GetG3dMapper(fieldMap) : NULL;
    nogrid = (ov021_FIELDMAP_GetNogridMapper != NULL) ? ov021_FIELDMAP_GetNogridMapper(fieldMap) : NULL;
    refresh_terrain(mapper, zone_id, p_gx, p_gz, ppos.y, force_full);

    memset(&sc, 0, sizeof(sc));
    tbl = (ov010_HIDDEN_ITEM_GetTable != NULL) ? ov010_HIDDEN_ITEM_GetTable(&tbl_cnt) : NULL;

    /* 1. Visible Item Balls in current EventDataSystem NPC table (scriptId 7000..7999) */
    if (eds->npcHeaders != NULL) {
        for (i = 0u; i < eds->npcCount && g_map_snap.item_count < POKEBW_MAP_MAX_ITEMS; i++) {
            const struct PokebwMapMmdlHdr *h = &eds->npcHeaders[i];
            int16_t gx = 0, gz = 0;
            uint8_t on_rail = 0u, collected = 0u, qty = 1u;
            uint16_t item_id = 0u;

            if (h->eventId < 7000u || h->eventId > 7999u) {
                continue;
            }
            if (h->posType == 0) {
                gx = (int16_t)h->pos[0];
                gz = (int16_t)h->pos[1];
            } else {
                on_rail = 1u;
                if (!resolve_rail_grid(nogrid, zone_id, h->pos[0], h->pos[1], (int16_t)h->pos[2], &gx, &gz)) {
                    continue;
                }
            }
            if (evWork != NULL && h->eventFlag != 0u && ZONEDATA_CheckEventFlag != NULL) {
                collected = (ZONEDATA_CheckEventFlag(evWork, (int32_t)h->eventFlag) != 0u) ? 1u : 0u;
            }
            (void)decode_script_item(&sc, zone_id, h->eventId, &item_id, &qty);
            add_item_entry(gx, gz, item_id, h->eventId, h->eventFlag,
                           POKEBW_ITEM_BALL, collected, qty, on_rail);
        }
    }

    /* 2. Hidden Dowsing Machine items in current zone bgEvents (type == 2) */
    if (eds->bgEvents != NULL) {
        for (i = 0u; i < eds->bgCount && g_map_snap.item_count < POKEBW_MAP_MAX_ITEMS; i++) {
            const struct PokebwMapBgEvent *bg = &eds->bgEvents[i];
            int16_t gx = 0, gz = 0;
            uint8_t on_rail = 0u, collected = 0u, qty = 1u, kind = POKEBW_ITEM_HIDDEN;
            uint16_t flag_id = 0u, item_id = 0u;

            if (bg->type != 2u) {
                continue;
            }
            if (bg->posType == 0) {
                gx = (int16_t)bg->word[0];
                gz = (int16_t)bg->word[1];
            } else {
                on_rail = 1u;
                if (!resolve_rail_grid(nogrid, zone_id,
                                       (uint16_t)(bg->word[0] & 0xFFFF),
                                       (uint16_t)((bg->word[0] >> 16) & 0xFFFF),
                                       (int16_t)(bg->word[1] & 0xFFFF),
                                       &gx, &gz)) {
                    continue;
                }
            }
            if (ov010_HIDDEN_ITEM_GetFlag != NULL) {
                flag_id = ov010_HIDDEN_ITEM_GetFlag(bg->scriptId);
            }
            if (evWork != NULL && flag_id != 0u && ZONEDATA_CheckEventFlag != NULL) {
                collected = (ZONEDATA_CheckEventFlag(evWork, (int32_t)flag_id) != 0u) ? 1u : 0u;
            }
            if (tbl != NULL && ov010_HIDDEN_ITEM_GetIndex != NULL) {
                uint16_t idx = ov010_HIDDEN_ITEM_GetIndex((uint32_t)bg->scriptId);
                if (idx < tbl_cnt && tbl[idx].respawns != 0u) {
                    kind = POKEBW_ITEM_HIDDEN_DAILY;
                }
            }
            (void)decode_script_item(&sc, zone_id, bg->scriptId, &item_id, &qty);
            add_item_entry(gx, gz, item_id, bg->scriptId, flag_id,
                           kind, collected, qty, on_rail);
        }
    }

    /* 3. Include any matching-zone HiddenItemTable entries not already listed */
    if (tbl != NULL) {
        for (i = 0u; i < tbl_cnt && g_map_snap.item_count < POKEBW_MAP_MAX_ITEMS; i++) {
            uint16_t flag_id;
            uint8_t j, dup = 0u, collected = 0u;
            if (tbl[i].zoneId != zone_id) {
                continue;
            }
            flag_id = (uint16_t)(tbl[i].flagIndex + HIDDEN_ITEM_FLAG_BASE);
            for (j = 0u; j < g_map_snap.item_count; j++) {
                if (g_map_snap.items[j].kind != POKEBW_ITEM_BALL &&
                    g_map_snap.items[j].flag_id == flag_id) {
                    dup = 1u;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            if (evWork != NULL && ZONEDATA_CheckEventFlag != NULL) {
                collected = (ZONEDATA_CheckEventFlag(evWork, (int32_t)flag_id) != 0u) ? 1u : 0u;
            }
            add_item_entry((int16_t)tbl[i].gridX, (int16_t)tbl[i].gridZ,
                           0u, 0u, flag_id,
                           tbl[i].respawns ? POKEBW_ITEM_HIDDEN_DAILY : POKEBW_ITEM_HIDDEN,
                           collected, 1u, 0u);
        }
    }

    script_cache_free(&sc);

    /* 4. Zone warps/exits */
    if (eds->connects != NULL) {
        for (i = 0u; i < eds->connectCount && g_map_snap.warp_count < POKEBW_MAP_MAX_WARPS; i++) {
            const struct PokebwMapConnect *c = &eds->connects[i];
            PokebwMapWarp *w;
            int16_t gx = 0, gz = 0;
            uint8_t sx = 1u, sz = 1u;

            if (c->zoneId == 0xFFFFu) {
                continue;
            }
            if (c->posType == 0u) {
                /* ConnectGridArea x/z are 1/16th-tile units (<< 12 -> fx32). */
                gx = (int16_t)(c->area.grid.x >> 4);
                gz = (int16_t)(c->area.grid.z >> 4);
                sx = c->area.grid.sizeX > 255u ? 255u : (c->area.grid.sizeX > 0u ? (uint8_t)c->area.grid.sizeX : 1u);
                sz = c->area.grid.sizeZ > 255u ? 255u : (c->area.grid.sizeZ > 0u ? (uint8_t)c->area.grid.sizeZ : 1u);
            } else {
                if (!resolve_rail_grid(nogrid, zone_id,
                                       c->area.rail.railIndex,
                                       c->area.rail.lineGrid,
                                       c->area.rail.widthGrid,
                                       &gx, &gz)) {
                    continue;
                }
            }
            w = &g_map_snap.warps[g_map_snap.warp_count++];
            w->gx = gx;
            w->gz = gz;
            w->size_x = sx;
            w->size_z = sz;
            w->dest_zone = c->zoneId;
        }
    }

    bw_host_map_push(&g_map_snap, (unsigned)sizeof(g_map_snap));
}

#else

void pokebw_area_map_on_field_tick(void *gameSystem, void *fieldMap)
{
    (void)gameSystem;
    (void)fieldMap;
}

#endif
