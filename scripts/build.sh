#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PRESET="${1:-debug}"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
ctest --preset "$PRESET"
