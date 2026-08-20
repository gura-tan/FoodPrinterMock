#!/usr/bin/env bash
# Vercelのビルド環境向け。GitHubリポジトリをVercelにGit連携するだけで、
# push毎にこのスクリプトが実行されてsim/build配下が静的サイトとして
# デプロイされる(vercel.json参照)。
#
# Emscripten SDKをVercelのビルドコンテナに毎回インストールしてから
# cmakeでビルドする(ビルドキャッシュが効かない場合、この分だけ時間が
# かかる。git clone+SDKダウンロードで数分程度)。
set -euo pipefail

EMSDK_DIR="${EMSDK_DIR:-$HOME/emsdk}"

if [ ! -d "$EMSDK_DIR" ]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
fi

cd "$EMSDK_DIR"
./emsdk install latest
./emsdk activate latest
# shellcheck disable=SC1091
source ./emsdk_env.sh
cd - >/dev/null

emcmake cmake -S sim -B sim/build
cmake --build sim/build -j"$(nproc 2>/dev/null || echo 4)"
