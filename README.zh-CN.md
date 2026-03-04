# PlayDate WASM-4 启动器

![Card](Source/launcher/launchImage.png)

[![Only on Playdate](assets/Playdate-badge-only-on.svg)](https://play.date/)

一个在 Playdate 上运行 **WASM-4** 卡带的启动器与运行时，底层使用 **WAMR**。

- 运行时后端：C11（`src/`）
- 启动器/UI：Lua（`Source/lua/`）
- 构建与打包：CMake + Playdate SDK

## 演示

![demo](assets/demo.gif)

## 特性

- 通过 WAMR 在 Playdate 上运行 WASM-4 卡带（`.wasm`）。
- 支持 AOT（`.aot`），加载失败时自动回退到 `.wasm`。
- 系统菜单支持：`Rescan`、`Reset`、`Dither`。
- 抖动模式：`None`、`Ordered`。
- Lua 应用层与 C 运行时/后端分层清晰。

## 下载

[![Download for Playdate](assets/Playdate-badge-download.svg)](https://github.com/colmugx/pd-wasm4/releases)

从 GitHub Releases 下载最新构建：

- https://github.com/colmugx/pd-wasm4/releases

## 侧载到 Playdate

本项目通过 **Sideload（侧载）** 安装，不通过 Playdate Catalog 发布。
官方参考：

- https://help.play.date/games/sideloading/
- https://help.play.date/developer/distributing-games/

### 方式 A：无线侧载（推荐）

1. 从 Releases 下载发布资产（`.pdx` 或压缩后的 `.pdx`）。
2. 登录 `play.date` 账号。
3. 打开侧载页面：`https://play.date/account/sideload/`。
4. 上传 `.pdx`（或压缩 `.pdx`）。
5. 在设备的 `Game Library` 中刷新，游戏会出现在侧载库里。

### 方式 B：USB Data Disk 侧载

1. 从 Releases 下载 `.pdx` 资产（该方式不使用 `.zip`）。
2. 用 USB 连接 Playdate。
3. 在 Playdate 上进入 `Settings -> System -> Reboot to Data Disk`。
4. 在电脑上打开挂载出来的 `PLAYDATE` 磁盘。
5. 将 `.pdx` 文件夹/包复制到 `Games/`。
6. 弹出磁盘（或在设备上按 `A/B`）退出 Data Disk 模式。
7. 在 Playdate 启动器中运行游戏。

## 快速开始

构建模拟器包：

```bash
make simulator
```

在模拟器中运行：

```bash
SIMULATOR_APP="$HOME/Playdate/bin/Playdate Simulator.app"
open -a "$SIMULATOR_APP" "$(pwd)/playdate-wasm-4.pdx"
```

构建设备包：

```bash
make device
```

同时构建：

```bash
make all
```

清理产物：

```bash
make clean
make distclean
```

产物：

- `./playdate-wasm-4.pdx`
- `./playdate-wasm-4-device.pdx`

## 卡带工作流

卡带只会从 **`Data/<bundleID>/cart`** 加载。

`bundleID` 定义在 [`Source/pdxinfo`](Source/pdxinfo)。

常见路径：

- 模拟器：`~/Playdate/Disk/Data/<bundleID>/cart`
- 真机（Data Disk 模式）：`Data/<bundleID>/cart`

流程：

1. 将 `.wasm` 和/或 `.aot` 复制到 `cart/`。
2. 在启动器菜单中执行 `Rescan`。
3. 选择卡带并运行。

加载优先级：

- 同名文件同时存在时优先 `.aot`。
- `.aot` 加载失败时回退到 `.wasm`。

## AOT 转换（可选）

批量转换某目录下所有 `.wasm`：

```bash
./scripts/build_aot.sh /path/to/cart
```

可选覆盖参数：

```bash
WAMRC_BIN=/absolute/path/to/wamrc \
WAMRC_TARGET=thumbv7em \
WAMRC_CPU=cortex-m7 \
./scripts/build_aot.sh /path/to/cart
```

## 许可证

[MIT](LICENSE)
