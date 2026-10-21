#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-}"
BUILD_DIR="${2:-}"
if [[ "$MODE" != "BLE" && "$MODE" != "LAN" ]]; then
    printf 'Usage: build_pi_module_variant.sh BLE|LAN BUILD_DIR\n' >&2
    exit 2
fi
if [[ -z "$BUILD_DIR" ]]; then
    printf 'BUILD_DIR is required so BLE and LAN outputs stay separate.\n' >&2
    exit 2
fi
if ! command -v idf.py >/dev/null 2>&1; then
    printf 'ESP-IDF is not exported; source export.sh first.\n' >&2
    exit 127
fi
if [[ -z "${PI_MODULE_SDKCONFIG:-}" || ! -f "$PI_MODULE_SDKCONFIG" ]]; then
    printf 'Set PI_MODULE_SDKCONFIG to the private provisioned sdkconfig.\n' >&2
    exit 2
fi
if [[ -z "${PI_MODULE_PACKAGE_DIR:-}" || ! -f "$PI_MODULE_PACKAGE_DIR/embedded_package.json" || ! -f "$PI_MODULE_PACKAGE_DIR/embedded_package.tar.zst" ]]; then
    printf 'Set PI_MODULE_PACKAGE_DIR to the signed embedded package directory.\n' >&2
    exit 2
fi

mkdir -p "$BUILD_DIR"
MODE_SDKCONFIG="$BUILD_DIR/sdkconfig.private"
python3 - "$PI_MODULE_SDKCONFIG" "$MODE_SDKCONFIG" "$MODE" <<'PY'
from pathlib import Path
import sys
source, destination, mode = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
values = {
    "CONFIG_APP_PI_TRANSPORT_BLE": "y" if mode == "BLE" else "n",
    "CONFIG_APP_PI_TRANSPORT_LAN": "n" if mode == "BLE" else "y",
    "CONFIG_APP_PI_PACKAGE_DATA": "n" if mode == "BLE" else "y",
    "CONFIG_APP_PI_PACKAGE_HOTSPOT_HTTP": "y" if mode == "BLE" else "n",
}
lines = [line for line in source.read_text(errors="strict").splitlines()
         if not any(line.startswith(key + "=") or line == "# " + key + " is not set" for key in values)]
for key, value in values.items():
    lines.append(f"{key}={value}" if value == "y" else f"# {key} is not set")
destination.write_text("\n".join(lines) + "\n")
destination.chmod(0o600)
PY

exec idf.py -C "$ROOT/esp/S3_Pi_Module" -B "$BUILD_DIR" \
    -DSDKCONFIG="$MODE_SDKCONFIG" \
    -DPI_MODULE_PACKAGE_DIR="$PI_MODULE_PACKAGE_DIR" build
