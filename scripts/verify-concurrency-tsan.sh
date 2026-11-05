#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${FEINSTATIC_TSAN_BUILD_DIR:-build/tsan}"
cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFEINSTATIC_BUILD_TESTS=ON \
  -DFEINSTATIC_ENABLE_THREAD_SANITIZER=ON
cmake --build "$BUILD_DIR" --parallel 2

# Keep the race-sensitive network tests focused and make the first report fatal.
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1:second_deadlock_stack=1}"
for filter in \
  network_server_uses_bounded_workers \
  network_server_rejects_connections_when_bounded_queue_is_full \
  network_server_handles_parallel_status_clients \
  network_authenticated_tcp_raft_cluster_commits_applies_and_recovers_ledger_blocks \
  raft_transaction_gossip
 do
  echo
  echo "========== ThreadSanitizer: $filter =========="
  "$BUILD_DIR/feinstatic_tests" "$filter"
done

echo "ThreadSanitizer concurrency checks passed."
