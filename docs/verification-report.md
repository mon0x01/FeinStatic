# FeinStatic v1.0.0 verification report

## Release scope

v1.0.0 completes the first live integration of the v0.9 durable Raft kernel with the authenticated TCP peer runtime and native Ledger state machine. Raft RequestVote, VoteResponse, AppendEntries, and AppendResponse messages are carried over the existing session-authenticated, Ed25519-signed TCP protocol (wire version 4). Incoming sender and recipient IDs are checked against the authenticated session and configured static cluster membership.

`RaftLedgerAdapter` encodes canonical bounded Ledger-block commands, permits proposals only from a ready leader, validates replicated blocks on followers, applies committed commands in index order, and validates recovered Ledger/Raft consistency on restart. Applying a committed block persists the Ledger append, durable mempool reconciliation, and Raft applied-index snapshot in one SQLite transaction. Raft-enabled members reject legacy direct block import and pull-based chain synchronization so those paths cannot bypass quorum commitment.

The runtime supports static permissioned odd-sized crash-fault clusters (3, 5, 7, or 9 members). This is not Byzantine fault tolerance. Membership changes, joint consensus, log compaction/snapshot installation, TLS/confidentiality, automatic transaction gossip, and production finality are not implemented.

## Verification matrix

The v1.0.0 test executable contains 126 named test cases. The release workspace was built with the following configurations:

| Configuration | Result | Notes |
|---|---:|---|
| GCC Debug | 126/126 | CTest passed |
| GCC Release | 126/126 | CTest passed |
| GCC ASan/UBSan | 126/126 | CTest passed; executable also passed two direct runs with no sanitizer report |
| Clang Release + `-Werror` | 126/126 | CTest passed |
| Release demonstration | Passed | In-memory synthetic wallets/assets |
| Match benchmark | Completed | 1,000,000 synthetic order operations |
| Clean extracted source archive | 126/126 | Fresh Release configure/build and CTest passed |

The sanitizer executable was also run twice directly because one combined verification-script invocation intermittently stalled at the CTest launch in this development container. A subsequent standalone CTest invocation passed in approximately 15 seconds, and both direct executions completed in approximately 14–15 seconds with 126/126 tests and no ASan/UBSan diagnostic. A 120-second CTest timeout is now configured to turn any future hang into a bounded failure.

## Live Raft-over-TCP integration coverage

The two live cluster tests instantiate three independently configured `NodeServer` instances, each with its own SQLite database, Ed25519 node identity, trust configuration, and loopback TCP endpoint. No shared writable database is used.

Coverage includes:

- Authenticated leader election and current-term no-op commitment.
- Leader proposal of a canonical Ledger block command.
- RequestVote and AppendEntries RPC exchange over actual authenticated TCP sessions.
- Majority replication and identical committed Ledger state on all members.
- Rejection of legacy direct block import in Raft mode.
- Restart of a member and recovery from its durable Raft log and Ledger state.
- Termination of the leader, replacement election by the remaining majority, and commitment of a subsequent block.
- Restart and catch-up of the former leader from the new leader's log.
- Independent SQLite databases and distinct node identity/trust relationships.

The adapter's unit tests additionally cover canonical block command encoding, invalid-block rejection, strict applied-index progression, stale revision rejection, and fail-closed behavior when the atomic SQLite application transaction cannot commit.

## Previous Raft kernel and storage coverage

The v0.9 kernel's simulator and persistence tests remain in the suite: durable per-term votes, candidate log freshness, current-term commit rules, majority/minority behavior, partition recovery, same-term leader uniqueness, conflict repair, committed-entry protection, bounded message sizes, overflow-safe timers, static membership/checkpoint binding, checksummed revisioned snapshots, schema migration v3-to-v4, stale-writer conflict, injected SQLite failure, and all-or-nothing AppendEntries validation.

## Benchmark

- Compiler: GCC 14.2.0.
- Hardware threads reported by the development container: 5.
- Workload: 1,000,000 synthetic same-price in-memory Match order operations.
- Completed trades: 500,000.
- Elapsed time: 4.278749 seconds.
- Throughput: 233,713.15 order submissions/second.
- Average latency: 4.24 microseconds.
- p50: 1.84 microseconds; p95: 5.61 microseconds; p99: 15.93 microseconds.

This is a single-process in-memory Match microbenchmark. It excludes TCP, cryptographic peer framing, SQLite persistence, Raft replication, and Ledger application. It is not a distributed throughput or end-to-end transaction-latency result and must not be compared across machines without controlling hardware, compiler, and workload.

## Persistence and migration

v1.0.0 retains SQLite schema v4, introduced in v0.9.0. No new migration is required by v1.0.0 itself. A database that is still at schema v3 (for example, an existing v0.8 database whose earlier v0.9 update did not reach the verification stage) is upgraded additively to v4 when the v1.0.0 verifier opens it. Existing blocks, wallets, financial snapshot, and mempool entries are preserved. The release updater removes only generated build outputs before compilation, preventing stale object files from being reused across source versions; `.git/` and `runtime/` are preserved.

## Known limitations and non-claims

- Crash faults only; no Byzantine fault tolerance or malicious-member safety.
- Static membership only; no joint-consensus membership changes or safe dynamic reconfiguration.
- No log compaction, snapshot transfer, or bounded-history catch-up for long-lived clusters.
- Authenticated frames are signed and replay-protected but not encrypted; TLS is not implemented.
- Transactions admitted to a node's local durable mempool are not automatically gossiped. Submit transactions to the current leader before proposing a block.
- The live integration tests cover controlled local loopback scenarios; they are not an exhaustive adversarial network, packet-loss, resource-exhaustion, or production operations assessment.
- The local Bridge, off-chain Match/Flow engines, and private FST Ledger are prototypes, not production custody, an externally audited exchange, real-money payment rails, or a public cryptocurrency.
- SQLite transaction boundaries and restart tests reduce application-level inconsistency, but are not a formal proof against every filesystem/controller/hardware power-loss scenario.

FeinStatic v1.0.0 demonstrates an operational static Raft-backed Ledger prototype under the tested local crash-fault scenarios. It must not be described as Byzantine fault tolerant, Internet-ready, or production financial infrastructure.
