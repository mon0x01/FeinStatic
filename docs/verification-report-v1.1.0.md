# FeinStatic v1.1.0 verification report

## Release scope

v1.1.0 adds a repeatable process-level Raft failure lab around the existing v1.0.0 authenticated TCP runtime and `RaftLedgerAdapter`. The lab launches three independent `feinstatic raft-node` OS processes. Each process owns a distinct SQLite database, Ed25519 node identity, trust store, TCP listener, and diagnostic log. A helper creates an isolated synthetic common Ledger checkpoint and ephemeral identities.

A Python standard-library TCP proxy is placed on each directed inter-node path. One path receives a deterministic 10 ms delay on the first payload of each TCP connection. The test then:

1. Elects a leader and commits a Ledger block across three processes.
2. Terminates the leader with `SIGKILL` and waits for a replacement elected by the remaining majority.
3. Commits another block while the former leader is down.
4. Restarts the former leader from its own durable SQLite database and verifies catch-up.
5. Isolates the current leader bidirectionally from both peers by stopping four directed proxies.
6. Verifies that the isolated node cannot advance its commit index, while the connected majority elects a leader and commits.
7. Restores all four links and verifies that all nodes converge on one term, commit index, applied index, log index, and Ledger tip, with no uncommitted suffix remaining.

## Verification matrix

Environment: Linux development container; GCC 14.2.0; Clang 17.0.0; OpenSSL 3.5.7; SQLite 3.46.1; Boost headers from the environment.

| Configuration | Result |
|---|---|
| GCC Debug | 126/126 tests passed |
| GCC Release | 126/126 tests passed |
| GCC ASan/UBSan | 126/126 tests passed |
| Clang Release + `-Werror` | 126/126 tests passed |
| Release demo | Passed |
| Match benchmark | Completed, 1,000,000 synthetic in-memory order operations |
| Multi-process chaos lab | Passed in repeated runs, including delayed-link and Release-binary runs |

The process-level lab covered leader election, commit, process kill, majority failover, offline-member restart, catch-up, a minority partition, no-quorum commit prevention, majority progress, partition healing, conflict repair, unique-leader restoration, and exact index/tip convergence. Multiple complete repeated runs passed. During stress testing, one externally time-limited batch was interrupted during final convergence; the harness signal handler then shut down its child process groups and retained the diagnostic workspace. The delay was reduced from a per-read delay to a one-time per-connection delay to avoid artificial timeouts caused by TCP fragmentation.

The four compiler/sanitizer configurations were each built and tested independently, and a clean extraction of the final source archive passed a fresh GCC Release build, CTest, and the process-level lab. The aggregate `scripts/verify-all.sh` wrapper was not observed to finish in one development-container tool invocation because the external execution window expired during the run; this is not recorded as a project test failure, and each of its verification stages was completed separately.

## Benchmark

The v1.1.0 development-container Release benchmark reported:

- Compiler: GCC 14.2.0.
- Hardware threads reported: 5.
- Workload: 1,000,000 synthetic same-price in-memory Match order operations.
- Completed trades: 500,000.
- Elapsed time: 7.365818 seconds.
- Throughput: 135,762.24 order submissions/second in the final observed run.
- Average latency: 7.31 microseconds; p50 2.48 microseconds; p95 15.56 microseconds; p99 32.89 microseconds.

This is a single-process in-memory Match microbenchmark. It excludes TCP, cryptographic peer framing, SQLite persistence, Raft replication, and Ledger application. It is not distributed throughput. Benchmark results vary with shared-container load and must not be compared directly across machines.

## Persistence and compatibility

v1.1.0 changes no Ledger, FinancialPlatform, or Raft persistence schema and no network wire version. Existing schema v4 databases require no migration. The update procedure creates a full pre-update backup, preserves `.git/` and `runtime/`, removes only generated `build/`, runs all four build/test configurations and the process-level failure lab, then verifies the existing runtime database.

## Known limitations

- The lab uses a deterministic, fixed 10 ms one-time connection delay on one directed path and abrupt connection-level partitions. It is not exhaustive randomized packet loss, latency distribution, bandwidth saturation, or arbitrary network scheduling.
- The lab does not inject disk-full, SQLite I/O errors during a live process crash, filesystem power loss, memory corruption, or kernel failure.
- Raft remains static-membership and crash-fault tolerant only; no Byzantine fault tolerance or dynamic membership.
- Peer frames remain authenticated and replay-protected but not encrypted; TLS/confidentiality is not implemented.
- No log compaction or snapshot installation is implemented.
- This release is a private engineering prototype, not production custody, an exchange, or public-chain infrastructure.

## User-machine verification still required

The full v1.1.0 verification matrix was run in the development container, not on the user's Arch Linux machine. The supplied Arch updater must complete successfully on the user's machine before v1.1.0 is considered installed there. Its terminal output is required to verify local toolchain behavior, the existing database hash, backup path, and completion message.
