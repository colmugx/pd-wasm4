# PlayDate WASM-4 Launcher

English | [中文](README.zh-CN.md) | [日本語](README.ja.md)

![Card](Source/launcher/launchImage.png)

[![Only on Playdate](Playdate-badges/Playdate-badge-only-on.svg)](https://play.date/)

A Playdate launcher and runtime for running **WASM-4** cartridges with **WAMR**.

- Runtime backend: C11 (`src/`)
- Launcher app/UI: Lua (`Source/lua/`)
- Packaging/build: CMake + Playdate SDK

## Highlights

- Runs WASM-4 carts (`.wasm`) on Playdate through WAMR.
- Supports AOT (`.aot`) with automatic fallback to `.wasm`.
- System menu actions: `Rescan`, `Reset`, `Dither`.
- Dither modes: `None`, `Ordered`.
- Strict layering between Lua app layer and C runtime/backend.

## Download

[![Download for Playdate](Playdate-badges/Playdate-badge-download.svg)](https://github.com/colmugx/pd-wasm4/releases)

Get the latest build from GitHub Releases:

- https://github.com/colmugx/pd-wasm4/releases

## Sideload to Playdate

This project is installed via **Sideload**, not via Playdate Catalog.
Official references:

- https://help.play.date/games/sideloading/
- https://help.play.date/developer/distributing-games/

### Option A: Wireless sideload (recommended)

1. Download the release asset (`.pdx` or zipped `.pdx`) from Releases.
2. Sign in at `play.date`.
3. Open your account Sideload page: `https://play.date/account/sideload/`.
4. Upload the `.pdx` file (or zipped `.pdx`).
5. On device, open `Game Library` and refresh. The game will appear in your sideloaded library.

### Option B: USB Data Disk sideload

1. Download the `.pdx` asset from Releases (`.zip` is not used in this method).
2. Connect Playdate via USB.
3. On Playdate, enter `Settings -> System -> Reboot to Data Disk`.
4. Open the mounted `PLAYDATE` drive on your computer.
5. Copy the `.pdx` folder/package into `Games/`.
6. Eject the drive (or press `A/B` on device) to exit Data Disk mode.
7. Launch the game from the Playdate launcher.

## Quick Start

Build simulator package:

```bash
make simulator
```

Run in simulator:

```bash
SIMULATOR_APP="$HOME/Playdate/bin/Playdate Simulator.app"
open -a "$SIMULATOR_APP" "$(pwd)/playdate-wasm-4.pdx"
```

Build device package:

```bash
make device
```

Build both:

```bash
make all
```

Clean artifacts:

```bash
make clean
make distclean
```

Generated packages:

- `./playdate-wasm-4.pdx`
- `./playdate-wasm-4-device.pdx`

## Cartridge Workflow

Cartridges are loaded from **`Data/<bundleID>/cart` only**.

`bundleID` is defined in [`Source/pdxinfo`](Source/pdxinfo).

Common paths:

- Simulator: `~/Playdate/Disk/Data/<bundleID>/cart`
- Device (Data Disk mode): `Data/<bundleID>/cart`

Workflow:

1. Copy `.wasm` and/or `.aot` into `cart/`.
2. In launcher menu, run `Rescan`.
3. Select a cartridge and run.

Load priority:

- If both same-stem files exist, `.aot` is preferred.
- If AOT load fails, runtime falls back to `.wasm`.

## AOT Conversion (Optional)

Convert all `.wasm` files in a cart directory:

```bash
./scripts/build_aot.sh /path/to/cart
```

Optional overrides:

```bash
WAMRC_BIN=/absolute/path/to/wamrc \
WAMRC_TARGET=thumbv7em \
WAMRC_CPU=cortex-m7 \
./scripts/build_aot.sh /path/to/cart
```

## License

[MIT](LICENSE)
