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

# Vercelのビルドイメージにはcmake/ninjaが入っていないことがある(確認済み:
# 2026/08時点でcmakeが無くてビルド失敗した)。無ければpipで用意し、make
# 依存を避けるためジェネレータもNinjaに固定する。システムのpipがPEP668
# (externally-managed-environment)で--userすら拒否する環境もあるため、
# 素のpipではなく専用venvを作ってそこにインストールする(どの環境でも安全)。
if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    PY=python3
    command -v "$PY" >/dev/null 2>&1 || PY=python
    VENV_DIR="${CMAKE_VENV_DIR:-$HOME/.cache/sim-build-venv}"
    if [ ! -d "$VENV_DIR" ]; then
        "$PY" -m venv "$VENV_DIR"
    fi
    "$VENV_DIR/bin/pip" install --quiet cmake ninja
    export PATH="$VENV_DIR/bin:$PATH"
fi

emcmake cmake -S sim -B sim/build -G Ninja
cmake --build sim/build -j"$(nproc 2>/dev/null || echo 4)"
