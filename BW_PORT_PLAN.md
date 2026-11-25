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
3. **Gear Companion & Unova Route Radar (Done):** Added the **Gear** tab in `host/imgui_overlay.cpp` backed by live `gear_push` telemetry (`sdk/pokebw_gear.c`, `sdk/pokebw_gear_state.h`, `host/main.c`, `host/pokebw_settings.c`): displays party move-pill cards, save backup slots (`1..3` alongside `eidos` slots `1..8`), and the live Unova Route Radar (Seasons, Dark Grass, Phenomena, Surfing/Fishing, Pokédex Caught/Seen/New status).

### Phase 3: 649-Species Sprites, Following Pokémon & Overworld Wilds (Done)
1. **649-Species Sprite Archive (Done):** Added `tools/pack_ow_sprites.py` (encodes 4×4/4×2/2×4/1×8 PNG sprite sheets and `.nsbtx`/`.btx0` files into compact `.bwsp` packs) paired with `sdk/pokebw_ow_sprites.c` (injects custom `.bwsp` sprites + automatic cartridge `a/0/0/7` 8-frame `BTX0` synthesis for all 649 species, shiny variants, and alternate forms into `a/0/4/9` via `bw_vfs_mem_set`, wrapping `ov010_FLDMMDL_LoadObjCodeParam` for `0x4000..0x5FFF`).
2. ~~**Native C Following Pokémon (High):**~~ **Done (`bw-fold`)**: Added `sdk/pokebw_follower.{h,c}` (`ov010_FLDMMDL_UpdateSysProc` & `ov021_FIELDMAP_DeleteMmdlSys` wraps + A-button cry/hop in `sdk/pokebw_portable_pc.c`) driving the lead conscious party Pokémon behind the player across grid/rail/curved-camera maps with auto-hide during cutscenes/Surf/Dive/wipes and `POKEBW_SET_FOLLOWING_POKEMON` in the ImGui `Gameplay` tab.
3. ~~**Overworld Wild Pokémon (High):**~~ **Done (`bw-fold`)**: Added `sdk/pokebw_wild.{h,c}` (visible roaming grass/dark-grass/cave/sand/water spawns pulled from live season/Gotta Catch 'Em All! `EncountData` tables, configurable density `1–6` via `POKEBW_SET_OVERWORLD_WILDS` / `POKEBW_SET_OVERWORLD_WILD_DENSITY`, 30%-clipped water swimming sprites with Flying-type exemption, 7-second off-screen despawn grace timer, same-zone persistence across battles/menus, live shiny sprites + `ov021_FIELD_ENCOUNT_CreatePersonality` shiny battle sync, and contact / `A`-button wild encounters).

### Phase 4: Extended QoL, Battle Convenience & Gear Suite (Low -> High)
1. ~~**Fast-Forward / Speed-Up Toggle (Low):**~~ **Done (`bw-fold`)**: Added live Fast-Forward speed multiplier (`2x`/`3x`/`4x`/`Max`) in `engine/platform/platform.c` (`eidos_platform_fast_forward`), top-center Android touch `FF` pill in `host/touch_controls.c`, and toggle/selector in `host/imgui_overlay.cpp` (`POKEBW_SET_FAST_FORWARD`, `POKEBW_SET_FAST_FORWARD_SPEED`).
2. ~~**Shiny Odds Multiplier (Low):**~~ **Done (`bw-fold`)**: Added `sdk/pokebw_shiny.{h,c}` (wrapping `POKE_TOOL_CreatePersonalRnd` and `ov021_SODATEYA_ApplyMasudaMethod` + helpers wired into `sdk/pokebw_wild.c`'s overworld spawns and `ov021_FIELD_ENCOUNT_CreatePersonality`), configurable `1/8192` (vanilla), `1/1024`, `1/256`, `1/64`, or `100%` via `POKEBW_SET_SHINY_ODDS` across wild Pokémon, overworld wilds, static legendaries, starters/gifts, and hatched eggs while preserving nature, gender, and ability.
3. ~~**"Use Another Repel?" Prompt (Low-Medium):**~~ **Done (`bw-fold`)**: Backported the *Black 2 / White 2* Repel-expiry reuse prompt via `sdk/pokebw_repel.c` (wrapping `MOVE_POKEMON_SAVE_SetCooldown`, `MOVE_POKEMON_SAVE_TickCooldown`, and `ov010_SCRIPT_SYS_CreateScriptEventSimple`), prompting with remaining count for Max/Super/regular Repel from the Bag (`POKEBW_SET_REPEL_PROMPT`).
4. ~~**In-Game HM/TM Quick-Use Shortcut (Medium):**~~ **Done (`bw-fold`)**: Added one-tap sorted `Quick Field Moves (HM/TM)` panel in the **Gear** tab (`host/imgui_overlay.cpp`) backed by live `PokebwGearFieldMove` telemetry (`sdk/pokebw_gear.c`, `sdk/pokebw_hm_shortcut.{h,c}`), sorting usable field moves (Cut, Fly, Surf, Strength, Waterfall, Dive, Flash, Dig) first and dispatching directly (including ov072 Town Map Fly destination selection).
5. ~~**Poké Ball Quick-Throw Carousel (Medium):**~~ **Done (`bw-fold`)**: Added `< [Poké Ball + Qty] >` quick-throw carousel directly on the One-Screen wild battle command menu (`engine/ui/recipe_battle.c`) backed by `sdk/pokebw_ball_carousel.{h,c}` (wrapping `ov093_BTLV_CORE_PollActionMenu`, enforcing Gen 5 capture preconditions, remembering last-selected ball, and toggling via `POKEBW_SET_BALL_CAROUSEL`).
6. **Gear — PC Box & Bag Auto-Sorter (Medium):** Sort PC boxes (by Dex#, name, level, or type) and Bag pockets directly from the Gear tab.
7. **Gear — Egg Step Counter & Day Care Preview (Medium):** Live steps remaining to hatch party eggs plus Route 3 Day Care compatibility, egg species/moves, Everstone nature, Masuda odds, and IV inheritance preview.
8. **Gear — Built-in Pokédex Lookup (Medium-High):** A–Z ROM-driven Pokédex lookup showing base stats, abilities, evolutions, full learnsets (level/TM/egg/tutor), and encounter locations.
9. **Gear — Overhead Area Item Map (High):** Top-down live map of the current route highlighting uncollected item balls and hidden Dowsing Machine items.
