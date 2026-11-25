#!/usr/bin/env python3
"""
tools/fetch_ow_sprites.py -- Fetch and pack 649-species 4-direction Pokémon
overworld sprites (normal + shiny + Gen 5 alternate forms) from
rh-hideout/pokeemerald-expansion (pinned release tag) into pokebw_ow_sprites.bwsp.

The last line printed to stdout is the generated .bwsp file path.

Usage:
  python3 tools/fetch_ow_sprites.py [--cache DIR] [--output PATH] [--min-normal 649] [--force]
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import pack_ow_sprites as packer  # noqa: E402

REPO = "https://github.com/rh-hideout/pokeemerald-expansion.git"
TAG = "expansion/1.17.2"
PACK_REV = "v1"
PACK_NAME = f"pokebw_ow_sprites-{TAG.split('/')[-1]}-{PACK_REV}.bwsp"
MAX_SLOTS = 2048

BASE_SPECIES_ALIASES = {
    "UNOWN_A",
    "CASTFORM_NORMAL",
    "DEOXYS_NORMAL",
    "BURMY_PLANT",
    "WORMADAM_PLANT",
    "MOTHIM_PLANT",
    "CHERRIM_OVERCAST",
    "SHELLOS_WEST",
    "GASTRODON_WEST",
    "ROTOM",
    "GIRATINA_ALTERED",
    "SHAYMIN_LAND",
    "ARCEUS_NORMAL",
    "BASCULIN_RED_STRIPED",
    "DARMANITAN_STANDARD",
    "DEERLING_SPRING",
    "SAWSBUCK_SPRING",
    "TORNADUS_INCARNATE",
    "THUNDURUS_INCARNATE",
    "LANDORUS_INCARNATE",
    "KYUREM",
    "KELDEO_ORDINARY",
    "MELOETTA_ARIA",
    "GENESECT",
}

GEN5_ALT_FORMS: Dict[str, int] = {
    **{f"UNOWN_{chr(ord('B') + i)}": i + 1 for i in range(25)},
    "UNOWN_EXCLAMATION": 26,
    "UNOWN_QUESTION": 27,
    "CASTFORM_SUNNY": 1,
    "CASTFORM_RAINY": 2,
    "CASTFORM_SNOWY": 3,
    "DEOXYS_ATTACK": 1,
    "DEOXYS_DEFENSE": 2,
    "DEOXYS_SPEED": 3,
    "BURMY_SANDY": 1,
    "BURMY_TRASH": 2,
    "WORMADAM_SANDY": 1,
    "WORMADAM_TRASH": 2,
    "CHERRIM_SUNSHINE": 1,
    "SHELLOS_EAST": 1,
    "GASTRODON_EAST": 1,
    "ROTOM_HEAT": 1,
    "ROTOM_WASH": 2,
    "ROTOM_FROST": 3,
    "ROTOM_FAN": 4,
    "ROTOM_MOW": 5,
    "GIRATINA_ORIGIN": 1,
    "SHAYMIN_SKY": 1,
    "ARCEUS_FIGHTING": 1,
    "ARCEUS_FLYING": 2,
    "ARCEUS_POISON": 3,
    "ARCEUS_GROUND": 4,
    "ARCEUS_ROCK": 5,
    "ARCEUS_BUG": 6,
    "ARCEUS_GHOST": 7,
    "ARCEUS_STEEL": 8,
    "ARCEUS_FIRE": 9,
    "ARCEUS_WATER": 10,
    "ARCEUS_GRASS": 11,
    "ARCEUS_ELECTRIC": 12,
    "ARCEUS_PSYCHIC": 13,
    "ARCEUS_ICE": 14,
    "ARCEUS_DRAGON": 15,
    "ARCEUS_DARK": 16,
    "BASCULIN_BLUE_STRIPED": 1,
    "DARMANITAN_ZEN": 1,
    "DEERLING_SUMMER": 1,
    "DEERLING_AUTUMN": 2,
    "DEERLING_WINTER": 3,
    "SAWSBUCK_SUMMER": 1,
    "SAWSBUCK_AUTUMN": 2,
    "SAWSBUCK_WINTER": 3,
    "TORNADUS_THERIAN": 1,
    "THUNDURUS_THERIAN": 1,
    "LANDORUS_THERIAN": 1,
    "KYUREM_WHITE": 1,
    "KYUREM_BLACK": 2,
    "KELDEO_RESOLUTE": 1,
    "MELOETTA_PIROUETTE": 1,
    "GENESECT_DOUSE": 1,
    "GENESECT_SHOCK": 2,
    "GENESECT_BURN": 3,
    "GENESECT_CHILL": 4,
}


def default_cache_dir() -> pathlib.Path:
    env = os.environ.get("EIDOS_OW_SPRITES_CACHE")
    if env:
        return pathlib.Path(env)
    if os.name == "nt":
        base = pathlib.Path(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"))
        return base / "eidos" / "ow_sprites"
    if sys.platform == "darwin":
        return pathlib.Path.home() / "Library" / "Caches" / "eidos" / "ow_sprites"
    base = pathlib.Path(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"))
    return base / "eidos" / "ow_sprites"


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def run_git(args: List[str], cwd: Optional[pathlib.Path] = None) -> None:
    cmd = ["git"] + args
    res = subprocess.run(cmd, cwd=str(cwd) if cwd else None, check=False, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({res.returncode}):\n{res.stderr.strip()}")


def ensure_sparse_checkout(src_dir: pathlib.Path, force: bool) -> None:
    sentinel = src_dir / "include" / "constants" / "pokedex.h"
    if sentinel.is_file() and not force:
        return
    if src_dir.exists():
        shutil.rmtree(src_dir)
    src_dir.parent.mkdir(parents=True, exist_ok=True)
    print(f"fetch_ow_sprites: cloning sparse checkout {REPO} ({TAG})")
    run_git(
        [
            "clone",
            "--depth",
            "1",
            "--filter=blob:none",
            "--no-checkout",
            "--branch",
            TAG,
            REPO,
            str(src_dir),
        ]
    )
    run_git(["sparse-checkout", "init", "--no-cone"], cwd=src_dir)
    run_git(
        [
            "sparse-checkout",
            "set",
            "/include/constants/pokedex.h",
            "/src/data/graphics/pokemon.h",
            "/src/data/object_events/object_event_pic_tables_followers.h",
            "/src/data/pokemon/species_info/gen_1_families.h",
            "/src/data/pokemon/species_info/gen_2_families.h",
            "/src/data/pokemon/species_info/gen_3_families.h",
            "/src/data/pokemon/species_info/gen_4_families.h",
            "/src/data/pokemon/species_info/gen_5_families.h",
            "/graphics/pokemon/**/overworld.png",
            "/graphics/pokemon/**/overworld_normal.pal",
            "/graphics/pokemon/**/overworld_shiny.pal",
            "/graphics/pokemon/**/normal.pal",
            "/graphics/pokemon/**/shiny.pal",
        ],
        cwd=src_dir,
    )
    run_git(["checkout"], cwd=src_dir)


def parse_pokedex_h(path: pathlib.Path) -> Dict[str, int]:
    """Return mapping `DEX_NAME` (without `NATIONAL_DEX_` prefix) -> National Dex integer."""
    text = strip_c_comments(path.read_text(encoding="utf-8", errors="replace"))
    m = re.search(r"enum\s+NationalDexOrder\s*\{([^}]+)\}", text)
    if not m:
        raise RuntimeError(f"Could not find NationalDexOrder enum in {path}")
    dex_map: Dict[str, int] = {}
    cur = 0
    for item in m.group(1).split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            name, val = [x.strip() for x in item.split("=", 1)]
            if re.fullmatch(r"(?:0x[0-9a-fA-F]+|\d+)", val):
                cur = int(val, 0)
            elif val.startswith("NATIONAL_DEX_") and val[13:] in dex_map:
                cur = dex_map[val[13:]]
        else:
            name = item
        if name.startswith("NATIONAL_DEX_"):
            dex_map[name[13:]] = cur
        cur += 1
    return dex_map


def parse_graphics_pokemon_h(path: pathlib.Path) -> Dict[str, str]:
    """Return symbol -> relative asset path inside pokeemerald-expansion."""
    text = strip_c_comments(path.read_text(encoding="utf-8", errors="replace"))
    sym_map: Dict[str, str] = {}
    pat = re.compile(
        r"\b(g(?:ObjectEventPic|OverworldPalette|ShinyOverworldPalette|MonPalette|MonShinyPalette)_[A-Za-z0-9_]+)"
        r'\s*\[\]\s*=\s*INCGFX_[A-Z0-9_]+\s*\(\s*"([^"]+)"'
    )
    for m in pat.finditer(text):
        sym, rel_path = m.group(1), m.group(2)
        # Prefer first definition (the non-GBA default inside #if !P_GBA_STYLE_SPECIES_GFX)
        if sym not in sym_map:
            sym_map[sym] = rel_path
    return sym_map


def parse_pic_tables_h(path: pathlib.Path) -> Dict[str, str]:
    """Return sPicTable_<Name> -> gObjectEventPic_<Name> symbol."""
    text = strip_c_comments(path.read_text(encoding="utf-8", errors="replace"))
    tbl_map: Dict[str, str] = {}
    pat = re.compile(
        r"\b(sPicTable_[A-Za-z0-9_]+)\s*\[\]\s*=\s*\{([^}]+)\}", re.S
    )
    for m in pat.finditer(text):
        tbl_name, body = m.group(1), m.group(2)
        pic_m = re.search(r"\b(gObjectEventPic_[A-Za-z0-9_]+)\b", body)
        if pic_m:
            tbl_map[tbl_name] = pic_m.group(1)
    return tbl_map


def _extract_balanced_call(text: str, macro_name: str) -> Optional[List[str]]:
    m = re.search(rf"\b{re.escape(macro_name)}\s*\(", text)
    if not m:
        return None
    i = m.end()
    depth = 1
    arg_start = i
    args: List[str] = []
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(text[arg_start:i].strip())
                break
        elif ch == "," and depth == 1:
            args.append(text[arg_start:i].strip())
            arg_start = i + 1
        i += 1
    return args


def _parse_local_macros(text: str) -> Tuple[Dict[str, str], Dict[str, Tuple[List[str], str]]]:
    """Parse local #define object/function macros that expand to species structs."""
    obj_macros: Dict[str, str] = {}
    fn_macros: Dict[str, Tuple[List[str], str]] = {}
    i = 0
    raw_lines = text.splitlines()
    while i < len(raw_lines):
        ln = raw_lines[i]
        m_fn = re.match(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\)(.*)$", ln)
        m_obj = None if m_fn else re.match(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$", ln)
        if m_fn or m_obj:
            chunks = [(m_fn.group(3) if m_fn else m_obj.group(2))]
            while chunks[-1].rstrip().endswith("\\") and i + 1 < len(raw_lines):
                chunks[-1] = chunks[-1].rstrip()[:-1]
                i += 1
                chunks.append(raw_lines[i])
            body = strip_c_comments("\n".join(chunks)).strip()
            if "OVERWORLD" in body or ".natDexNum" in body:
                if m_fn:
                    params = [p.strip() for p in m_fn.group(2).split(",") if p.strip()]
                    fn_macros[m_fn.group(1)] = (params, body)
                elif m_obj:
                    obj_macros[m_obj.group(1)] = body
        i += 1
    return obj_macros, fn_macros


def _expand_species_macros(text: str) -> str:
    obj_macros, fn_macros = _parse_local_macros(text)
    if not obj_macros and not fn_macros:
        return strip_c_comments(text)
    clean = strip_c_comments(text)

    # Expand function-like macro invocations matching known species macros
    for name, (params, body) in fn_macros.items():
        pat = re.compile(rf"(\[SPECIES_[A-Z0-9_]+\]\s*=\s*){re.escape(name)}\s*\(")
        out_parts: List[str] = []
        last = 0
        for m in pat.finditer(clean):
            i = m.end()
            depth = 1
            arg_start = i
            args: List[str] = []
            while i < len(clean) and depth > 0:
                if clean[i] == "(":
                    depth += 1
                elif clean[i] == ")":
                    depth -= 1
                    if depth == 0:
                        args.append(clean[arg_start:i].strip())
                        i += 1
                        break
                elif clean[i] == "," and depth == 1:
                    args.append(clean[arg_start:i].strip())
                    arg_start = i + 1
                i += 1
            if len(args) == len(params):
                expanded = body
                for p, a in zip(params, args):
                    expanded = re.sub(rf"\b{re.escape(p)}\b", lambda _m, rep=a: rep, expanded)
                expanded = re.sub(r"\s*##\s*", "", expanded)
                out_parts.append(clean[last : m.start()])
                out_parts.append(m.group(1) + expanded)
                last = i
        if last > 0:
            out_parts.append(clean[last:])
            clean = "".join(out_parts)

    # Expand object-like species macros (e.g. MOTHIM_SPECIES_INFO)
    for name, body in obj_macros.items():
        clean = re.sub(
            rf"(\[SPECIES_[A-Z0-9_]+\]\s*=\s*){re.escape(name)}\b",
            lambda m, b=body: m.group(1) + b,
            clean,
        )
    return clean


def _iter_species_blocks(path: pathlib.Path) -> List[Tuple[str, str]]:
    text = _expand_species_macros(path.read_text(encoding="utf-8", errors="replace"))
    blocks: List[Tuple[str, str]] = []
    pat = re.compile(r"\[SPECIES_([A-Z0-9_]+)\]\s*=\s*\{")
    for m in pat.finditer(text):
        key = m.group(1)
        pos = m.end()
        depth = 1
        while pos < len(text) and depth > 0:
            if text[pos] == "{":
                depth += 1
            elif text[pos] == "}":
                depth -= 1
            pos += 1
        blocks.append((key, text[m.end() : pos - 1]))
    return blocks


def _shadow_from_macro(macro: str) -> int:
    if "NONE" in macro:
        return 0
    if "SHADOW_SIZE_L" in macro or "SHADOW_SIZE_XL" in macro:
        return 2
    return 1


def build_pack_from_checkout(
    src_dir: pathlib.Path, out_bwsp: pathlib.Path, min_normal: int
) -> Tuple[int, int, int, int]:
    dex_map = parse_pokedex_h(src_dir / "include" / "constants" / "pokedex.h")
    sym_assets = parse_graphics_pokemon_h(src_dir / "src" / "data" / "graphics" / "pokemon.h")
    pic_tables = parse_pic_tables_h(
        src_dir / "src" / "data" / "object_events" / "object_event_pic_tables_followers.h"
    )

    species_blocks: Dict[str, str] = {}
    for gen in range(1, 6):
        fpath = src_dir / "src" / "data" / "pokemon" / "species_info" / f"gen_{gen}_families.h"
        for key, body in _iter_species_blocks(fpath):
            species_blocks[key] = body

    # Helper to resolve NATIONAL_DEX name for a species key (handling .baseSpecies inheritance)
    key_to_dex_name: Dict[str, str] = {}
    for key, body in species_blocks.items():
        nm = re.search(r"\.natDexNum\s*=\s*NATIONAL_DEX_([A-Z0-9_]+)", body)
        if nm:
            key_to_dex_name[key] = nm.group(1)
    for key, body in species_blocks.items():
        if key not in key_to_dex_name:
            bm = re.search(r"\.baseSpecies\s*=\s*SPECIES_([A-Z0-9_]+)", body)
            if bm and bm.group(1) in key_to_dex_name:
                key_to_dex_name[key] = key_to_dex_name[bm.group(1)]

    entries_by_key: Dict[Tuple[int, int, int], Tuple[int, int, int, bytes, int, int, int, int, int, int]] = {}
    missing_assets: List[str] = []

    for key, body in species_blocks.items():
        dex_name = key_to_dex_name.get(key)
        if not dex_name:
            continue
        dex_num = dex_map.get(dex_name, 0)
        if dex_num < 1 or dex_num > 649:
            continue

        if key == dex_name or key in BASE_SPECIES_ALIASES:
            form_idx = 0
        elif key in GEN5_ALT_FORMS:
            form_idx = GEN5_ALT_FORMS[key]
        else:
            continue

        ow_args = _extract_balanced_call(body, "OVERWORLD")
        if ow_args is None or len(ow_args) < 3:
            missing_assets.append(f"{key}(no OVERWORLD macro)")
            continue

        pic_tbl = ow_args[0]
        shadow_macro = ow_args[2]
        ow_pal_norm_sym = ow_args[5] if len(ow_args) > 5 else ""
        ow_pal_shiny_sym = ow_args[6] if len(ow_args) > 6 else ""

        # Handle simple macro aliases like P_FEMALE_... / #if wrappers or direct gObjectEventPic
        pic_sym = pic_tables.get(pic_tbl, pic_tbl)
        png_rel = sym_assets.get(pic_sym)
        if not png_rel:
            cand = src_dir / "graphics" / "pokemon" / dex_name.lower() / "overworld.png"
            if cand.is_file():
                png_path = cand
            else:
                missing_assets.append(f"{key}(no png for {pic_tbl})")
                continue
        else:
            png_path = src_dir / png_rel
        if not png_path.is_file():
            missing_assets.append(f"{key}(missing {png_path.relative_to(src_dir)})")
            continue

        mon_pal_m = re.search(r"\.palette\s*=\s*(gMonPalette_[A-Za-z0-9_]+)", body)
        mon_spal_m = re.search(r"\.shinyPalette\s*=\s*(gMonShinyPalette_[A-Za-z0-9_]+)", body)

        def pick_pal(sym: str, fallback_sym: str, names: Tuple[str, ...]) -> Optional[pathlib.Path]:
            for s in (sym, fallback_sym):
                if s and s in sym_assets:
                    p = src_dir / sym_assets[s]
                    if p.is_file():
                        return p
            for fn in names:
                p = png_path.parent / fn
                if p.is_file():
                    return p
            return None

        pal_norm = pick_pal(
            ow_pal_norm_sym,
            mon_pal_m.group(1) if mon_pal_m else "",
            ("overworld_normal.pal", "normal.pal"),
        )
        pal_shiny = pick_pal(
            ow_pal_shiny_sym,
            mon_spal_m.group(1) if mon_spal_m else "",
            ("overworld_shiny.pal", "shiny.pal"),
        )

        shadow_size = _shadow_from_macro(shadow_macro)
        for shiny_flag, pal_file in ((0, pal_norm), (1, pal_shiny or pal_norm)):
            rec_key = (dex_num, form_idx, shiny_flag)
            if rec_key in entries_by_key:
                continue
            btx, mdl_size = packer.encode_pokeemerald_overworld(png_path, pal_file)
            entries_by_key[rec_key] = (
                dex_num,
                form_idx,
                shiny_flag,
                btx,
                mdl_size,
                packer.ANIM_ID_DEFAULT,
                0,
                0,
                0,
                shadow_size,
            )

    if len(entries_by_key) > MAX_SLOTS:
        raise RuntimeError(f"Packed sprite count {len(entries_by_key)} exceeds OW_MAX_SLOTS={MAX_SLOTS}")

    packer.write_bwsp(list(entries_by_key.values()), out_bwsp)
    count, normal, shiny, forms = packer.inspect_bwsp(out_bwsp)
    if missing_assets:
        print(f"fetch_ow_sprites: skipped/missing ({len(missing_assets)}): {missing_assets[:10]}", file=sys.stderr)
    if normal < min_normal or shiny < min_normal:
        present = {sp for sp, fm, sh in entries_by_key if fm == 0 and sh == 0}
        absent = [sp for sp in range(1, 650) if sp not in present]
        raise RuntimeError(
            f"Incomplete sprite pack: normal={normal}, shiny={shiny} (expected >= {min_normal}); "
            f"missing base dex={absent[:20]}"
        )
    return count, normal, shiny, forms


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cache", type=pathlib.Path, default=None)
    ap.add_argument("--output", type=pathlib.Path, default=None)
    ap.add_argument("--min-normal", type=int, default=649)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args(argv)

    cache = args.cache if args.cache is not None else default_cache_dir()
    cache.mkdir(parents=True, exist_ok=True)
    cached_pack = cache / PACK_NAME
    src_dir = cache / f"pokeemerald-expansion-{TAG.split('/')[-1]}"

    need_build = args.force or not cached_pack.is_file()
    if not need_build:
        try:
            count, normal, shiny, _ = packer.inspect_bwsp(cached_pack)
            if normal < args.min_normal or shiny < args.min_normal or count > MAX_SLOTS:
                need_build = True
        except Exception:
            need_build = True

    if need_build:
        ensure_sparse_checkout(src_dir, force=args.force)
        build_pack_from_checkout(src_dir, cached_pack, args.min_normal)

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(cached_pack, args.output)
        out_path = args.output
    else:
        out_path = cached_pack

    print(str(out_path).replace("\\", "/"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
