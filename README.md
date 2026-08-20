# FoodPrinterMock

## Build

Enter docker env

```
cd ~/projects/FoodPrinterMock
DEV=/dev/ttyACM0;docker run --rm -it -v ${PWD}:/workspaces/FoodPrinterMock -w /workspaces/FoodPrinterMock --device=${DEV} --group-add $(stat -c '%g' ${DEV}) ghcr.io/wurly200a/builder-esp32/esp-idf-v5.5:5.5.5
```
In the docker env

```
idf.py build
idf.py flash
```

## UIシミュレータ(ブラウザでプレビュー)

main/以下の画面ロジック(menu_data/menu_nav/ui_screens)を実機なしでブラウザ上に
そのまま表示できる、Emscripten(WASM)ビルドが `sim/` にある。ロータリー
エンコーダー+ボタンの代わりにキーボードで操作する:

- ←/↑・→/↓: 選択を移動
- Enter: 決定
- Esc / Backspace: キャンセル/戻る

初回だけEmscripten SDKを導入する(どこか適当な場所に。リポジトリ外推奨):

```
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
```

ビルドして起動:

```
source ~/emsdk/emsdk_env.sh
emcmake cmake -S sim -B sim/build
cmake --build sim/build
python3 -m http.server -d sim/build 8000
```

ブラウザで `http://localhost:8000/index.html` を開く。main/側のUIコードを
編集したときは `cmake --build sim/build` を再実行すれば反映される。

### 音

ページ上部の「音フォルダを選択」ボタンから、PC上の任意のフォルダを選ぶと
鳴る(ブラウザはローカルファイルへ勝手にアクセスできないため、実機の
SDカード読み込みの代わりにこの方式にしている)。`main/SD_CARD_SOUND_SETUP.md`
と同じ命名のファイルをそのフォルダ直下に置くこと:

```
<選んだフォルダ>/
├── hit.wav
├── proceed.wav
├── move.wav
├── back.wav
└── done.wav
```

WAVのフォーマット要件(非圧縮PCM/16bit等)はSD_CARD_SOUND_SETUP.mdと同じ。
未選択のままでもUIの動作確認はできる(音が鳴らないだけ)。

## Vercelでブラウザプレビューを公開する

GitHubリポジトリをVercelにGit連携すれば、push毎に `sim/` のWASMビルドを
自動デプロイできる(`vercel.json`が `sim/vercel-build.sh` を実行し、
`sim/build` を静的サイトとして公開する設定になっている)。

1. https://vercel.com で「Add New Project」→このGitHubリポジトリを選択
2. Framework Presetは「Other」のままでよい(vercel.jsonの設定が優先される)
3. デプロイを実行

初回デプロイ時にVercelのビルドコンテナにEmscripten SDKを毎回インストール
してからビルドするため、数分かかる。もしビルドコンテナに`cmake`等が無くて
失敗した場合は、GitHub Actions側でビルドしてVercel CLIで静的デプロイする
方式に切り替えが必要(cmake不足で失敗したら教えてほしい)。
