#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="$(dirname "$(dirname "$(realpath "${BASH_SOURCE[0]}")")")"
source "$SOURCE_DIR/scripts/env.sh"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build}"
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" -DCMAKE_INSTALL_PREFIX=/usr "$@"
cmake --build "$BUILD_DIR" --parallel "${JOBS:-2}"
