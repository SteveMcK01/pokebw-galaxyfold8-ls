#!/usr/bin/env python3
"""
tools/pack_ow_sprites.py -- Phase 3.1: 649-Species Overworld Sprite Archive Builder.

Encodes Pokémon overworld sprite sheets (TooManyLuigis 4x4 RPGM/PokeMMO sheets,
HGSS 4x2/2x4 sheets, 1x8 Gen 5 billboard strips, or raw .nsbtx/.btx0 files)
into byte-exact Nitro G3D BTX0 (NSBTX) textures packed into a compact BWSP
archive (`pokebw_ow_sprites.bwsp`) or eidos NARC VFS mod directory.

Supported filenames inside `--input` (and optional `shiny/` subdirectory):
  001.png / 001_s.png / 001_shiny.png
  001_f1.png / 001-1.png / 001_f1_s.png
  001.nsbtx / 001.btx0 / ...

Optional `--offsets <file.json>` mapping keys (`"25"`, `"386_f1"`, etc.):
  { "386_f1": { "mdl_size": 2, "anim_id": 14, "off_x": 0, "off_y": 0, "off_z": 0, "shadow": 1 } }

Examples:
  python3 tools/pack_ow_sprites.py pack --input ./follower_pngs --output pokebw_ow_sprites.bwsp
  python3 tools/pack_ow_sprites.py inspect pokebw_ow_sprites.bwsp
  python3 tools/pack_ow_sprites.py self-test
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import struct
import sys
import zlib
from typing import Dict, List, Optional, Tuple

BWSP_MAGIC = b"BWSP"
BWSP_VERSION = 1

# Frame ordering for Gen 5 billboard animId 14 (sBlactAnimeTable[14]):
#   Up(0,1), Down(2,3), Left(4,5), Right(6,7)
FRAME_COUNT = 8
ANIM_ID_DEFAULT = 14

# Filename patterns: e.g. 001.png, 025_s.png, 386-1.png, 386_f2_shiny.png
NAME_RE = re.compile(
    r"^(?P<species>\d{1,4})"
    r"(?:(?:[-_]f?|f)(?P<form>\d{1,2}))?"
    r"(?:[_-]?(?P<shiny>s|shiny))?$",
    re.IGNORECASE,
)


def _log2_size_code(px: int) -> int:
    mapping = {8: 0, 16: 1, 32: 2, 64: 3, 128: 4, 256: 5, 512: 6, 1024: 7}
    if px not in mapping:
        raise ValueError(f"Unsupported DS texture dimension {px} (must be power of 2 in 8..1024)")
    return mapping[px]


def rgba_to_bgr555(r: int, g: int, b: int) -> int:
    return ((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10)


def quantize_palette_16(pixels: List[Tuple[int, int, int, int]]) -> Tuple[List[int], List[int]]:
    """Quantize RGBA pixels into 16 BGR555 colors (index 0 = transparent) + 4bpp indices."""
    opaque: Dict[int, int] = {}
    bgr_pixels: List[int] = []
    for r, g, b, a in pixels:
        if a < 128:
            bgr_pixels.append(-1)
        else:
            c = rgba_to_bgr555(r, g, b)
            bgr_pixels.append(c)
            opaque[c] = opaque.get(c, 0) + 1

    # Order unique opaque colors by frequency
    ranked = sorted(opaque.items(), key=lambda kv: (-kv[1], kv[0]))
    if len(ranked) <= 15:
        pal_bgr = [0] + [c for c, _ in ranked]
    else:
        # Frequency-seeded greedy 5-bit RGB bucketing into 15 slots
        pal_bgr = [0]
        step = max(1, len(ranked) // 15)
        for i in range(0, len(ranked), step):
            if len(pal_bgr) >= 16:
                break
            pal_bgr.append(ranked[i][0])
        while len(pal_bgr) < 16 and len(ranked) >= len(pal_bgr):
            pal_bgr.append(ranked[len(pal_bgr) - 1][0])

    while len(pal_bgr) < 16:
        pal_bgr.append(0)

    pal_rgb = [(c & 31, (c >> 5) & 31, (c >> 10) & 31) for c in pal_bgr]
    cache: Dict[int, int] = {}
    indices: List[int] = []
    for c in bgr_pixels:
        if c < 0:
            indices.append(0)
            continue
        idx = cache.get(c)
        if idx is None:
            r0, g0, b0 = c & 31, (c >> 5) & 31, (c >> 10) & 31
            best_i = 1
            best_d = 1 << 30
            for pi in range(1, 16):
                pr, pg, pb = pal_rgb[pi]
                d = (r0 - pr) * (r0 - pr) + (g0 - pg) * (g0 - pg) + (b0 - pb) * (b0 - pb)
                if d < best_d:
                    best_d = d
                    best_i = pi
                    if d == 0:
                        break
            idx = best_i
            cache[c] = idx
        indices.append(idx)

    return pal_bgr, indices


def build_single_btx0(width: int, height: int, indices: List[int], pal_bgr: List[int]) -> bytes:
    """
    Build a byte-exact 1-texture PAL16 (4bpp) Nitro BTX0 file compatible with
    both DS hardware VRAM uploaders (ov010/fldmmdl_blact) and eidos's BTX0 parser.
    """
    if width * height != len(indices) or len(pal_bgr) != 16:
        raise ValueError("Invalid pixel or palette size for BTX0")

    s_code = _log2_size_code(width)
    t_code = _log2_size_code(height)
    texel_bytes = (width * height) // 2
    pal_bytes = 32

    tex_dict_size = 44  # 8 + (1+1)*4 + 4 + 1*8 + 16
    pal_dict_size = 40  # 8 + (1+1)*4 + 4 + 1*4 + 16
    tex0_hdr_size = 0x3C
    ofs_tex = tex0_hdr_size + tex_dict_size + pal_dict_size  # 0x90
    ofs_pal = ofs_tex + texel_bytes
    tex0_size = ofs_pal + pal_bytes
    file_size = 0x14 + tex0_size

    buf = bytearray()
    # BTX0 container header (20 bytes)
    buf += struct.pack("<4sHHIHHI", b"BTX0", 0xFEFF, 1, file_size, 0x10, 1, 0x14)

    # TEX0 header (0x3C bytes)
    tex0_hdr = bytearray(0x3C)
    struct.pack_into("<4sI", tex0_hdr, 0x00, b"TEX0", tex0_size)
    struct.pack_into("<HHHH", tex0_hdr, 0x0C, texel_bytes >> 3, 0x3C, 0, 0)
    struct.pack_into("<I", tex0_hdr, 0x14, ofs_tex)
    struct.pack_into("<HHHH", tex0_hdr, 0x1C, 0, 0x3C, 0, 0)
    struct.pack_into("<II", tex0_hdr, 0x24, ofs_pal, ofs_pal)
    struct.pack_into("<HHHH", tex0_hdr, 0x30, 4, 0, 0x3C + tex_dict_size, 0)
    struct.pack_into("<I", tex0_hdr, 0x38, ofs_pal)
    buf += tex0_hdr

    # Texture ResDict (44 bytes): 2 patricia nodes + 1 record (8B) + 1 name (16B)
    params = (s_code << 4) | (t_code << 7) | (3 << 10) | (1 << 13)
    extra = (width & 0x7FF) | ((height & 0x7FF) << 11) | (1 << 31)
    tex_dict = bytearray()
    tex_dict += struct.pack("<BBHHH", 0, 1, tex_dict_size, 8, 16)
    tex_dict += struct.pack("<BBBB", 0x7F, 1, 0, 0)
    tex_dict += struct.pack("<BBBB", 0x00, 1, 0, 0)
    tex_dict += struct.pack("<HH", 8, 28)
    tex_dict += struct.pack("<HHI", 0, params, extra)
    tex_dict += b"pk_ow".ljust(16, b"\x00")
    assert len(tex_dict) == tex_dict_size
    buf += tex_dict

    # Palette ResDict (40 bytes): 2 patricia nodes + 1 record (4B) + 1 name (16B)
    pal_dict = bytearray()
    pal_dict += struct.pack("<BBHHH", 0, 1, pal_dict_size, 8, 16)
    pal_dict += struct.pack("<BBBB", 0x7F, 1, 0, 0)
    pal_dict += struct.pack("<BBBB", 0x00, 1, 0, 0)
    pal_dict += struct.pack("<HH", 4, 24)
    pal_dict += struct.pack("<HH", 0, 0)
    pal_dict += b"pk_ow_pl".ljust(16, b"\x00")
    assert len(pal_dict) == pal_dict_size
    buf += pal_dict

    # Pack 4bpp texels (low nibble = even pixel, high nibble = odd pixel)
    texels = bytearray(texel_bytes)
    for i in range(0, len(indices), 2):
        texels[i // 2] = (indices[i] & 0xF) | ((indices[i + 1] & 0xF) << 4)
    buf += texels

    # Pack 16 BGR555 colors
    for c in pal_bgr:
        buf += struct.pack("<H", c & 0x7FFF)

    assert len(buf) == file_size
    return bytes(buf)


def validate_btx0(blob: bytes) -> Tuple[int, int, int]:
    """Validate BTX0 header + TEX0 + dicts and return (width, height, fmt)."""
    if len(blob) < 0x14 + 0x3C + 84 or blob[:4] != b"BTX0":
        raise ValueError("Not a valid BTX0 container")
    file_size, hdr_size, nblk = struct.unpack_from("<IHH", blob, 8)
    if file_size > len(blob) or nblk < 1:
        raise ValueError("Malformed BTX0 container header")
    blk_off = struct.unpack_from("<I", blob, hdr_size)[0]
    if blob[blk_off : blk_off + 4] != b"TEX0":
        raise ValueError("Missing TEX0 block")
    tex_off = struct.unpack_from("<I", blob, blk_off + 0x14)[0]
    pal_off = struct.unpack_from("<I", blob, blk_off + 0x24)[0]
    td_off = blk_off + 0x3C
    td_rev, td_cnt, td_size = struct.unpack_from("<BBH", blob, td_off)
    if td_rev != 0 or td_cnt < 1:
        raise ValueError("Bad TEX0 texture dictionary")
    pd_off = td_off + td_size
    pd_rev, pd_cnt, _ = struct.unpack_from("<BBH", blob, pd_off)
    if pd_rev != 0 or pd_cnt < 1:
        raise ValueError("Bad TEX0 palette dictionary")
    rec_off = td_off + 12 + (td_cnt + 1) * 4
    _, params = struct.unpack_from("<HH", blob, rec_off)
    w = 8 << ((params >> 4) & 7)
    h = 8 << ((params >> 7) & 7)
    fmt = (params >> 10) & 7
    c0 = (params >> 13) & 1
    if c0 != 1 or blk_off + pal_off > len(blob) or tex_off > pal_off:
        raise ValueError("Bad TEX0 texel/palette offsets")
    return w, h, fmt


def _load_png_pure_python(path: pathlib.Path) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    """Minimal RGBA/RGB/Indexed PNG fallback when Pillow is not installed."""
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos = 8
    w = h = bit_depth = color_type = 0
    idat = bytearray()
    while pos + 8 <= len(raw):
        clen = struct.unpack_from(">I", raw, pos)[0]
        ctype = raw[pos + 4 : pos + 8]
        cdata = raw[pos + 8 : pos + 8 + clen]
        pos += 12 + clen
        if ctype == b"IHDR":
            w, h, bit_depth, color_type = struct.unpack_from(">IIBB", cdata, 0)
        elif ctype == b"IDAT":
            idat += cdata
        elif ctype == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6):
        raise RuntimeError(
            f"{path}: pure-Python PNG fallback supports 8-bit RGB/RGBA only; install Pillow (`pip install pillow`) for full PNG support"
        )
    bpp = 4 if color_type == 6 else 3
    stride = w * bpp
    decomp = zlib.decompress(bytes(idat))
    out: List[Tuple[int, int, int, int]] = []
    prev = bytearray(stride)
    idx = 0
    for _ in range(h):
        flt = decomp[idx]
        idx += 1
        row = bytearray(decomp[idx : idx + stride])
        idx += stride
        if flt == 1:
            for x in range(bpp, stride):
                row[x] = (row[x] + row[x - bpp]) & 0xFF
        elif flt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 0xFF
        elif flt == 3:
            for x in range(stride):
                a = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif flt == 4:
            for x in range(stride):
                a = row[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[x] = (row[x] + pr) & 0xFF
        elif flt != 0:
            raise ValueError(f"{path}: unsupported PNG filter {flt}")
        for x in range(0, stride, bpp):
            out.append((row[x], row[x + 1], row[x + 2], row[x + 3] if bpp == 4 else 255))
        prev = row
    return w, h, out


def load_rgba(path: pathlib.Path) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    try:
        from PIL import Image  # type: ignore

        with Image.open(path) as im:
            rgba = im.convert("RGBA")
            w, h = rgba.size
            return w, h, list(rgba.getdata())
    except ImportError:
        return _load_png_pure_python(path)


def extract_8_frames(
    w: int, h: int, pixels: List[Tuple[int, int, int, int]], layout: str
) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    """
    Convert input sprite sheet into a 1x8 vertical strip ordered for Gen 5
    billboard animId 14: [Up0, Up1, Down0, Down1, Left0, Left1, Right0, Right1].
    """
    if layout == "auto":
        if w * 8 == h:
            layout = "1x8"
        elif w == h:
            layout = "4x4"
        elif w * 2 == h:
            layout = "4x2"
        elif h * 2 == w:
            layout = "2x4"
        else:
            raise ValueError(f"Cannot auto-detect sprite sheet layout for {w}x{h}")

    def crop_cell(cx: int, cy: int, cols: int, rows: int) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
        cw, ch = w // cols, h // rows
        out = []
        for y in range(cy * ch, (cy + 1) * ch):
            row_start = y * w + cx * cw
            out.extend(pixels[row_start : row_start + cw])
        return cw, ch, out

    if layout == "1x8":
        cw, ch = w, h // 8
        frames = [crop_cell(0, i, 1, 8)[2] for i in range(8)]
    elif layout == "4x4":
        # RPGM / PokeMMO / TooManyLuigis: rows Down(0), Left(1), Right(2), Up(3); cols 0,1 = stand, step
        cw, ch = w // 4, h // 4
        coords = [(0, 3), (1, 3), (0, 0), (1, 0), (0, 1), (1, 1), (0, 2), (1, 2)]
        frames = [crop_cell(cx, cy, 4, 4)[2] for cx, cy in coords]
    elif layout == "4x2":
        # 2 cols x 4 rows: Down, Up, Left, Right OR Up, Down, Left, Right -> use standard (D, L, R, U) mapped cleanly
        cw, ch = w // 2, h // 4
        coords = [(0, 3), (1, 3), (0, 0), (1, 0), (0, 1), (1, 1), (0, 2), (1, 2)]
        frames = [crop_cell(cx, cy, 2, 4)[2] for cx, cy in coords]
    elif layout == "2x4":
        # 4 cols x 2 rows (each direction 2 frames)
        cw, ch = w // 4, h // 2
        coords = [(3, 0), (3, 1), (0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1)]
        frames = [crop_cell(cx, cy, 4, 2)[2] for cx, cy in coords]
    else:
        raise ValueError(f"Unknown layout: {layout}")

    # Scale/pad cell to DS-supported square billboard cell size (16, 32, or 64)
    target = 16 if cw <= 16 and ch <= 16 else (32 if cw <= 32 and ch <= 32 else 64)
    strip: List[Tuple[int, int, int, int]] = []
    for fr in frames:
        if cw == target and ch == target:
            strip.extend(fr)
        else:
            pad_x = max(0, (target - cw) // 2)
            pad_y = max(0, target - ch)
            for ty in range(target):
                sy = ty - pad_y
                for tx in range(target):
                    sx = tx - pad_x
                    if 0 <= sx < cw and 0 <= sy < ch:
                        strip.append(fr[sy * cw + sx])
                    else:
                        strip.append((0, 0, 0, 0))
    return target, target * 8, strip


def encode_png_sheet(
    path: pathlib.Path, layout: str
) -> Tuple[bytes, int]:
    w, h, pixels = load_rgba(path)
    sw, sh, strip = extract_8_frames(w, h, pixels, layout)
    pal_bgr, indices = quantize_palette_16(strip)
    btx = build_single_btx0(sw, sh, indices, pal_bgr)
    mdl_size = 1 if sw == 16 else (2 if sw == 64 else 0)
    return btx, mdl_size


def parse_sprite_filename(path: pathlib.Path, shiny_hint: bool) -> Optional[Tuple[int, int, int]]:
    m = NAME_RE.match(path.stem)
    if not m:
        return None
    species = int(m.group("species"))
    form = int(m.group("form") or 0)
    shiny = 1 if (shiny_hint or m.group("shiny")) else 0
    if species < 1 or species > 649:
        return None
    return species, form, shiny


def pack_directory(
    input_dir: pathlib.Path,
    output_bwsp: pathlib.Path,
    layout: str,
    offsets_path: Optional[pathlib.Path],
    mod_dir: Optional[pathlib.Path],
) -> int:
    offsets: Dict[str, dict] = {}
    if offsets_path and offsets_path.is_file():
        offsets = json.loads(offsets_path.read_text(encoding="utf-8"))

    items: Dict[Tuple[int, int, int], Tuple[bytes, int]] = {}
    for p in sorted(input_dir.rglob("*")):
        if not p.is_file():
            continue
        ext = p.suffix.lower()
        if ext not in (".png", ".nsbtx", ".btx0"):
            continue
        shiny_dir = any(part.lower() == "shiny" for part in p.relative_to(input_dir).parts[:-1])
        key = parse_sprite_filename(p, shiny_dir)
        if key is None:
            continue
        if ext == ".png":
            btx, mdl_size = encode_png_sheet(p, layout)
        else:
            btx = p.read_bytes()
            w, _, _ = validate_btx0(btx)
            mdl_size = 1 if w <= 16 else (2 if w >= 64 else 0)
        items[key] = (btx, mdl_size)

    sorted_keys = sorted(items.keys())
    header_size = 16
    entry_size = 20
    data_cursor = header_size + len(sorted_keys) * entry_size
    entries_buf = bytearray()
    blobs_buf = bytearray()

    for species, form, shiny in sorted_keys:
        btx, mdl_size = items[(species, form, shiny)]
        meta = offsets.get(f"{species}_f{form}", offsets.get(str(species), {}))
        mdl_size = int(meta.get("mdl_size", mdl_size))
        anim_id = int(meta.get("anim_id", ANIM_ID_DEFAULT))
        off_x = int(meta.get("off_x", 0))
        off_y = int(meta.get("off_y", 0))
        off_z = int(meta.get("off_z", 0))
        shadow = int(meta.get("shadow", 1))

        pad = (-len(blobs_buf)) & 3
        blobs_buf += b"\x00" * pad
        data_off = data_cursor + len(blobs_buf)
        blobs_buf += btx

        entries_buf += struct.pack(
            "<HBBBBbbbBHII",
            species,
            form,
            shiny,
            mdl_size,
            anim_id,
            off_x,
            off_y,
            off_z,
            shadow,
            0,
            data_off,
            len(btx),
        )

    output_bwsp.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack("<4sHHII", BWSP_MAGIC, BWSP_VERSION, 0, len(sorted_keys), 0)
    output_bwsp.write_bytes(header + entries_buf + blobs_buf)

    if mod_dir is not None:
        manifest = mod_dir / "eidos_mod.txt"
        mod_dir.mkdir(parents=True, exist_ok=True)
        manifest.write_text("name = PokeBW 649 Overworld Sprites\norder = 5\n", encoding="utf-8")
        (mod_dir / "pokebw_ow_sprites.bwsp").write_bytes(output_bwsp.read_bytes())

    print(f"Packed {len(sorted_keys)} sprites into {output_bwsp} ({output_bwsp.stat().st_size} bytes)")
    return len(sorted_keys)


def inspect_bwsp(path: pathlib.Path) -> None:
    raw = path.read_bytes()
    magic, ver, _, count, _ = struct.unpack_from("<4sHHII", raw, 0)
    if magic != BWSP_MAGIC or ver != BWSP_VERSION:
        raise ValueError(f"{path}: invalid BWSP header")
    normal = shiny = forms = 0
    for i in range(count):
        sp, fm, sh, mdl, anim, ox, oy, oz, shdw, _, doff, dsz = struct.unpack_from(
            "<HBBBBbbbBHII", raw, 16 + i * 20
        )
        validate_btx0(raw[doff : doff + dsz])
        if fm != 0:
            forms += 1
        elif sh != 0:
            shiny += 1
        else:
            normal += 1
    print(f"{path}: v{ver}, total={count} (normal={normal}, shiny={shiny}, forms={forms}), size={len(raw)} bytes")


def run_self_test() -> int:
    # 1. Verify 32x256 and 64x512 BTX0 structural round-trip
    for cw in (16, 32, 64):
        ch = cw * 8
        pal = [rgba_to_bgr555((i * 17) & 255, (i * 43) & 255, (255 - i * 11) & 255) for i in range(16)]
        pal[0] = 0
        idx = [(x + y) % 16 for y in range(ch) for x in range(cw)]
        btx = build_single_btx0(cw, ch, idx, pal)
        vw, vh, vfmt = validate_btx0(btx)
        assert (vw, vh, vfmt) == (cw, ch, 3), f"Mismatch {(vw, vh, vfmt)}"

    # 2. Verify BWSP entry layout & PNG sheet slicing (4x4 and 4x2)
    w, h = 128, 128
    px = [((x % 32) * 8, (y % 32) * 8, 180, 255 if (x % 32) > 2 else 0) for y in range(h) for x in range(w)]
    sw, sh, strip = extract_8_frames(w, h, px, "4x4")
    assert (sw, sh, len(strip)) == (32, 256, 32 * 256)
    pal_bgr, qidx = quantize_palette_16(strip)
    btx = build_single_btx0(sw, sh, qidx, pal_bgr)
    assert len(btx) == 4292, f"Expected 4292 bytes for 32x256 BTX0, got {len(btx)}"
    validate_btx0(btx)
    print("pack_ow_sprites.py self-test OK (16x128/32x256=4292B/64x512 BTX0 + 4x4 strip verified)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_pack = sub.add_parser("pack", help="Pack sprite directory into .bwsp (and optional eidos mod)")
    p_pack.add_argument("--input", required=True, type=pathlib.Path)
    p_pack.add_argument("--output", required=True, type=pathlib.Path)
    p_pack.add_argument("--layout", default="auto", choices=("auto", "4x4", "4x2", "2x4", "1x8"))
    p_pack.add_argument("--offsets", type=pathlib.Path, default=None)
    p_pack.add_argument("--mod-dir", type=pathlib.Path, default=None)

    p_insp = sub.add_parser("inspect", help="Validate and summarize a .bwsp pack")
    p_insp.add_argument("file", type=pathlib.Path)

    sub.add_parser("self-test", help="Run built-in BTX0 and sprite slicer self-test")

    args = ap.parse_args()
    if args.cmd == "self-test":
        return run_self_test()
    if args.cmd == "inspect":
        inspect_bwsp(args.file)
        return 0
    pack_directory(args.input, args.output, args.layout, args.offsets, args.mod_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
