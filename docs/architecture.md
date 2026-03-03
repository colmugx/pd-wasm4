# Playdate WAMR Architecture

## Overview

The project is organized as a launcher app on Lua and a runtime backend on C.

- Lua (`Source/lua/*`) is the app layer:
  - `api/`: host bridge wrapper around `wamr_*`.
  - `model/`: launcher state and shared constants.
  - `service/`: runtime/cart domain logic.
  - `controller/`: input flow per scene.
  - `view/`: browser and running HUD rendering.
- C (`src/*`) is the runtime backend:
  - `src/main.c`: Playdate entrypoint.
  - `src/wamr_bridge.c`: compatibility bridge facade.
  - `src/app/app_lifecycle.*`: app lifecycle wiring.
  - `src/app/runtime_service.*`: application service/use-case layer for launcher/runtime flows.
  - `src/app/lua_bindings.*`: Lua marshalling only, delegates to app service.
  - `src/backend/game_backend.*`: backend orchestration facade and backend API.
  - `src/backend/cart_catalog.*`: cartridge scanning/sorting/catalog utilities.
  - `src/backend/host_runtime.*`: WAMR `env` host API handlers and `NativeSymbol` registry.
  - `src/backend/runtime_session.*`: WAMR runtime lifecycle, module session lifecycle, frame stepping, and runtime error/perf state.
  - `src/backend/persistence.*`: save path derivation and disk load/flush.
  - `src/backend/input_mapper.*`: Playdate button state to WASM-4 gamepad mapping.
  - `src/backend/menu_controller.*`: system menu creation/removal and callback dispatch.
  - `src/backend/render_composer.*`: framebuffer compositing and dither/scale path.
  - `src/deps/wasm4/*`: wasm4-facing layout definitions.

## Dependency Boundaries

- `third_party/wasm-micro-runtime` and `third_party/wasm4-core` are treated as dependencies.
- `game_backend` static library encapsulates runtime behavior and links dependencies.
- The final Playdate target only provides entrypoint wiring and links `game_backend`.
- Lua <-> C boundary is isolated in `src/app/lua_bindings.*`; launcher policy/coordination is isolated in `src/app/runtime_service.*`.
- `game_backend` does not directly own WAMR module/session internals; these are encapsulated in `runtime_session`.

## Compatibility

Lua native function names remain unchanged:

- `wamr_load`, `wamr_step`, `wamr_unload`
- `wamr_status_raw`, `wamr_perf_raw`, `wamr_runtime_config_raw`
- `wamr_set_dither_mode`, `wamr_get_dither_mode`
- `wamr_set_log_level`, `wamr_set_refresh_rate`, `wamr_get_fps_raw`
- `wamr_rescan_carts`, `wamr_list_carts`, `wamr_select_cart`
