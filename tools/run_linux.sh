#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja
cmake --build "${BUILD_DIR}" --target kairocore
"${BUILD_DIR}/platform/linux_sdl/kairocore" "$@"
