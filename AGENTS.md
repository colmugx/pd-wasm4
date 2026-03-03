# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Native Playdate host code in C.
  - `src/app/`: app/use-case layer (`lua_bindings`, `runtime_service`, lifecycle wiring).
  - `src/backend/`: backend/runtime domain (`game_backend`, `runtime_session`, cart/input/render/persistence).
  - `src/deps/`: dependency-facing adapters/types (for example wasm4-facing structs).
  - `src/wamr_bridge.c/.h`: compatibility facade for existing `wamr_*` API names.
- `Source/`: Playdate runtime assets and Lua launcher app.
  - `Source/lua/api`: Lua-to-native bridge wrappers.
  - `Source/lua/model|service|controller|view`: launcher app architecture layers.
- `third_party/`: Embedded dependencies (`wasm-micro-runtime`, `wasm4-core`).
- `docs/`: Runtime and game development notes.

## Build, Test, and Development Commands
- `cmake -S . -B build`: Configure the project (requires Playdate SDK path via `PLAYDATE_SDK_PATH` or `~/.Playdate/config`).
- `cmake --build build`: Compile host code and regenerate the `.pdx` package.
- `open -a "~/Playdate/bin/Playdate Simulator.app" "/absolute/path/to/playdate-wasm-4.pdx"`: Launch in simulator.
- AOT conversion: `./scripts/build_aot.sh <cart_dir>` converts all `*.wasm` in the target directory to same-stem `*.aot`.
- Fast loop for cartridge changes: put cartridges in `Data/<bundleID>/cart/`, then trigger rescan in-app.

## Coding Style & Naming Conventions
- Language baseline: C11 (`CMakeLists.txt` sets `CMAKE_C_STANDARD 11`).
- Use 4-space indentation; no tabs.
- C style in this repo:
  - Return type on its own line before function name.
  - Braces on the next line for functions; control statements use `if (...) {`.
  - Macros/constants in upper snake case (for example `W4_FRAMEBUFFER_SIZE`).
- Naming:
  - C bridge/public functions use `wamr_*`.
  - Lua locals/functions use `camelCase`; state keys use snake_case where already established.

## Testing Guidelines
- No automated unit test suite is currently configured.
- Required validation is simulator-based smoke testing:
  1. Build successfully with CMake.
  2. Launch simulator and load at least one cartridge.
  3. Verify input handling, frame updates, and menu actions (`Rescan`, `Reset`, `Dither`).
- When changing runtime behavior, document manual test steps in the PR.

## Commit & Pull Request Guidelines
- Follow Conventional Commit prefixes seen in history (`feat: ...`), and use imperative summaries.
- Keep commits focused (host C changes, Lua UI changes, and cartridge/assets separated when practical).
- PRs should include:
  - Clear behavior summary and motivation.
  - Linked issue (if applicable).
  - Manual test evidence (steps + result); add simulator screenshots for UI/menu changes.

## Post-Refactor Summary (2026-03)
- Layering is strict: `lua_bindings` only marshals Lua/native values and delegates to `app/runtime_service`.
- `runtime_service` owns app policies/use-cases; it must not leak Lua marshalling or WAMR internals.
- `game_backend` is backend facade/orchestrator; WAMR module/runtime session internals are encapsulated in `backend/runtime_session`.
- Treat `wasm-micro-runtime` and `wasm4-core` as dependencies, not extension points for app logic.
- Cartridge source of truth is `Data/<bundleID>/cart` (`.wasm/.aot`), not `Source/cart`.
- Avoid "fake layering": no pass-through wrappers that bypass the app/service boundary directly into backend internals.
