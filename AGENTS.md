# Repository Workflow Rules (`pokebw-galaxyfold8-ls`)

- **Base Engine & Decomp Pairing:** Native Android port of *Pokémon Black* & *Pokémon White* built on `eidos` (`~/pokebw-galaxyfold8-ls`, branch `bw-fold`) paired with sibling decompilation repo `~/aletheia` (`-DALETHEIA_ROOT=../aletheia`).
- **Target Platform:** Android `arm64-v8a` only (optimized for Samsung Galaxy Z Fold & phones). Do not run or suggest desktop builds unless explicitly requested.
- **Terminal Safety:** Always wrap multi-step shell scripts in a subshell `( ... )` and avoid top-level `set -e`.
- **Git & CI Workflow:** Every completed change should update `AGENTS.md`, commit cleanly, and push to `origin bw-fold`.
- **Android CI Pipeline (Phase 0.2):** `.github/workflows/build_android.yml` builds the two-stage `arm64-v8a` APK (`eidos_boot.wasm` via `wasi-sdk`, then `android/build.py` via NDK + Gradle) against `SteveMcK01/aletheia` (`main`), sets `VERSION_CODE=$((9200000 + RUN_NUMBER))` and `VERSION_NAME=<short-sha>` via `android/app/build.gradle` env overrides, supports optional `ANDROID_KEYSTORE` release signing (falling back to debug), uploads `pokebw-android` + `libmain.so` symbols, and publishes the rolling `bw-fold-android-latest` prerelease on pushes to `bw-fold`.
- **Roadmap Reference:** Read `BW_PORT_PLAN.md` for feature priority order and architecture strategy.
