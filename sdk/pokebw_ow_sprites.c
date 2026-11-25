/*
 * sdk/pokebw_ow_sprites.c -- Phase 3.1: 649-Species Overworld Sprite Archive.
 *
 * Reads optional custom sprite packs (`pokebw_ow_sprites.bwsp`, produced by
 * tools/pack_ow_sprites.py) from BW_OW_SPRITE_PACK, <save_dir>, <app_home>,
 * or the extract parent folder, and injects BTX0 billboards for all 649
 * species (normal + shiny + alternate forms from a/0/1/6) into a/0/4/9 via
 * bw_vfs_mem_set("", "a/0/4/9", ...).
 *
 * Any species/form/shiny entry not supplied by a custom .bwsp pack is
 * synthesized automatically at boot from the player's own cartridge icons
 * (a/0/0/7) into a valid 32x256 8-frame animId=14 BTX0 billboard so all 649
 * species render out-of-the-box without external files.
 *
 * Linked into eidos_boot only (CMakeLists.txt).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokebw_ow_sprites.h"
#include "port.h"
#include "save.h"
#include "vfs.h"

#if defined(__wasm__)
#include "wasm_host.h"
#endif

#define BWSP_MAGIC_0 'B'
#define BWSP_MAGIC_1 'W'
#define BWSP_MAGIC_2 'S'
#define BWSP_MAGIC_3 'P'
#define BWSP_VERSION 1u
#define BWSP_HEADER_SIZE 16u
#define BWSP_ENTRY_SIZE  20u

#define OW_MAX_SLOTS       2048u
#define PARAM_RECORD_SIZE  0x1Cu
#define WALK_TEMPLATE_CODE 1u
#define ANIM_ID_8FRAME     14u

#define ICON_PAL_ADDR      0x0209E474u
#define ICON_PAL_BYTES     0x300u
#define ICON_MEMBERS       1431u

#define BTX_W              32u
#define BTX_H              256u
#define BTX_TEXEL_BYTES    ((BTX_W * BTX_H) / 2u) /* 4096 */
#define BTX_PAL_BYTES      32u
#define BTX_TEX_DICT_SIZE  44u
#define BTX_PAL_DICT_SIZE  40u
#define BTX_TEX0_HDR_SIZE  0x3Cu
#define BTX_OFS_TEX        (BTX_TEX0_HDR_SIZE + BTX_TEX_DICT_SIZE + BTX_PAL_DICT_SIZE) /* 0x90 */
#define BTX_OFS_PAL        (BTX_OFS_TEX + BTX_TEXEL_BYTES)
#define BTX_TEX0_SIZE      (BTX_OFS_PAL + BTX_PAL_BYTES)
#define BTX_TOTAL_SIZE     (0x14u + BTX_TEX0_SIZE) /* 4292 */

/* ABI-matched view of aletheia/src/game/ov010/fldmmdl_types.h MmdlObjCodeParam. */
struct PokebwMmdlObjCodeParam {
    uint16_t code;          /* 0x00 */
    uint8_t  drawType;      /* 0x02 */
    uint8_t  drawProcNo;    /* 0x03 */
    uint8_t  shadowSize;    /* 0x04 */
    uint8_t  footMarkType;  /* 0x05 */
    uint8_t  reflectType;   /* 0x06 */
    uint8_t  mdlSize;       /* 0x07 */
    uint8_t  texSize;       /* 0x08 */
    uint8_t  animId;        /* 0x09 */
    uint8_t  sex;           /* 0x0A */
    uint8_t  sizeWidth;     /* 0x0B */
    uint8_t  sizeDepth;     /* 0x0C */
    int8_t   offsetX;       /* 0x0D */
    int8_t   offsetY;       /* 0x0E */
    int8_t   offsetZ;       /* 0x0F */
    uint8_t  resource[12];  /* 0x10 */
};

typedef char pokebw_mmdl_param_size_check[
    sizeof(struct PokebwMmdlObjCodeParam) == PARAM_RECORD_SIZE ? 1 : -1];

struct PokebwOwEntry {
    uint16_t species;
    uint8_t  form;
    uint8_t  shiny;
    uint8_t  custom;
    uint8_t  active;
    uint16_t obj_code;
    struct PokebwMmdlObjCodeParam param;
};

static struct PokebwOwEntry g_entries[OW_MAX_SLOTS];
static uint32_t g_entry_count = 0u;
static uint32_t g_custom_count = 0u;
static uint32_t g_form_count = 0u;
static int g_registered = 0;

/* Fast lookup for normal/shiny base forms (1..649). */
static uint16_t g_base_objcode[POKEBW_OW_SPECIES_MAX + 1u][2];

extern void __real_ov010_FLDMMDL_LoadObjCodeParam(const void *sys, uint32_t objCode, void *out);

static uint16_t rd16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint8_t *read_arc_member(BwArcFile *arc, uint32_t member, uint32_t *out_size)
{
    long n;
    uint8_t *buf;

    if (arc == NULL || member >= arc->count) {
        return NULL;
    }
    n = bw_arc_member_size(arc, member);
    if (n <= 0) {
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL) {
        return NULL;
    }
    if (bw_arc_read_member(arc, member, buf, (uint32_t)n) != n) {
        free(buf);
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = (uint32_t)n;
    }
    return buf;
}

static uint8_t *read_file_bytes(const char *path, uint32_t *out_size)
{
    FILE *f;
    long n;
    uint8_t *buf;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        n > (64L * 1024L * 1024L) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size != NULL) {
        *out_size = (uint32_t)n;
    }
    return buf;
}

static uint8_t *load_bwsp_pack(uint32_t *out_size)
{
    const char *env = getenv("BW_OW_SPRITE_PACK");
    const char *save_dir = bw_save_dir();
    const char *data_root = bw_data_root();
    char path[1152];
    uint8_t *buf;

    if (env != NULL && env[0] != '\0') {
        if ((buf = read_file_bytes(env, out_size)) != NULL) {
            fprintf(stderr, "eidos: ow_sprites: loaded %s (%u bytes)\n",
                    env, out_size != NULL ? (unsigned)*out_size : 0u);
            return buf;
        }
    }
    if (save_dir != NULL && save_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/pokebw_ow_sprites.bwsp", save_dir);
        if ((buf = read_file_bytes(path, out_size)) != NULL) {
            fprintf(stderr, "eidos: ow_sprites: loaded %s (%u bytes)\n",
                    path, out_size != NULL ? (unsigned)*out_size : 0u);
            return buf;
        }
    }
#if defined(__wasm__)
    {
        char app_home[1024];
        if (bw_host_os_path(BW_HOST_PATH_APP_HOME, app_home, (unsigned)sizeof(app_home)) == 0 &&
            app_home[0] != '\0') {
            snprintf(path, sizeof(path), "%s/pokebw_ow_sprites.bwsp", app_home);
            if ((buf = read_file_bytes(path, out_size)) != NULL) {
                fprintf(stderr, "eidos: ow_sprites: loaded %s (%u bytes)\n",
                        path, out_size != NULL ? (unsigned)*out_size : 0u);
                return buf;
            }
        }
    }
#endif
    if (data_root != NULL && data_root[0] != '\0') {
        snprintf(path, sizeof(path), "%s/../pokebw_ow_sprites.bwsp", data_root);
        if ((buf = read_file_bytes(path, out_size)) != NULL) {
            fprintf(stderr, "eidos: ow_sprites: loaded %s (%u bytes)\n",
                    path, out_size != NULL ? (unsigned)*out_size : 0u);
            return buf;
        }
        snprintf(path, sizeof(path), "%s/../../pokebw_ow_sprites.bwsp", data_root);
        if ((buf = read_file_bytes(path, out_size)) != NULL) {
            fprintf(stderr, "eidos: ow_sprites: loaded %s (%u bytes)\n",
                    path, out_size != NULL ? (unsigned)*out_size : 0u);
            return buf;
        }
    }
    return NULL;
}

static int find_entry_slot(uint16_t species, uint8_t form, int shiny)
{
    uint32_t i;

    if (species == 0u || species > POKEBW_OW_SPECIES_MAX) {
        return -1;
    }
    if (form == 0u && g_base_objcode[species][shiny ? 1 : 0] != 0u) {
        return (int)(g_base_objcode[species][shiny ? 1 : 0] - POKEBW_OW_OBJCODE_BASE);
    }
    for (i = 0u; i < g_entry_count; i++) {
        if (g_entries[i].active &&
            g_entries[i].species == species &&
            g_entries[i].form == form &&
            g_entries[i].shiny == (uint8_t)(shiny ? 1 : 0)) {
            return (int)i;
        }
    }
    return -1;
}

static void build_synthesized_btx0(
    const uint8_t *icon_chr,
    const uint8_t *pal_nclr,
    uint32_t pal_size,
    uint32_t pal_bank,
    int shiny,
    uint8_t out[BTX_TOTAL_SIZE])
{
    static const uint8_t frame_flip[8] = { 1, 1, 0, 0, 0, 0, 1, 1 };
    static const uint8_t frame_step[8] = { 0, 1, 0, 1, 0, 1, 0, 1 };
    uint8_t frame_px[2][32][32];
    uint8_t *tex0 = out + 0x14u;
    uint8_t *td = tex0 + BTX_TEX0_HDR_SIZE;
    uint8_t *pd = td + BTX_TEX_DICT_SIZE;
    uint8_t *texels = tex0 + BTX_OFS_TEX;
    uint8_t *pal_out = tex0 + BTX_OFS_PAL;
    uint32_t f, ty, tx, py, px, i;

    memset(out, 0, BTX_TOTAL_SIZE);

    /* BTX0 header */
    out[0] = 'B'; out[1] = 'T'; out[2] = 'X'; out[3] = '0';
    wr16_le(out + 0x04u, 0xFEFFu);
    wr16_le(out + 0x06u, 0x0001u);
    wr32_le(out + 0x08u, BTX_TOTAL_SIZE);
    wr16_le(out + 0x0Cu, 0x0010u);
    wr16_le(out + 0x0Eu, 0x0001u);
    wr32_le(out + 0x10u, 0x0014u);

    /* TEX0 header */
    tex0[0] = 'T'; tex0[1] = 'E'; tex0[2] = 'X'; tex0[3] = '0';
    wr32_le(tex0 + 0x04u, BTX_TEX0_SIZE);
    wr16_le(tex0 + 0x0Cu, (uint16_t)(BTX_TEXEL_BYTES >> 3));
    wr16_le(tex0 + 0x0Eu, BTX_TEX0_HDR_SIZE);
    wr32_le(tex0 + 0x14u, BTX_OFS_TEX);
    wr16_le(tex0 + 0x1Eu, BTX_TEX0_HDR_SIZE);
    wr32_le(tex0 + 0x24u, BTX_OFS_PAL);
    wr32_le(tex0 + 0x28u, BTX_OFS_PAL);
    wr16_le(tex0 + 0x30u, 4u);
    wr16_le(tex0 + 0x34u, BTX_TEX0_HDR_SIZE + BTX_TEX_DICT_SIZE);
    wr32_le(tex0 + 0x38u, BTX_OFS_PAL);

    /* Texture ResDict (count=1, 44 bytes): 32x256 PAL16 color0=1 */
    td[0] = 0u; td[1] = 1u;
    wr16_le(td + 2u, BTX_TEX_DICT_SIZE);
    wr16_le(td + 4u, 8u);
    wr16_le(td + 6u, 16u);
    td[8] = 0x7Fu; td[9] = 1u;
    td[12] = 0x00u; td[13] = 1u;
    wr16_le(td + 16u, 8u);
    wr16_le(td + 18u, 28u);
    wr16_le(td + 20u, 0u);
    /* s=2 (32px), t=5 (256px), fmt=3 (PAL16), c0=1 -> 0x2EA0 */
    wr16_le(td + 22u, (2u << 4) | (5u << 7) | (3u << 10) | (1u << 13));
    wr32_le(td + 24u, (BTX_W & 0x7FFu) | ((BTX_H & 0x7FFu) << 11) | (1u << 31));
    memcpy(td + 28u, "pk_ow", 5);

    /* Palette ResDict (count=1, 40 bytes) */
    pd[0] = 0u; pd[1] = 1u;
    wr16_le(pd + 2u, BTX_PAL_DICT_SIZE);
    wr16_le(pd + 4u, 8u);
    wr16_le(pd + 6u, 16u);
    pd[8] = 0x7Fu; pd[9] = 1u;
    pd[12] = 0x00u; pd[13] = 1u;
    wr16_le(pd + 16u, 4u);
    wr16_le(pd + 18u, 24u);
    wr16_le(pd + 20u, 0u);
    wr16_le(pd + 22u, 0u);
    memcpy(pd + 24u, "pk_ow_pl", 8);

    /* Decode both 32x32 OBJ-tiled icon frames from a/0/0/7 member */
    for (f = 0u; f < 2u; f++) {
        const uint8_t *src = icon_chr + 0x30u + f * 512u;
        for (ty = 0u; ty < 4u; ty++) {
            for (tx = 0u; tx < 4u; tx++) {
                const uint8_t *tile = src + (ty * 4u + tx) * 32u;
                for (py = 0u; py < 8u; py++) {
                    for (px = 0u; px < 4u; px++) {
                        uint8_t b = tile[py * 4u + px];
                        frame_px[f][ty * 8u + py][tx * 8u + px * 2u]      = (uint8_t)(b & 0x0Fu);
                        frame_px[f][ty * 8u + py][tx * 8u + px * 2u + 1u] = (uint8_t)((b >> 4) & 0x0Fu);
                    }
                }
            }
        }
    }

    /* Pack 8 linear 32x32 frames into 32x256 4bpp texels */
    for (f = 0u; f < 8u; f++) {
        uint32_t src_idx = frame_step[f];
        int flip = frame_flip[f];
        uint32_t y_shift = frame_step[f] ? 1u : 0u;

        for (py = 0u; py < 32u; py++) {
            uint32_t sy = (py + y_shift < 32u) ? (py + y_shift) : 31u;
            uint8_t *dst_row = texels + (f * 32u + py) * (BTX_W / 2u);

            for (px = 0u; px < BTX_W; px += 2u) {
                uint32_t sx0 = flip ? (BTX_W - 1u - px) : px;
                uint32_t sx1 = flip ? (BTX_W - 2u - px) : (px + 1u);
                uint8_t p0 = frame_px[src_idx][sy][sx0];
                uint8_t p1 = frame_px[src_idx][sy][sx1];
                dst_row[px / 2u] = (uint8_t)((p0 & 0x0Fu) | ((p1 & 0x0Fu) << 4));
            }
        }
    }

    /* Write 16-color BGR555 palette (index 0 always 0; shiny rotates RGB channels) */
    wr16_le(pal_out, 0u);
    for (i = 1u; i < 16u; i++) {
        uint32_t off = 0x28u + pal_bank * 32u + i * 2u;
        uint16_t c = (off + 2u <= pal_size) ? rd16_le(pal_nclr + off) : (uint16_t)0x7FFFu;
        if (shiny) {
            uint16_t r = c & 31u;
            uint16_t g = (c >> 5) & 31u;
            uint16_t b = (c >> 10) & 31u;
            c = (uint16_t)(g | (b << 5) | (r << 10));
        }
        wr16_le(pal_out + i * 2u, c & 0x7FFFu);
    }
}

static uint8_t default_tex_size_for_mdl(uint8_t mdl_size)
{
    if (mdl_size == 1u) {
        return 12u; /* 16x128: s=1 (16), t=4 (128) -> s*8+t = 12 */
    }
    if (mdl_size == 2u) {
        return 30u; /* 64x512: s=3 (64), t=6 (512) -> s*8+t = 30 */
    }
    return 21u; /* 32x256: s=2 (32), t=5 (256) -> s*8+t = 21 */
}

static uint32_t cell_px_for_mdl(uint8_t mdl_size)
{
    if (mdl_size == 1u) {
        return 16u;
    }
    if (mdl_size == 2u) {
        return 64u;
    }
    return 32u;
}

static uint8_t derive_btx0_tex_size(
    const uint8_t *btx,
    uint32_t btx_size,
    uint8_t mdl_size,
    uint8_t fallback_tex_size)
{
    uint32_t cell_px = cell_px_for_mdl(mdl_size);
    uint8_t safe_def = default_tex_size_for_mdl(mdl_size);
    uint8_t safe_fb = safe_def;

    if (fallback_tex_size < 64u) {
        uint32_t fb_s = (uint32_t)fallback_tex_size >> 3;
        uint32_t fb_t = (uint32_t)fallback_tex_size & 7u;
        if ((8u << fb_s) >= cell_px && (8u << fb_t) >= cell_px) {
            safe_fb = fallback_tex_size;
        }
    }

    if (btx != NULL && btx_size >= 0x54u &&
        btx[0] == 'B' && btx[1] == 'T' && btx[2] == 'X' && btx[3] == '0') {
        uint32_t sec_off = rd32_le(btx + 0x10u);
        if (sec_off >= 0x10u && sec_off + 0x3Cu <= btx_size) {
            const uint8_t *tex0 = btx + sec_off;
            uint32_t tex0_avail = btx_size - sec_off;
            if (tex0[0] == 'T' && tex0[1] == 'E' && tex0[2] == 'X' && tex0[3] == '0') {
                uint32_t td_off = rd16_le(tex0 + 0x0Eu);
                if (td_off >= 0x3Cu && td_off + 24u <= tex0_avail) {
                    const uint8_t *td = tex0 + td_off;
                    uint32_t td_avail = tex0_avail - td_off;
                    uint32_t ofs_entries = rd16_le(td + 6u);
                    if (ofs_entries >= 8u && ofs_entries + 8u <= td_avail) {
                        uint16_t params = rd16_le(td + ofs_entries + 6u);
                        uint32_t s_code = (params >> 4) & 7u;
                        uint32_t t_code = (params >> 7) & 7u;
                        if ((8u << s_code) >= cell_px && (8u << t_code) >= cell_px) {
                            return (uint8_t)(s_code * 8u + t_code);
                        }
                    }
                }
            }
        }
    }

    return safe_fb;
}

static void register_entry(
    const struct PokebwMmdlObjCodeParam *tmpl,
    uint32_t member_id,
    uint16_t species,
    uint8_t form,
    int shiny,
    int is_custom,
    uint8_t mdl_size,
    uint8_t tex_size,
    uint8_t anim_id,
    int8_t off_x,
    int8_t off_y,
    int8_t off_z,
    uint8_t shadow_size)
{
    uint32_t slot;
    struct PokebwOwEntry *e;

    if (g_entry_count >= OW_MAX_SLOTS) {
        return;
    }
    if (mdl_size > 2u) {
        mdl_size = 0u;
    }
    slot = g_entry_count++;
    e = &g_entries[slot];
    memset(e, 0, sizeof(*e));
    e->species = species;
    e->form = form;
    e->shiny = shiny ? 1u : 0u;
    e->custom = is_custom ? 1u : 0u;
    e->active = 1u;
    e->obj_code = (uint16_t)(POKEBW_OW_OBJCODE_BASE + slot);

    e->param = *tmpl;
    e->param.code = e->obj_code;
    e->param.drawType = 1u; /* billboard */
    e->param.shadowSize = shadow_size;
    e->param.mdlSize = mdl_size;
    e->param.texSize = tex_size;
    e->param.animId = anim_id;
    e->param.sizeWidth = 1u;
    e->param.sizeDepth = 1u;
    e->param.offsetX = off_x;
    e->param.offsetY = off_y;
    e->param.offsetZ = off_z;
    memset(e->param.resource, 0, sizeof(e->param.resource));
    wr32_le(e->param.resource, member_id);

    if (form == 0u && species <= POKEBW_OW_SPECIES_MAX) {
        g_base_objcode[species][e->shiny] = e->obj_code;
    }
    if (form != 0u) {
        g_form_count++;
    }
    if (is_custom) {
        g_custom_count++;
    }
}

int pokebw_ow_sprites_register(void)
{
    BwArcFile arc_tbl, arc_spr, arc_ico, arc_per;
    struct PokebwMmdlObjCodeParam tmpl;
    uint8_t tmpl_tex_by_size[3];
    uint8_t *tbl_buf = NULL;
    uint8_t *pal_nclr = NULL;
    uint8_t *bwsp = NULL;
    uint32_t tbl_size = 0u, pal_size = 0u, bwsp_size = 0u;
    uint32_t next_member = 0u;
    const uint8_t *ptab;
    int have_ico = 0, have_per = 0;
    uint32_t sp, i;

    if (g_registered != 0) {
        return g_registered > 0 ? 0 : -1;
    }
    g_registered = -1;

    uint32_t rec_count, r;
    int found_anim14 = 0;
    int found_drawproc1 = 0;

    tmpl_tex_by_size[0] = default_tex_size_for_mdl(0u);
    tmpl_tex_by_size[1] = default_tex_size_for_mdl(1u);
    tmpl_tex_by_size[2] = default_tex_size_for_mdl(2u);

    if (bw_arc_open(&arc_tbl, "a/0/4/8") != 0) {
        return -1;
    }
    tbl_buf = read_arc_member(&arc_tbl, 0u, &tbl_size);
    bw_arc_close(&arc_tbl);
    if (tbl_buf == NULL || tbl_size < 4u + (WALK_TEMPLATE_CODE + 1u) * PARAM_RECORD_SIZE) {
        free(tbl_buf);
        return -1;
    }
    rec_count = rd32_le(tbl_buf);
    if (rec_count > (tbl_size - 4u) / PARAM_RECORD_SIZE) {
        rec_count = (tbl_size - 4u) / PARAM_RECORD_SIZE;
    }
    memcpy(&tmpl, tbl_buf + 4u + WALK_TEMPLATE_CODE * PARAM_RECORD_SIZE, PARAM_RECORD_SIZE);
    for (r = 0u; r < rec_count; r++) {
        struct PokebwMmdlObjCodeParam cand;
        memcpy(&cand, tbl_buf + 4u + r * PARAM_RECORD_SIZE, PARAM_RECORD_SIZE);
        if (cand.drawType == 1u && cand.animId == ANIM_ID_8FRAME) {
            if (!found_anim14 || (!found_drawproc1 && cand.drawProcNo == 1u)) {
                tmpl = cand;
                found_anim14 = 1;
                if (cand.drawProcNo == 1u) {
                    found_drawproc1 = 1;
                }
            }
            if (cand.mdlSize <= 2u && cand.texSize < 64u) {
                uint32_t cell_px = cell_px_for_mdl(cand.mdlSize);
                uint32_t s_code = (uint32_t)cand.texSize >> 3;
                uint32_t t_code = (uint32_t)cand.texSize & 7u;
                if ((8u << s_code) >= cell_px && (8u << t_code) >= cell_px) {
                    tmpl_tex_by_size[cand.mdlSize] = cand.texSize;
                }
            }
        }
    }
    free(tbl_buf);

    if (bw_arc_open(&arc_spr, "a/0/4/9") != 0) {
        return -1;
    }
    next_member = arc_spr.count;
    bw_arc_close(&arc_spr);

    /* 1. Register optional custom BWSP sprite pack entries first (highest priority). */
    bwsp = load_bwsp_pack(&bwsp_size);
    if (bwsp != NULL && bwsp_size >= BWSP_HEADER_SIZE &&
        bwsp[0] == BWSP_MAGIC_0 && bwsp[1] == BWSP_MAGIC_1 &&
        bwsp[2] == BWSP_MAGIC_2 && bwsp[3] == BWSP_MAGIC_3 &&
        rd16_le(bwsp + 4u) == BWSP_VERSION) {
        uint32_t count = rd32_le(bwsp + 8u);
        if (BWSP_HEADER_SIZE + (uint64_t)count * BWSP_ENTRY_SIZE <= bwsp_size) {
            for (i = 0u; i < count && g_entry_count < OW_MAX_SLOTS; i++) {
                const uint8_t *ent = bwsp + BWSP_HEADER_SIZE + i * BWSP_ENTRY_SIZE;
                uint16_t species = rd16_le(ent + 0u);
                uint8_t form = ent[2];
                uint8_t shiny = ent[3] ? 1u : 0u;
                uint8_t mdl_size = (ent[4] <= 2u) ? ent[4] : 0u;
                uint8_t anim_id = ent[5];
                int8_t off_x = (int8_t)ent[6];
                int8_t off_y = (int8_t)ent[7];
                int8_t off_z = (int8_t)ent[8];
                uint8_t shadow = ent[9];
                uint32_t doff = rd32_le(ent + 12u);
                uint32_t dsize = rd32_le(ent + 16u);
                uint8_t tex_size;

                if (species == 0u || species > POKEBW_OW_SPECIES_MAX ||
                    dsize == 0u || (uint64_t)doff + dsize > bwsp_size ||
                    next_member >= BW_NARC_MAX_MEMBERS || next_member >= 65535u) {
                    continue;
                }
                if (find_entry_slot(species, form, shiny) >= 0) {
                    continue;
                }
                tex_size = derive_btx0_tex_size(
                    bwsp + doff, dsize, mdl_size, tmpl_tex_by_size[mdl_size]);
                if (bw_vfs_mem_set("", "a/0/4/9", next_member, bwsp + doff, dsize) == 0) {
                    register_entry(&tmpl, next_member, species, form, shiny, 1,
                                   mdl_size, tex_size, anim_id, off_x, off_y, off_z, shadow);
                    next_member++;
                }
            }
        }
    }
    free(bwsp);

    /* 2. Synthesize any missing species (1..649 normal+shiny) & alternate forms from ROM a/0/0/7. */
    if (bw_arc_open(&arc_ico, "a/0/0/7") == 0) {
        pal_nclr = read_arc_member(&arc_ico, 0u, &pal_size);
        have_ico = (pal_nclr != NULL);
    }
    if (bw_arc_open(&arc_per, "a/0/1/6") == 0) {
        have_per = 1;
    }
    ptab = (const uint8_t *)bw_arm9_rodata(ICON_PAL_ADDR, ICON_PAL_BYTES);

    if (have_ico) {
        uint8_t *btx = (uint8_t *)malloc(BTX_TOTAL_SIZE);
        if (btx != NULL) {
            for (sp = 1u; sp <= POKEBW_OW_SPECIES_MAX && g_entry_count + 2u <= OW_MAX_SLOTS; sp++) {
                uint32_t icon_member = sp * 2u + 7u;
                uint32_t chr_size = 0u;
                uint8_t *chr = read_arc_member(&arc_ico, icon_member, &chr_size);
                uint32_t bank = (ptab != NULL && sp < ICON_PAL_BYTES) ? (uint32_t)(ptab[sp] & 15u) : 0u;
                uint32_t form_first = 0u, form_cnt = 1u;
                int form_shared = 1;

                if (bank >= 3u) {
                    bank = 0u;
                }
                if (chr != NULL && chr_size >= 0x30u + 1024u) {
                    int sh;
                    for (sh = 0; sh < 2; sh++) {
                        if (find_entry_slot((uint16_t)sp, 0u, sh) < 0 &&
                            next_member < BW_NARC_MAX_MEMBERS && next_member < 65535u) {
                            uint8_t tex_size;
                            build_synthesized_btx0(chr, pal_nclr, pal_size, bank, sh, btx);
                            tex_size = derive_btx0_tex_size(
                                btx, BTX_TOTAL_SIZE, 0u, tmpl_tex_by_size[0]);
                            if (bw_vfs_mem_set("", "a/0/4/9", next_member, btx, BTX_TOTAL_SIZE) == 0) {
                                register_entry(&tmpl, next_member, (uint16_t)sp, 0u, sh, 0,
                                               0u, tex_size, ANIM_ID_8FRAME, 0, 0, 0, 1u);
                                next_member++;
                            }
                        }
                    }
                }
                free(chr);

                /* Alternate forms from a/0/1/6 */
                if (have_per) {
                    uint32_t psize = 0u;
                    uint8_t *prec = read_arc_member(&arc_per, sp, &psize);
                    if (prec != NULL && psize >= 0x24u) {
                        form_first = rd16_le(prec + 0x1Eu);
                        form_cnt = prec[0x20];
                        form_shared = (prec[0x21] >> 7) & 1;
                    }
                    free(prec);

                    if (form_cnt > 1u && !form_shared) {
                        uint32_t fm;
                        for (fm = 1u; fm < form_cnt && fm < 32u && g_entry_count + 2u <= OW_MAX_SLOTS; fm++) {
                            uint32_t fm_member = (form_first + fm - 1u) * 2u + 1311u;
                            uint32_t fm_pal_idx = form_first + fm + 651u;
                            uint32_t fm_bank = (ptab != NULL && fm_pal_idx < ICON_PAL_BYTES)
                                                   ? (uint32_t)(ptab[fm_pal_idx] & 15u)
                                                   : bank;
                            uint32_t fsize = 0u;
                            uint8_t *fchr;
                            int sh;

                            if (fm_bank >= 3u) {
                                fm_bank = 0u;
                            }
                            if (fm_member >= ICON_MEMBERS) {
                                continue;
                            }
                            fchr = read_arc_member(&arc_ico, fm_member, &fsize);
                            if (fchr != NULL && fsize >= 0x30u + 1024u) {
                                for (sh = 0; sh < 2; sh++) {
                                    if (find_entry_slot((uint16_t)sp, (uint8_t)fm, sh) < 0 &&
                                        next_member < BW_NARC_MAX_MEMBERS && next_member < 65535u) {
                                        uint8_t tex_size;
                                        build_synthesized_btx0(fchr, pal_nclr, pal_size, fm_bank, sh, btx);
                                        tex_size = derive_btx0_tex_size(
                                            btx, BTX_TOTAL_SIZE, 0u, tmpl_tex_by_size[0]);
                                        if (bw_vfs_mem_set("", "a/0/4/9", next_member, btx, BTX_TOTAL_SIZE) == 0) {
                                            register_entry(&tmpl, next_member, (uint16_t)sp, (uint8_t)fm, sh, 0,
                                                           0u, tex_size, ANIM_ID_8FRAME, 0, 0, 0, 1u);
                                            next_member++;
                                        }
                                    }
                                }
                            }
                            free(fchr);
                        }
                    }
                }
            }
            free(btx);
        }
    }

    free(pal_nclr);
    if (have_ico) {
        bw_arc_close(&arc_ico);
    }
    if (have_per) {
        bw_arc_close(&arc_per);
    }

    bw_vfs_flush_path("a/0/4/9");
    g_registered = 1;
    fprintf(stderr,
            "eidos: ow_sprites: %u registered (%u custom .bwsp, %u alt forms, %u ROM synth, tmpl drawProc=%u anim14=%d tex=[%u,%u,%u])\n",
            (unsigned)g_entry_count, (unsigned)g_custom_count, (unsigned)g_form_count,
            (unsigned)(g_entry_count - g_custom_count),
            (unsigned)tmpl.drawProcNo, found_anim14,
            (unsigned)tmpl_tex_by_size[0], (unsigned)tmpl_tex_by_size[1],
            (unsigned)tmpl_tex_by_size[2]);
    return 0;
}

uint8_t pokebw_ow_sprite_mdl_size(uint16_t objCode)
{
    uint32_t slot;
    if (objCode < POKEBW_OW_OBJCODE_BASE || objCode >= POKEBW_OW_OBJCODE_END) {
        return 0u;
    }
    slot = objCode - POKEBW_OW_OBJCODE_BASE;
    if (slot < g_entry_count && g_entries[slot].active) {
        return g_entries[slot].param.mdlSize;
    }
    return 0u;
}

uint16_t pokebw_ow_sprite_objcode(uint16_t species, uint8_t form, int shiny)
{
    int slot = find_entry_slot(species, form, shiny);
    if (slot < 0 && form != 0u) {
        slot = find_entry_slot(species, 0u, shiny);
    }
    if (slot < 0 && shiny) {
        slot = find_entry_slot(species, form, 0);
    }
    if (slot < 0) {
        return 0u;
    }
    return g_entries[slot].obj_code;
}

int pokebw_ow_sprites_is_code(uint32_t objCode)
{
    uint32_t slot;
    if (objCode < POKEBW_OW_OBJCODE_BASE || objCode >= POKEBW_OW_OBJCODE_END) {
        return 0;
    }
    slot = objCode - POKEBW_OW_OBJCODE_BASE;
    return (slot < g_entry_count && g_entries[slot].active) ? 1 : 0;
}

void pokebw_ow_sprites_stats(uint32_t *out_total, uint32_t *out_custom, uint32_t *out_forms)
{
    if (out_total != NULL) {
        *out_total = g_entry_count;
    }
    if (out_custom != NULL) {
        *out_custom = g_custom_count;
    }
    if (out_forms != NULL) {
        *out_forms = g_form_count;
    }
}

void __wrap_ov010_FLDMMDL_LoadObjCodeParam(const void *sys, uint32_t objCode, void *out)
{
    if (objCode >= POKEBW_OW_OBJCODE_BASE && objCode < POKEBW_OW_OBJCODE_END && out != NULL) {
        uint32_t slot = objCode - POKEBW_OW_OBJCODE_BASE;
        if (slot < g_entry_count && g_entries[slot].active) {
            memcpy(out, &g_entries[slot].param, PARAM_RECORD_SIZE);
            return;
        }
    }
    __real_ov010_FLDMMDL_LoadObjCodeParam(sys, objCode, out);
}
