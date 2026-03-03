# Playdate 专用：将 WASM 转为 WAMR AOT

本指南只针对 Playdate 设备性能优化，不讨论桌面平台。

## 先回答你的两个问题

1. AOT 可以跨 CPU 吗？
- 不可以。`.aot` 是目标架构相关产物，`x86_64` 机器上生成给 `x86_64` 的 AOT 不能在 Playdate（ARM Cortex-M7）运行。
- 你要的是“给 Playdate CPU 的 AOT”，不是“当前电脑 CPU 的 AOT”。

2. 需要在 Playdate 设备上做 AOT 编译吗？
- 不需要。AOT 编译在开发机上用 `wamrc` 做离线转换。
- Playdate 设备只负责加载并运行 `.aot`。

## 固定目标（仅 Playdate）

- 目标架构：`thumbv7em`
- 目标 CPU：`cortex-m7`

> 本仓库的 `scripts/build_aot.sh` 已默认使用这两个值。
> 如你本地 `wamrc` 对 target 命名不同，再覆盖环境变量即可。

## 1. 构建 wamrc（一次即可）

```bash
cd third_party/wasm-micro-runtime/wamr-compiler
./build_llvm.sh
mkdir -p build
cd build
cmake .. -DWAMR_BUILD_PLATFORM=darwin
cmake --build . -j
```

## 2. 单个 wasm 转 aot（Playdate 参数固定）

```bash
third_party/wasm-micro-runtime/wamr-compiler/build/wamrc \
  --target=thumbv7em \
  --cpu=cortex-m7 \
  -o /path/to/cart/tankle.aot \
  /path/to/cart/tankle.wasm
```

## 3. 批量转换（推荐）

```bash
./scripts/build_aot.sh /path/to/cart
```

脚本行为：
- 输入：`<cart_dir>/*.wasm`
- 输出：同名 `.aot`
- 默认参数：`--target=thumbv7em --cpu=cortex-m7`

如需手工覆盖：

```bash
WAMRC_BIN=/absolute/path/to/wamrc \
WAMRC_TARGET=thumbv7em \
WAMRC_CPU=cortex-m7 \
./scripts/build_aot.sh /path/to/cart
```

## 4. 在项目中启用并验证（设备端）

```bash
cmake -S . -B build-device -DTOOLCHAIN=armgcc -DWAMR_PD_ENABLE_AOT=ON
cmake --build build-device
```

运行规则（已在仓库实现）：
- 同名同时存在时，优先加载 `.aot`
- `.aot` 加载失败，自动回退 `.wasm`

## 5. 仅 Playdate 的建议

- `Data/<bundleID>/cart/` 中同时保留 `.wasm` + `.aot`
- 发布到设备时以 `.aot` 为主，`.wasm` 作为回退保险
- 桌面模拟器不作为性能优化目标，不需要为其单独做 AOT 策略
