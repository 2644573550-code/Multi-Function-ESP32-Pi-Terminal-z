#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="${1:?Usage: build_esp32.sh PROJECT [idf.py arguments ...]}"
shift
case "$PROJECT" in
  S3_Pi_Module|S3_PiBridge_LinkIt_Host) ;;
  *) echo "Unknown ESP32 project: $PROJECT" >&2; exit 2 ;;
esac
command -v idf.py >/dev/null || { echo "ESP-IDF is not exported; source export.sh first." >&2; exit 127; }
BUILD_DIR="${ESP_BUILD_DIR:-$HOME/esp32_workspace/builds/$PROJECT}"
if [[ -n "${ESP_SDKCONFIG:-}" ]]; then
  exec idf.py -C "$ROOT/esp/$PROJECT" -B "$BUILD_DIR" -DSDKCONFIG="$ESP_SDKCONFIG" build "$@"
fi
exec idf.py -C "$ROOT/esp/$PROJECT" -B "$BUILD_DIR" build "$@"
