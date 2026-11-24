# Pokémon Black & White Android Port — Master Technical Plan & Roadmap

## 1. Core Architecture & Upstream Strategy
- **Engine & Android Base:** Fork of `Project-Holon/eidos` (`pokebw-galaxyfold8-ls`, `bw-fold` branch) — CMake + Ninja + SDL3 DS hardware simulator with Android `arm64-v8a` build support (`android/build.py`) and native mod loader (`kenon`/`morphe`).
- **Decompiled C Source:** Paired with sibling fork `~/aletheia` (`Project-Holon/aletheia`, 64-bit C decompilation of *Pokémon Black* & *White*), linked via `-DALETHEIA_ROOT=../aletheia`.
- **ROM Strategy:** Validates clean vanilla *Black 1* / *White 1* (USA/EUR) ROMs at first boot; delivers all gameplay upgrades in C and custom sprites via bundled `eidos` mod archives.
- **Platinum Code Reuse:** Ports `gen5/` Touch Battle HUD (`Btlv`/`BtlProc`), Modern EXP Share, effectiveness badges, and ImGui Gear companion UI from `pokeplatinum-galaxyfold8-ls`.
- **Follower & Wild Sprite Sources:** Combines TooManyLuigis's Gen 5 Follower Sprites v2.1b + `kaitrbl`'s *Following BW2* `NSBTX` frame layout + Hobo's *Following Black* spacing offsets, driven by native C hooks in `aletheia` (no map-script binary patches).

## 2. Phased Feature Roadmap (Priority First, Easiest -> Hardest)

### Phase 0: Foundation & Android CI (Done)
1. Fork `eidos` + `aletheia`, establish `AGENTS.md` and `BW_PORT_PLAN.md`.
2. Configure `.github/workflows/build_android.yml` (`arm64-v8a` APK, `VERSION_CODE=$((9200000 + RUN_NUMBER))`, persistent release signing).
3. Add Dear ImGui overlay + persistent INI settings (`pokebw_settings.ini`) over SDL3 + GLES 3.0.

### Phase 1: Top-Priority Features (Trivial -> Medium)
1. **Trade Evolution Fixes (Done):** Lv. 36/37+ for pure trade evolutions; level-up with held item for item-trade evolutions; party-counterpart/Lv. 36+ for Karrablast & Shelmet.
2. **Modern EXP Share (Done):** Gen 6+ party-wide EXP (100% participants, 50% bench) with Gen 5 scaling and EV yields preserved.
3. **HM Without Teaching / HM Freedom (Done):** Use Cut, Fly, Surf, Strength, Waterfall, Dive, and Flash from any compatible party member without occupying a moveslot (plus optional badge bypass).
4. **Gotta Catch 'Em All! Mode (Done):** Merge Black/White exclusives, inject Gens 1-4 National Dex species into Unova routes/phenomena, and enable built-in Victini, Zorua, Zoroark, Keldeo, Meloetta, Genesect, and opposite-legendary encounters.
5. **Native `eidos` One-Screen Mode + Unified ImGui Settings Bridge (Done):** Uses `eidos`'s built-in aspect-adaptive widescreen 3D (`--layout one`), 15 one-screen UI recipes, and on-demand `C-GEAR` sheet instead of a competing PiP; exposes `eidos` layout, graphics, audio, and QoL options live inside our ImGui overlay.
6. **Portable PC Box Access / PC Anywhere (Done):** Launch Unova `BoxApp` (`ov170`, Organize Boxes or Move Items) from the ImGui overlay anywhere while free-roaming on the overworld with cutscene/battle/comm/wipe guardrails (renders natively in `eidos`'s one-screen PC recipe).

### Phase 2: Fast-Follow QoL, Battle HUD & Gear Radar (Low -> Medium)
1. **QoL & Cheats (Done):** Instant Text and Fast/Seamless Map Transitions are wired from `eidos`; added Hard Level Cap (Unova badge caps), No Random Encounters, Always Bite When Fishing, and Season & Clock Switcher.
2. ~~**Gen 5 Battle HUD Enhancements (Low-Medium):**~~ **Done (`bw-fold`)**: Added Gen 5 `4x`/`2x`/`1/2`/`1/4`/`0x` move-card effectiveness pills directly in `engine/ui/recipe_battle.c` (Teravolt/Turboblaze/Mold Breaker, Scrappy, and defender ability-aware, Illusion-safe), toggled via `Battle Effectiveness Pills` (`POKEBW_SET_TYPE_EFFECTIVENESS`) in the ImGui `QoL` tab.
3. **Gear Companion & Unova Route Radar (Medium):** Party move-pill cards, save slots, and live encounter radar supporting Seasons, Dark Grass, and Phenomena.

### Phase 3: 649-Species Sprites, Following Pokémon & Overworld Wilds (Medium-High -> High)
1. **649-Species Sprite Archive (Medium-High):** Python build pipeline packing normal + shiny + form `NSBTX` sprites into a built-in `eidos` NARC VFS mod pack (`sdk/vfs_narc.c`).
2. **Native C Following Pokémon (High):** `FieldPlayer` C follower controller handling Unova curved cameras, 3D bridges, auto-hide during `ScriptContext` locks, and leak-free task cleanup.
3. **Overworld Wild Pokémon (High):** Visible roaming grass/cave/water spawns (`1-6` density, live shiny visuals, season-aware tables, touch encounters).
4. **Android Link Play Polish (Medium):** 32-bit DS pointer safety is already guaranteed by the `wasm32` -> `wasm2c` pipeline, and wireless Link Play / co-op is already implemented in `eidos` over `metaxy`; validate and polish Android Link Play flows.
