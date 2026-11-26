/*
 * sdk/pokebw_map_state.h -- Fixed-layout, pointer-free ABI shared between
 * the wasm32 area item map collector (sdk/pokebw_area_map.c) and the 64-bit
 * native host overlay (host/pokebw_settings.c + host/imgui_overlay.cpp).
 */

#ifndef POKEBW_MAP_STATE_H
#define POKEBW_MAP_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POKEBW_MAP_MAX_W     64
#define POKEBW_MAP_MAX_H     64
#define POKEBW_MAP_MAX_ITEMS 64
#define POKEBW_MAP_MAX_WARPS 32

enum {
    POKEBW_TILE_VOID       = 0,
    POKEBW_TILE_BLOCKED    = 1,
    POKEBW_TILE_WALKABLE   = 2,
    POKEBW_TILE_GRASS      = 3,
    POKEBW_TILE_DARK_GRASS = 4,
    POKEBW_TILE_WATER      = 5,
    POKEBW_TILE_SAND_SNOW  = 6,
    POKEBW_TILE_LEDGE      = 7
};

enum {
    POKEBW_ITEM_BALL         = 0, /* Visible Poké Ball on map (eventId 7000..7999) */
    POKEBW_ITEM_HIDDEN       = 1, /* Hidden Dowsing Machine item (bgEvent type 2 / table) */
    POKEBW_ITEM_HIDDEN_DAILY = 2  /* Hidden Dowsing Machine item with daily respawn */
};

typedef struct {
    int16_t  gx;
    int16_t  gz;
    uint16_t item_id;
    uint16_t script_id;
    uint16_t flag_id;
    uint8_t  kind;      /* POKEBW_ITEM_* */
    uint8_t  collected; /* 1 if already picked up, 0 if still present */
    uint8_t  qty;       /* usually 1 */
    uint8_t  on_rail;   /* 1 if resolved from rail coordinates */
    uint16_t pad;
    char     name[28];
} PokebwMapItem;

typedef struct {
    int16_t  gx;
    int16_t  gz;
    uint8_t  size_x;
    uint8_t  size_z;
    uint16_t dest_zone;
} PokebwMapWarp;

typedef struct {
    uint32_t      valid;
    uint16_t      zone_id;
    int16_t       player_gx;
    int16_t       player_gz;
    uint8_t       player_dir; /* 0=N, 1=S, 2=W, 3=E */
    uint8_t       is_rail_map;
    int16_t       origin_x;
    int16_t       origin_z;
    uint8_t       grid_w;
    uint8_t       grid_h;
    uint8_t       item_count;
    uint8_t       uncollected_balls;
    uint8_t       uncollected_hidden;
    uint8_t       warp_count;
    uint8_t       pad[2];
    char          zone_name[48];
    uint8_t       tiles[POKEBW_MAP_MAX_W * POKEBW_MAP_MAX_H];
    PokebwMapItem items[POKEBW_MAP_MAX_ITEMS];
    PokebwMapWarp warps[POKEBW_MAP_MAX_WARPS];
} PokebwMapSnapshot;

#ifdef __cplusplus
}
#endif

#endif /* POKEBW_MAP_STATE_H */
