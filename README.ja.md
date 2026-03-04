# PlayDate WASM-4 Launcher

![Card](Source/launcher/launchImage.png)

[![Only on Playdate](assets/Playdate-badge-only-on.svg)](https://play.date/)

**WAMR** を使って Playdate 上で **WASM-4** カートリッジを実行するランチャー/ランタイムです。

- ランタイムバックエンド: C11（`src/`）
- ランチャー/UI: Lua（`Source/lua/`）
- ビルド/パッケージ: CMake + Playdate SDK

## DEMO

<video src="assets/demo.mp4" controls muted playsinline></video>

## 主な機能

- WAMR 経由で WASM-4 カート（`.wasm`）を Playdate で実行。
- AOT（`.aot`）対応。読み込み失敗時は `.wasm` へ自動フォールバック。
- システムメニュー: `Rescan`、`Reset`、`Dither`。
- ディザモード: `None`、`Ordered`。
- Lua アプリ層と C ランタイム/バックエンドを明確に分離。

## ダウンロード

[![Download for Playdate](assets/Playdate-badge-download.svg)](https://github.com/colmugx/pd-wasm4/releases)

最新ビルドは GitHub Releases から取得できます。

- https://github.com/colmugx/pd-wasm4/releases

## Playdate へのサイドロード

本プロジェクトは Playdate Catalog ではなく、**Sideload** でインストールします。
公式ドキュメント:

- https://help.play.date/games/sideloading/
- https://help.play.date/developer/distributing-games/

### 方法 A: ワイヤレスサイドロード（推奨）

1. Releases から配布ファイル（`.pdx` または zip 化された `.pdx`）をダウンロード。
2. `play.date` にサインイン。
3. サイドロードページ `https://play.date/account/sideload/` を開く。
4. `.pdx`（または zip 化 `.pdx`）をアップロード。
5. デバイス側で `Game Library` を更新すると、サイドロード一覧に表示されます。

### 方法 B: USB Data Disk サイドロード

1. Releases から `.pdx` をダウンロード（この方法では `.zip` は使いません）。
2. USB で Playdate を接続。
3. Playdate で `Settings -> System -> Reboot to Data Disk` を実行。
4. PC にマウントされた `PLAYDATE` ドライブを開く。
5. `.pdx` フォルダ/パッケージを `Games/` にコピー。
6. ドライブを取り外す（または本体で `A/B`）と Data Disk モードを終了。
7. Playdate ランチャーから起動。

## クイックスタート

シミュレータ向けパッケージをビルド:

```bash
make simulator
```

シミュレータで起動:

```bash
SIMULATOR_APP="$HOME/Playdate/bin/Playdate Simulator.app"
open -a "$SIMULATOR_APP" "$(pwd)/playdate-wasm-4.pdx"
```

実機向けパッケージをビルド:

```bash
make device
```

両方ビルド:

```bash
make all
```

生成物をクリーン:

```bash
make clean
make distclean
```

生成されるパッケージ:

- `./playdate-wasm-4.pdx`
- `./playdate-wasm-4-device.pdx`

## カート運用フロー

カートリッジは **`Data/<bundleID>/cart`** からのみ読み込みます。

`bundleID` は [`Source/pdxinfo`](Source/pdxinfo) に定義されています。

代表的なパス:

- シミュレータ: `~/Playdate/Disk/Data/<bundleID>/cart`
- 実機（Data Disk モード）: `Data/<bundleID>/cart`

手順:

1. `.wasm` と/または `.aot` を `cart/` にコピー。
2. ランチャーメニューで `Rescan` を実行。
3. カートを選んで実行。

読み込み優先順位:

- 同名が両方ある場合は `.aot` を優先。
- `.aot` が失敗した場合は `.wasm` にフォールバック。

## AOT 変換（任意）

指定ディレクトリ内の `.wasm` を一括変換:

```bash
./scripts/build_aot.sh /path/to/cart
```

上書きオプション:

```bash
WAMRC_BIN=/absolute/path/to/wamrc \
WAMRC_TARGET=thumbv7em \
WAMRC_CPU=cortex-m7 \
./scripts/build_aot.sh /path/to/cart
```

## ライセンス

[MIT](LICENSE)
