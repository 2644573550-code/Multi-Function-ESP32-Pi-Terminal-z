#!/usr/bin/env bash
set -euo pipefail
ROOT="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
source "$ROOT/scripts/env.sh"
BUILD="$ROOT/../build-host"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTERMINAL_REPAIR_AUDIT=OFF
cmake --build "$BUILD" --target arkpets_preview --parallel 4
# Local preview only: no product config, serial/BLE, SSH, service or deployment.
exec "$BUILD/arkpets_preview"
