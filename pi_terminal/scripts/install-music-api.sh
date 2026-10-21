#!/usr/bin/env bash
set -euo pipefail
ROOT="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
if ! command -v node >/dev/null || ! command -v npm >/dev/null; then
    echo '请先安装 Node.js 18+ 和 npm：sudo apt install nodejs npm' >&2
    exit 1
fi
cd "$ROOT/music_api"
npm ci --omit=dev --ignore-scripts --no-audit --no-fund
echo '音乐 API 库已准备好，终端会按需启动，无需手动配置端口。'
