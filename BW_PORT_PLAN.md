# Pokémon Black & White Android Port — Master Technical Plan & Roadmap

## 1. Core Architecture & Upstream Strategy
- **Engine & Android Base:** Fork of `Project-Holon/eidos` (`pokebw-galaxyfold8-ls`, `bw-fold` branch) — CMake + Ninja + SDL3 DS hardware simulator with Android `arm64-v8a` build support (`android/build.py`) and native mod loader (`kenon`/`morphe`).
- **Decompiled C Source:** Paired with sibling fork `~/aletheia` (`Project-Holon/aletheia`, 64-bit C decompilation of *Pokémon Black* & *White*), linked via `-DALETHEIA_ROOT=../aletheia`.
- **ROM Strategy:** Validates clean vanilla *Black 1* / *White 1* (USA/EUR) ROMs at first boot; delivers all gameplay upgrades in C and custom sprites via bundled `eidos` mod archives.
- **Platinum Code Reuse:** Ports `gen5/` Touch Battle HUD (`Btlv`/`BtlProc`), Modern EXP Share, effectiveness badges, and ImGui Gear companion UI from `pokeplatinum-galaxyfold8-ls`.
- **Follower & Wild Sprite Sources:** Combines TooManyLuigis's Gen 5 Follower Sprites v2.1b + `kaitrbl`'s *Following BW2* `NSBTX` frame layout + Hobo's *Following Black* spacing offsets, driven by native C hooks in `aletheia` (no map-script binary patches).

## 2. Phased Feature Roadmap (Priority First, Easiest -> Hardest)

### Phase 0: Foundation & Android CI (Low)
1. Fork `eidos` + `aletheia`, establish `AGENTS.md` and `BW_PORT_PLAN.md`.
2. Configure `.github/workflows/build_android.yml` (`arm64-v8a` APK, `VERSION_CODE=$((9200000 + RUN_NUMBER))`).
3. Add Dear ImGui overlay + persistent JSON/INI `Settings` config over SDL3.

### Phase 1: Top-Priority Features (Trivial -> Medium)
1. **Trade Evolution Fixes (Trivial):** Lv. 36/37+ for pure trade evolutions; level-up with held item for item-trade evolutions; party-counterpart/Lv. 36+ for Karrablast & Shelmet.
2. **Modern EXP Share (Low):** Gen 6+ party-wide EXP (100% participants, 50% bench), ported from Platinum's `pokebw2` patch.
3. **HM Without Teaching / HM Freedom (Low-Medium):** Use Cut, Fly, Surf, Strength, Waterfall, Dive, and Flash from any compatible party member without occupying a moveslot (optional badge bypass).
4. **Gotta Catch 'Em All! Mode (Low-Medium):** Merge Black/White exclusives, inject Gens 1-4 National Dex species into Unova routes/phenomena, and enable built-in Victini, Zorua, and Zoroark events.
5. **Full-Screen Foldable Mode + Floating PiP (Medium):** Full-screen 60 FPS widescreen Engine A + scalable, movable rounded-bezel PiP for Engine B with touch remapping and quick hide/show toggle.
6. **Portable PC Box Access / PC Anywhere (Medium):** Launch Unova `BoxApp` or Gear quick-swap anywhere with cutscene/Elite Four/bridge lock guardrails.

### Phase 2: Fast-Follow QoL, Touch Battle HUD & Gear Radar (Low -> Medium)
1. **QoL & Cheats (Low):** Universal TM/HM compatibility, Hard Level Cap, No Random Encounters, Always Bite When Fishing, Instant Text, Season & Clock switcher.
2. **Gen 5 Touch Battle HUD (Medium):** Port 2x2 Action/Move/Target cards (with Triple & Rotation Battle target support, Teravolt/Turboblaze effectiveness pills, YES/NO cards, and Poké Ball carousel).
3. **Gear Companion & Unova Route Radar (Medium):** Party move-pill cards, save slots, and live encounter radar supporting Seasons, Dark Grass, and Phenomena.

### Phase 3: 649-Species Sprites, Following Pokémon & Overworld Wilds (Medium-High -> High)
1. **649-Species Sprite Archive (Medium-High):** Python build pipeline packing normal + shiny + form `NSBTX` sprites into a built-in `eidos` mod pack.
2. **Native C Following Pokémon (High):** `FieldPlayer` C follower controller handling Unova curved cameras, 3D bridges, auto-hide during `ScriptContext` locks, and leak-free task cleanup.
3. **Overworld Wild Pokémon (High):** Visible roaming grass/cave/water spawns (`1-6` density, live shiny visuals, season-aware tables, touch encounters).
4. **64-Bit Campaign Audit & Link Play (High):** Full Unova campaign pointer audit and wireless battles/trades.
