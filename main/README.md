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

- ←/↑・→/↓: Encoder側の選択を移動
- W・S: Scroll側の選択を移動(それぞれ←/↑・→/↓と同じ向き)
- Enter: 決定
- Esc / Backspace: キャンセル/戻る

【Unit Scroll対応について】実機はUnit Encoder(Port.A)とUnit Scroll(Port.B)の
どちらからでも同じ操作ができ、選択移動の音(MoveCat/MovePrm)だけが入力ごとに
違う。simでは矢印キーがEncoder、W/SキーがScrollの回転に相当し、それぞれ
対応する音が鳴る(決定/戻るはEnter/Escで共通。実機のような入力ごとに独立した
押しボタンはキーボードには無いため)。

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
├── back.wav
├── deny.wav
├── ready.wav
├── done.wav
├── emovecat.wav     (Encoder用のカテゴリ/メニュー移動音)
├── emoveprm.wav     (Encoder用のパラメータ移動音)
├── smovecat.wav     (Scroll用のカテゴリ/メニュー移動音)
├── smoveprm.wav     (Scroll用のパラメータ移動音)
├── movecat.wav      (互換用フォールバック。emovecat/smovecatが無いときに使われる)
└── moveprm.wav      (互換用フォールバック。emoveprm/smoveprmが無いときに使われる)
```

hit/proceed/back/deny/ready/doneはEncoder(矢印キー)・Scroll(W/Sキー)の
どちらで操作しても共通。movecat/moveprmだけ置いた従来のフォルダでも動作し、
その場合は矢印キー・W/Sキーとも同じ音が鳴る(フォールバックの詳細は
`main/SD_CARD_SOUND_SETUP.md` 参照)。

WAVのフォーマット要件(非圧縮PCM/16bit等)はSD_CARD_SOUND_SETUP.mdと同じ。
未選択のままでもUIの動作確認はできる(音が鳴らないだけ)。

## Vercelでブラウザプレビューを公開する

GitHubリポジトリをVercelにGit連携すれば、push毎に `sim/` のWASMビルドを
自動デプロイできる(`vercel.json`が `sim/vercel-build.sh` を実行し、
`sim/build` を静的サイトとして公開する設定になっている)。

1. https://vercel.com で「Add New Project」→このGitHubリポジトリを選択
2. Framework Presetは「Other」のままでよい(vercel.jsonの設定が優先される)
3. デプロイを実行

デプロイの度にEmscripten SDKとLVGL本体をダウンロードしてからビルドするため
(ビルドキャッシュが効かない場合)数分かかる。`sim/vercel-build.sh`は
Vercelのビルドイメージに`cmake`/`ninja`が無い場合はpip経由のvenvで自動的に
用意する(2026/08確認: Vercelの標準イメージには入っていなかった)。
