#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v clang++ >/dev/null 2>&1; then
    echo "ERROR: clang++ is required for the complete verification matrix." >&2
    echo "Install it (Ubuntu: sudo apt-get install -y clang) and rerun." >&2
    exit 2
fi

# Build and test each preset. Fail fast on any failure.
for preset in debug release asan clang-release tsan; do
    echo
    echo "========== ${preset^^}: CONFIGURE =========="
    cmake --preset "$preset"
    echo "========== ${preset^^}: BUILD =========="
    cmake --build --preset "$preset" --parallel 2
    echo "========== ${preset^^}: TEST =========="
    ctest --preset "$preset" --output-on-failure --timeout 120
done

echo
echo "========== VERIFICATION MATRIX COMPLETE =========="
echo "PASS: GCC Debug, GCC Release, GCC ASan/UBSan, Clang Release + -Werror, GCC ThreadSanitizer"

echo
echo "========== PROJECT-RECORD VALIDATION =========="
bash scripts/validate-project-records.sh
