#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for project in S3_PiBridge_LinkIt_Host; do
  bash "$ROOT/build_scripts/build_esp32.sh" "$project"
done
if [[ -n "${PI_MODULE_SDKCONFIG:-}" && -n "${PI_MODULE_PACKAGE_DIR:-}" ]]; then
  for mode in BLE LAN; do
    bash "$ROOT/build_scripts/build_pi_module_variant.sh" "$mode" \
      "$HOME/esp32_workspace/builds/S3_Pi_Module-$mode"
  done
else
  echo "Skipping S3_Pi_Module variants: set PI_MODULE_SDKCONFIG and PI_MODULE_PACKAGE_DIR for private provisioning."
fi
