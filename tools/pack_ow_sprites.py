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


def _unfilter_scanlines(
    decomp: bytes, w: int, h: int, stride: int, bpp_filter: int, path: pathlib.Path
) -> List[bytes]:
    rows: List[bytes] = []
    prev = bytearray(stride)
    idx = 0
    for _ in range(h):
        flt = decomp[idx]
        idx += 1
        row = bytearray(decomp[idx : idx + stride])
        idx += stride
        if flt == 1:
            for x in range(bpp_filter, stride):
                row[x] = (row[x] + row[x - bpp_filter]) & 0xFF
        elif flt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 0xFF
        elif flt == 3:
            for x in range(stride):
                a = row[x - bpp_filter] if x >= bpp_filter else 0
                row[x] = (row[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif flt == 4:
            for x in range(stride):
                a = row[x - bpp_filter] if x >= bpp_filter else 0
                b = prev[x]
                c = prev[x - bpp_filter] if x >= bpp_filter else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[x] = (row[x] + pr) & 0xFF
        elif flt != 0:
            raise ValueError(f"{path}: unsupported PNG filter {flt}")
        rows.append(bytes(row))
        prev = row
    return rows


def _parse_png_raw(
    path: pathlib.Path,
) -> Tuple[int, int, int, int, List[bytes], List[Tuple[int, int, int, int]]]:
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")
    pos = 8
    w = h = bit_depth = color_type = 0
    interlace = 0
    idat = bytearray()
    plte = b""
    trns = b""
    while pos + 8 <= len(raw):
        clen = struct.unpack_from(">I", raw, pos)[0]
        ctype = raw[pos + 4 : pos + 8]
        cdata = raw[pos + 8 : pos + 8 + clen]
        pos += 12 + clen
        if ctype == b"IHDR":
            w, h, bit_depth, color_type, _, _, interlace = struct.unpack_from(">IIBBBBB", cdata, 0)
        elif ctype == b"PLTE":
            plte = cdata
        elif ctype == b"tRNS":
            trns = cdata
        elif ctype == b"IDAT":
            idat += cdata
        elif ctype == b"IEND":
            break
    if interlace != 0:
        raise RuntimeError(f"{path}: interlaced PNG is not supported cleanly without Pillow")
    if color_type == 3:
        if bit_depth not in (1, 2, 4, 8):
            raise RuntimeError(f"{path}: unsupported indexed PNG bit depth {bit_depth}")
        stride = (w * bit_depth + 7) // 8
        bpp_filter = 1
    elif bit_depth == 8 and color_type in (2, 6):
        bpp_filter = 4 if color_type == 6 else 3
        stride = w * bpp_filter
    else:
        raise RuntimeError(
            f"{path}: pure-Python PNG decoder supports 8-bit RGB/RGBA or 1/2/4/8-bit indexed PNG (got depth={bit_depth}, color_type={color_type})"
        )
    decomp = zlib.decompress(bytes(idat))
    rows = _unfilter_scanlines(decomp, w, h, stride, bpp_filter, path)
    pal_rgba: List[Tuple[int, int, int, int]] = []
    for i in range(0, len(plte) - 2, 3):
        idx_c = i // 3
        alpha = trns[idx_c] if idx_c < len(trns) else 255
        pal_rgba.append((plte[i], plte[i + 1], plte[i + 2], alpha))
    return w, h, bit_depth, color_type, rows, pal_rgba


def _unpack_indexed_rows(
    w: int, bit_depth: int, rows: List[bytes]
) -> List[int]:
    indices: List[int] = []
    if bit_depth == 8:
        for row in rows:
            indices.extend(row[:w])
    elif bit_depth == 4:
        for row in rows:
            for x in range(w):
                b = row[x >> 1]
                indices.append((b >> 4) & 0xF if (x & 1) == 0 else b & 0xF)
    elif bit_depth == 2:
        for row in rows:
            for x in range(w):
                b = row[x >> 2]
                shift = 6 - 2 * (x & 3)
                indices.append((b >> shift) & 0x3)
    elif bit_depth == 1:
        for row in rows:
            for x in range(w):
                b = row[x >> 3]
                indices.append((b >> (7 - (x & 7))) & 0x1)
    return indices


def load_indexed_png(
    path: pathlib.Path,
) -> Tuple[int, int, List[int], List[Tuple[int, int, int, int]]]:
    """Load an indexed PNG cleanly (1/2/4/8-bit color_type=3) preserving exact palette indices."""
    w, h, bit_depth, color_type, rows, pal_rgba = _parse_png_raw(path)
    if color_type != 3:
        raise ValueError(f"{path}: PNG is not indexed (color_type={color_type})")
    indices = _unpack_indexed_rows(w, bit_depth, rows)
    return w, h, indices, pal_rgba


def parse_jasc_pal(path: pathlib.Path) -> List[int]:
    """Parse a JASC-PAL 16-color palette into BGR555 u16 colors (index 0 forced transparent)."""
    lines = [
        ln.strip()
        for ln in path.read_text(encoding="utf-8", errors="replace").splitlines()
        if ln.strip()
    ]
    if len(lines) < 3 or lines[0] != "JASC-PAL":
        raise ValueError(f"{path}: not a valid JASC-PAL file")
    count = int(lines[2])
    pal: List[int] = []
    for ln in lines[3 : 3 + min(16, count)]:
        parts = ln.split()
        if len(parts) < 3:
            raise ValueError(f"{path}: malformed palette entry {ln!r}")
        r, g, b = int(parts[0]), int(parts[1]), int(parts[2])
        pal.append(rgba_to_bgr555(r, g, b))
    while len(pal) < 16:
        pal.append(0)
    pal[0] = 0
    return pal[:16]


def _load_png_pure_python(path: pathlib.Path) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    """Minimal RGBA/RGB/Indexed PNG fallback when Pillow is not installed."""
    w, h, bit_depth, color_type, rows, pal_rgba = _parse_png_raw(path)
    out: List[Tuple[int, int, int, int]] = []
    if color_type == 3:
        for idx_p in _unpack_indexed_rows(w, bit_depth, rows):
            if idx_p < len(pal_rgba):
                r, g, b, a = pal_rgba[idx_p]
                out.append((r, g, b, 0 if idx_p == 0 and a == 255 else a))
            else:
                out.append((0, 0, 0, 0))
        return w, h, out
    bpp = 4 if color_type == 6 else 3
    for row in rows:
        for x in range(0, w * bpp, bpp):
            out.append((row[x], row[x + 1], row[x + 2], row[x + 3] if bpp == 4 else 255))
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


def _pad_frames_strip(
    cw: int, ch: int, frames: List[List[object]], empty_val: object
) -> Tuple[int, int, List[object]]:
    """Pad or center-crop 8 square/rectangular cells into a 16/32/64x(8*target) vertical strip."""
    target = 16 if cw <= 16 and ch <= 16 else (32 if cw <= 32 and ch <= 32 else 64)
    strip: List[object] = []
    for fr in frames:
        if cw == target and ch == target:
            strip.extend(fr)
        else:
            pad_x = (target - cw) // 2
            pad_y = target - ch
            for ty in range(target):
                sy = ty - pad_y
                for tx in range(target):
                    sx = tx - pad_x
                    if 0 <= sx < cw and 0 <= sy < ch:
                        strip.append(fr[sy * cw + sx])
                    else:
                        strip.append(empty_val)
    return target, target * 8, strip


def _crop_cell(
    pixels: List[object], w: int, h: int, cx: int, cy: int, cw: int, ch: int, hflip: bool = False
) -> List[object]:
    out: List[object] = []
    for y in range(cy * ch, (cy + 1) * ch):
        row = pixels[y * w + cx * cw : y * w + (cx + 1) * cw]
        if hflip:
            row = row[::-1]
        out.extend(row)
    return out


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

    if layout == "1x8":
        cw, ch = w, h // 8
        frames = [_crop_cell(pixels, w, h, 0, i, cw, ch) for i in range(8)]
    elif layout == "4x4":
        # RPGM / PokeMMO / TooManyLuigis: rows Down(0), Left(1), Right(2), Up(3); cols 0,1 = stand, step
        cw, ch = w // 4, h // 4
        coords = [(0, 3), (1, 3), (0, 0), (1, 0), (0, 1), (1, 1), (0, 2), (1, 2)]
        frames = [_crop_cell(pixels, w, h, cx, cy, cw, ch) for cx, cy in coords]
    elif layout == "4x2":
        cw, ch = w // 2, h // 4
        coords = [(0, 3), (1, 3), (0, 0), (1, 0), (0, 1), (1, 1), (0, 2), (1, 2)]
        frames = [_crop_cell(pixels, w, h, cx, cy, cw, ch) for cx, cy in coords]
    elif layout == "2x4":
        cw, ch = w // 4, h // 2
        coords = [(3, 0), (3, 1), (0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1)]
        frames = [_crop_cell(pixels, w, h, cx, cy, cw, ch) for cx, cy in coords]
    else:
        raise ValueError(f"Unknown layout: {layout}")

    sw, sh, strip = _pad_frames_strip(cw, ch, frames, (0, 0, 0, 0))
    return sw, sh, strip  # type: ignore[return-value]


def encode_pokeemerald_overworld(
    png_path: pathlib.Path, pal_path: Optional[pathlib.Path] = None
) -> Tuple[bytes, int]:
    """
    Encode a rh-hideout/pokeemerald-expansion horizontal overworld strip
    (6-frame symmetric / 8-frame asymmetric / 9-frame NPC / 4 / 2 / 1 frames)
    and optional JASC-PAL palette into a Gen 5 animId=14 BTX0 texture.
    """
    POKEEMERALD_FRAME_MAP: Dict[int, List[Tuple[int, bool]]] = {
        6: [(2, False), (3, False), (0, False), (1, False), (4, False), (5, False), (4, True), (5, True)],
        8: [(2, False), (3, False), (0, False), (1, False), (4, False), (5, False), (6, False), (7, False)],
        9: [(1, False), (5, False), (0, False), (3, False), (2, False), (7, False), (2, True), (7, True)],
        4: [(1, False), (1, False), (0, False), (0, False), (2, False), (2, False), (3, False), (3, False)],
        2: [(0, False), (1, False), (0, False), (1, False), (0, False), (1, False), (0, True), (1, True)],
        1: [(0, False)] * 8,
    }

    indexed_ok = False
    w = h = 0
    indices: List[int] = []
    pal_rgba: List[Tuple[int, int, int, int]] = []
    try:
        w, h, indices, pal_rgba = load_indexed_png(png_path)
        if max(indices, default=0) < 16:
            indexed_ok = True
    except (ValueError, RuntimeError):
        indexed_ok = False

    if indexed_ok:
        ch = h
        if w % ch != 0:
            raise ValueError(f"{png_path}: unexpected pokeemerald strip size {w}x{h}")
        cw = ch
        n_frames = w // cw
        if n_frames not in POKEEMERALD_FRAME_MAP:
            raise ValueError(f"{png_path}: unsupported horizontal frame count {n_frames} ({w}x{h})")
        if pal_path is not None and pal_path.is_file():
            pal_bgr = parse_jasc_pal(pal_path)
        else:
            pal_bgr = [rgba_to_bgr555(r, g, b) for r, g, b, _ in pal_rgba[:16]]
            while len(pal_bgr) < 16:
                pal_bgr.append(0)
            pal_bgr[0] = 0
        frames = [
            _crop_cell(indices, w, h, fidx, 0, cw, ch, hflip=hflip)
            for fidx, hflip in POKEEMERALD_FRAME_MAP[n_frames]
        ]
        sw, sh, strip = _pad_frames_strip(cw, ch, frames, 0)
        btx = build_single_btx0(sw, sh, strip, pal_bgr)  # type: ignore[arg-type]
    else:
        w, h, pixels = load_rgba(png_path)
        ch = h
        if w % ch != 0:
            raise ValueError(f"{png_path}: unexpected pokeemerald strip size {w}x{h}")
        cw = ch
        n_frames = w // cw
        if n_frames not in POKEEMERALD_FRAME_MAP:
            raise ValueError(f"{png_path}: unsupported horizontal frame count {n_frames} ({w}x{h})")
        frames_px = [
            _crop_cell(pixels, w, h, fidx, 0, cw, ch, hflip=hflip)
            for fidx, hflip in POKEEMERALD_FRAME_MAP[n_frames]
        ]
        sw, sh, strip_px = _pad_frames_strip(cw, ch, frames_px, (0, 0, 0, 0))
        pal_bgr, qidx = quantize_palette_16(strip_px)  # type: ignore[arg-type]
        btx = build_single_btx0(sw, sh, qidx, pal_bgr)

    validate_btx0(btx)
    mdl_size = 1 if sw == 16 else (2 if sw == 64 else 0)
    return btx, mdl_size


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


def write_bwsp(
    entries: List[Tuple[int, int, int, bytes, int, int, int, int, int, int]],
    output_bwsp: pathlib.Path,
) -> int:
    """
    Write sorted (species, form, shiny) BWSP entries to `output_bwsp`.
    Each tuple: (species, form, shiny, btx, mdl_size, anim_id, off_x, off_y, off_z, shadow).
    """
    sorted_entries = sorted(entries, key=lambda e: (e[0], e[1], e[2]))
    header_size = 16
    entry_size = 20
    data_cursor = header_size + len(sorted_entries) * entry_size
    entries_buf = bytearray()
    blobs_buf = bytearray()

    for species, form, shiny, btx, mdl_size, anim_id, off_x, off_y, off_z, shadow in sorted_entries:
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
    header = struct.pack("<4sHHII", BWSP_MAGIC, BWSP_VERSION, 0, len(sorted_entries), 0)
    tmp = output_bwsp.with_name(output_bwsp.name + ".tmp")
    tmp.write_bytes(header + entries_buf + blobs_buf)
    os.replace(tmp, output_bwsp)
    return len(sorted_entries)


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

    records: List[Tuple[int, int, int, bytes, int, int, int, int, int, int]] = []
    for species, form, shiny in sorted(items.keys()):
        btx, mdl_size = items[(species, form, shiny)]
        meta = offsets.get(f"{species}_f{form}", offsets.get(str(species), {}))
        records.append(
            (
                species,
                form,
                shiny,
                btx,
                int(meta.get("mdl_size", mdl_size)),
                int(meta.get("anim_id", ANIM_ID_DEFAULT)),
                int(meta.get("off_x", 0)),
                int(meta.get("off_y", 0)),
                int(meta.get("off_z", 0)),
                int(meta.get("shadow", 1)),
            )
        )

    packed = write_bwsp(records, output_bwsp)

    if mod_dir is not None:
        manifest = mod_dir / "eidos_mod.txt"
        mod_dir.mkdir(parents=True, exist_ok=True)
        manifest.write_text("name = PokeBW 649 Overworld Sprites\norder = 5\n", encoding="utf-8")
        (mod_dir / "pokebw_ow_sprites.bwsp").write_bytes(output_bwsp.read_bytes())

    print(f"Packed {packed} sprites into {output_bwsp} ({output_bwsp.stat().st_size} bytes)")
    return packed


def inspect_bwsp(path: pathlib.Path) -> Tuple[int, int, int, int]:
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
    return count, normal, shiny, forms


def _write_test_indexed_png(path: pathlib.Path, w: int, h: int, indices: List[int], plte: bytes) -> None:
    def chunk(ctype: bytes, data: bytes) -> bytes:
        crc = zlib.crc32(ctype + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", w, h, 4, 3, 0, 0, 0)
    raw_rows = bytearray()
    stride = (w + 1) // 2
    for y in range(h):
        raw_rows.append(0)
        row = bytearray(stride)
        for x in range(w):
            v = indices[y * w + x] & 0xF
            if (x & 1) == 0:
                row[x >> 1] |= v << 4
            else:
                row[x >> 1] |= v
        raw_rows.extend(row)
    idat = zlib.compress(bytes(raw_rows))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"PLTE", plte) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")
    )


def run_self_test() -> int:
    import tempfile

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

    # 3. Verify 4bpp indexed PNG + JASC-PAL + pokeemerald 6-frame (mirrored Right) & 8-frame (asym Right)
    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        plte = bytes([i * 16, (255 - i * 8) & 0xFF, (i * 23) & 0xFF] for i in range(16)) if False else b"".join(
            bytes([i * 16, (255 - i * 8) & 0xFF, (i * 23) & 0xFF]) for i in range(16)
        )
        pal_file = tmp / "overworld_normal.pal"
        pal_lines = ["JASC-PAL", "0100", "16"] + [f"{i * 16} {(i * 8) & 255} {(255 - i * 12) & 255}" for i in range(16)]
        pal_file.write_text("\n".join(pal_lines) + "\n", encoding="utf-8")
        jasc_bgr = parse_jasc_pal(pal_file)
        assert jasc_bgr[0] == 0 and len(jasc_bgr) == 16

        # 6-frame 192x32 indexed PNG: put pixel value 7 at (frame4, x=1, y=5) -> Right0 must mirror to x=30, y=5
        idx6 = [0] * (192 * 32)
        idx6[5 * 192 + (4 * 32 + 1)] = 7
        png6 = tmp / "ow6.png"
        _write_test_indexed_png(png6, 192, 32, idx6, plte)
        rw, rh, ridx, _ = load_indexed_png(png6)
        assert (rw, rh) == (192, 32) and ridx[5 * 192 + (4 * 32 + 1)] == 7
        btx6, mdl6 = encode_pokeemerald_overworld(png6, pal_file)
        assert len(btx6) == 4292 and mdl6 == 0
        # Right0 is frame 6 (y = 6*32 + 5 = 197), x = 30 -> nibble 0 (even pixel) at byte 197*16 + 15
        tex_off = 0x14 + 0x90
        assert (btx6[tex_off + (6 * 32 + 5) * 16 + 15] & 0xF) == 7

        # 8-frame 256x32 asymmetric indexed PNG: put pixel value 9 at (frame6, x=2, y=4) -> Right0 has x=2 unflipped
        idx8 = [0] * (256 * 32)
        idx8[4 * 256 + (6 * 32 + 2)] = 9
        png8 = tmp / "ow8.png"
        _write_test_indexed_png(png8, 256, 32, idx8, plte)
        btx8, mdl8 = encode_pokeemerald_overworld(png8, pal_file)
        assert len(btx8) == 4292 and mdl8 == 0
        assert (btx8[tex_off + (6 * 32 + 4) * 16 + 1] & 0xF) == 9

    print("pack_ow_sprites.py self-test OK (4bpp indexed PNG, JASC-PAL, 6/8-frame pokeemerald + 4x4 BTX0 verified)")
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
