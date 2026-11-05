# FeinStatic 1.9.0 release notes

## Summary

FeinStatic 1.9.0 adds explicit peer lifecycle supervision to the outbound transport. Raft RPCs and transaction gossip now share one circuit breaker per destination. A failing peer cannot be independently probed by the two traffic classes during the same backoff interval. The authenticated protocol remains v8, the endpoint registry format is unchanged, and no SQLite schema migration is introduced.

## Peer lifecycle state machine

Each configured outbound peer reports one of the following states:

- `UNKNOWN`: no successful operation has completed in the current enabled endpoint epoch.
- `HEALTHY`: an authenticated operation succeeded and there are no consecutive transport failures.
- `DEGRADED`: one to four consecutive transport failures.
- `OFFLINE`: five or more consecutive transport failures; the circuit is open until the retry deadline.
- `HALF_OPEN`: the cooldown elapsed and the one allowed in-flight operation is probing recovery.
- `DISABLED`: an operator disabled the endpoint. This is not the same as an unreachable peer.

## Retry and recovery

- Both Raft and transaction gossip consult the same peer-level retry gate.
- Backoff starts at 250 ms, doubles with consecutive transport failures, and caps at 30 seconds.
- Stable peer-ID/failure-generation jitter varies each peer's retry window by up to 20%, avoiding synchronized reconnect bursts while keeping tests deterministic.
- The existing one-in-flight-per-peer dispatcher rule makes half-open recovery a single probe. Success closes the circuit and clears the failure count; another transport failure reopens it.
- An authenticated request that receives a terminal application-level rejection is a completed transport exchange. It is counted as a non-delivery/drop but does not mark the endpoint offline.
- Disabling/replacing an endpoint resets the current lifecycle epoch. Health state is process-local and starts as `UNKNOWN` after restart; durable Raft state and mempool contents remain the recovery sources.

## Operator visibility

`raft-peer-health-auth` now displays `Lifecycle` beside the existing endpoint, queue, attempt, success, failure, drop and retry-delay counters. The lifecycle state is derived client-side from existing health fields and the fixed five-failure threshold; the response encoding is unchanged, so protocol v8 peers remain compatible.

## Compatibility and limitations

- Authenticated peer wire protocol: v8, unchanged.
- SQLite schema and Raft persistent-state format: unchanged.
- Endpoint-registry and trust-store formats: unchanged.
- Membership remains pre-provisioned and is changed only through the existing joint-consensus procedure.
- No automatic peer discovery, multi-address endpoint failover, TLS, Byzantine fault tolerance, or independent cryptographic audit is added.
- Lifecycle state is not persisted. A restarted node does not inherit stale offline state from a prior process.

## Validation

Validated against the v1.9.0 source tree:

| Configuration | Result |
|---|---:|
| GCC Debug | 161/161 passed |
| GCC Release | 161/161 passed |
| GCC ASan/UBSan | 161/161 passed |
| Clang Release with `-Werror` | 161/161 passed |
| Three-process Raft chaos laboratory | 9/9 stages passed |
| Four-process joint-consensus membership laboratory | 8/8 stages passed |

The synthetic one-million-order benchmark and release demonstration are also executed by `scripts/verify-all.sh`. Benchmark figures must be interpreted as in-memory single-process measurements, not distributed exchange throughput.

### Focused concurrency instrumentation

The optional `scripts/verify-concurrency-tsan.sh` workflow completed six focused tests covering concurrent inbound handshakes, bounded-queue saturation/recovery, parallel status clients, authenticated Raft state access, and transaction-gossip retry/recovery. GCC 14 emitted Boost.Asio's known `atomic_thread_fence` ThreadSanitizer instrumentation warning during compilation; no TSan race report occurred in the selected tests. This focused run is supplemental evidence, not a formal proof that all schedules are race-free.
