# WASM-4 Runtime Notes

## Host Design

- Runtime engine: WAMR interpreter
- Graphics: Playdate framebuffer (`pd->graphics->getFrame()`)
- Audio: Playdate callback source (`pd->sound->addSource`)
- Storage: Playdate file API (`save/<cart>.disk`)
- Dither: `None`, `Ordered` (menu/Lua configurable)
  - `None`: 4 色二值化（两浅色=白、两深色=黑）
  - `Ordered`: Bayer2 有序抖动

## Frame Loop

Each `wamr_step()` does:

1. Read buttons and write `GAMEPAD1`
2. Call `start()` on first frame (if present)
3. Clear framebuffer unless `SYSTEM_PRESERVE_FRAMEBUFFER`
4. Call `update()`
5. `w4_apuTick()`
6. Composite 2bpp framebuffer to Playdate 1bpp with dithering
7. Update `last_step_ms`

## Save Data

- Capacity: 1024 bytes
- Backed by disk file per cartridge
- Writes are throttled and flushed on unload/shutdown

## Cartridge Source

- Cartridge modules are loaded from `Data/<bundleID>/cart` only.
- Runtime uses `kFileReadData` for module reads (no `.pdx` fallback).
- If no cartridges exist, the browser shows an empty-state prompt and waits for `Rescan`.
- `.aot` still falls back to same-stem `.wasm` when available.

## Lua Bridge

- `wamr_load(path?) -> ok, load_ms, err`
- `wamr_step() -> ok, step_ms, err`
- `wamr_unload()`
- `wamr_status_raw() -> loaded, load_ms, step_ms, err, current_path`
- `wamr_set_dither_mode(mode)` where mode is `0/1`
- `wamr_get_dither_mode() -> mode`
- `wamr_rescan_carts() -> count, current_path`
- `wamr_list_carts() -> count, selected_index, joined_paths`
- `wamr_select_cart(index) -> ok, path_or_err`

## Known Limits

- Mouse: not implemented
- Netplay: not implemented
- 4-color exact fidelity: intentionally not targeted
