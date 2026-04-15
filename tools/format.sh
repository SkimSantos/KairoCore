#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

find "${ROOT_DIR}/core" "${ROOT_DIR}/emu_backend" "${ROOT_DIR}/platform" \
    \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
    | xargs -0 clang-format -i
