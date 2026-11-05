#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if ! command -v clang++ >/dev/null 2>&1; then
    echo "ERROR: clang++ is required for the complete verification matrix." >&2
    echo "Install it (Arch: sudo pacman -S --needed clang) and rerun." >&2
    exit 2
fi

for preset in debug release asan clang-release; do
    echo
    echo "========== ${preset^^}: CONFIGURE =========="
    cmake --preset "$preset"
    echo "========== ${preset^^}: BUILD =========="
    cmake --build --preset "$preset" --parallel 2
    echo "========== ${preset^^}: TEST =========="
    ctest --preset "$preset" --output-on-failure --timeout 120
done

echo
echo "========== RELEASE DEMONSTRATION =========="
./build/release/feinstatic demo

echo
echo "========== RELEASE BENCHMARK (1,000,000 ORDERS) =========="
./build/release/feinstatic_bench 1000000

echo
echo "========== VERIFICATION MATRIX =========="
echo "PASS: GCC Debug, GCC Release, GCC ASan/UBSan, Clang Release + -Werror"

echo
echo "========== MULTI-PROCESS RAFT FAILURE LAB =========="
python3 ./scripts/raft-process-chaos.py \
    --binary ./build/release/feinstatic \
    --prepare-helper ./build/release/feinstatic_raft_lab_prepare

echo
echo "========== JOINT-CONSENSUS MEMBERSHIP PROCESS LAB =========="
python3 ./scripts/raft-membership-process.py \
    --binary ./build/release/feinstatic \
    --prepare-helper ./build/release/feinstatic_raft_lab_prepare
