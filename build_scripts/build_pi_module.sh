#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/esp/S3_Pi_Module"
BUILD_DIR="${1:-$HOME/esp32_workspace/builds/S3_Pi_Module}"

if ! command -v idf.py >/dev/null 2>&1; then
    printf 'ESP-IDF is not exported; source export.sh first.\n' >&2
    exit 127
fi
if [[ -z "${PI_MODULE_SDKCONFIG:-}" || ! -f "$PI_MODULE_SDKCONFIG" ]]; then
    printf 'Set PI_MODULE_SDKCONFIG to the private provisioned sdkconfig.\n' >&2
    exit 2
fi
if [[ -z "${PI_MODULE_PACKAGE_DIR:-}" || ! -f "$PI_MODULE_PACKAGE_DIR/embedded_package.json" || ! -f "$PI_MODULE_PACKAGE_DIR/embedded_package.tar.zst" ]]; then
    printf 'Set PI_MODULE_PACKAGE_DIR to a directory containing the signed embedded package files.\n' >&2
    exit 2
fi

exec idf.py -C "$PROJECT" -B "$BUILD_DIR" \
    -DSDKCONFIG="$PI_MODULE_SDKCONFIG" \
    -DPI_MODULE_PACKAGE_DIR="$PI_MODULE_PACKAGE_DIR" build
