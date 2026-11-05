# FeinStatic v1.2.0 verification report

## Release scope

v1.2.0 adds Raft applied-prefix compaction and bounded snapshot transfer. The immutable bootstrap checkpoint remains distinct from the moving compacted boundary. Snapshot payloads are checksummed and stored separately in SQLite schema v5. `InstallSnapshot` transfers a full Ledger snapshot in chunks of at most 512 KiB, with a 64 MiB total payload limit. The receiver validates the complete payload and atomically installs the Ledger history, durable mempool state, Raft state, and snapshot record in one SQLite transaction.

The default compaction threshold is 128 applied Raft entries. When a follower falls behind the leader's compacted prefix, it receives a snapshot and then the remaining AppendEntries suffix.

## Test inventory and build matrix

The source tree contains 130 registered `FS_TEST` test cases. The final source archive was extracted into a fresh directory and rebuilt. Results:

| Configuration | Build | Test execution |
|---|---|---|
| GCC Debug | Passed | CTest: 130/130 passed |
| GCC Release | Passed | CTest: 130/130 passed |
| GCC ASan/UBSan | Passed | Direct test binary: 130 passed, 0 failed |
| Clang Release + `-Werror` | Passed | Direct test binary: 130 passed, 0 failed |

In this development container, CTest invocations for ASan/UBSan and Clang exceeded the outer command window after starting, although direct execution of both test binaries completed successfully. The clean archive also passed the full nine-stage real-process Raft lab, including authenticated TCP snapshot installation. The aggregate `scripts/verify-all.sh` wrapper was not used as the sole acceptance signal because the development container imposed execution limits.

## Snapshot and persistence coverage

The deterministic in-process tests cover:

- Multi-chunk Raft snapshot transfer and byte-offset acknowledgements.
- Receiver process restart during a partial transfer; because partial chunks are currently in memory, the receiver requests offset zero and the sender retransmits the full snapshot.
- Snapshot metadata and durable payload recovery.
- Automatic compaction and restoration of a lagging Ledger node.
- Restart after snapshot installation.
- Injected SQLite snapshot-row failure, proving the Ledger and Raft snapshot install transaction rolls back atomically and can be retried.
- Additive SQLite schema migration and backward deserialization of v1 Raft state/message encodings.

The real three-process TCP lab passed with three independent FeinStatic OS processes, three independent SQLite databases, separately generated Ed25519 identities, and per-directed-link TCP fault proxies. It verified leader election, block replication, `SIGKILL` leader failover, former-leader restart/catch-up, minority isolation, no-quorum commit prevention, majority progress, partition healing, conflict repair, and convergence.

The v1.2 extension then stopped one process while the other two committed 136 additional blocks. The majority leader persisted a snapshot at Raft index 128. After restart, the lagging process installed snapshot 128 into its own durable SQLite database over the authenticated TCP transport and converged at Ledger height 139. The receiver's durable `raft_snapshots` row was checked rather than inferring snapshot installation from Ledger height alone.

## Demonstration and benchmark

The Release Ledger, matching-engine, and AMM demonstrations passed.

The development-container Match benchmark reported:

- Compiler: GCC 14.2.0.
- Hardware threads reported: 5.
- Workload: 1,000,000 synthetic same-price in-memory order operations.
- Completed trades: 500,000.
- Elapsed time: 3.057283 seconds.
- Throughput: 327,087.81 order operations/second.
- Average latency: 3.02 microseconds; p50 1.84 microseconds; p95 3.25 microseconds; p99 4.82 microseconds.

This benchmark covers one process and an in-memory matching book. It excludes networking, cryptographic framing, SQLite persistence, Raft replication, and Ledger application. Results vary by machine and system load; this is not distributed throughput.

## Persistence and update behavior

v1.2.0 adds SQLite schema v5 by creating the `raft_snapshots` table. Existing schema v4 databases are migrated additively when opened. The Arch updater creates a full pre-update backup and preserves `.git/` and `runtime/`. It removes only generated `build/` output. It checks that the database file is unchanged during source integration, then permits the expected schema migration and compares the schema and data of every table that existed before the update. The new `raft_snapshots` table is excluded from that pre-existing-data comparison.

## Known limitations

- Raft remains static-membership and crash-fault tolerant only; it is not Byzantine fault tolerant and does not support joint-consensus membership changes.
- The application snapshot stores the complete Ledger block history, not a compact account-state image. The 64 MiB total snapshot limit therefore bounds chain growth; a state-only or segmented format is a future scaling requirement.
- Snapshot chunks are assembled in receiver memory. Restart during transfer discards partial progress and causes a restart from byte zero.
- Snapshot validation replays the full Ledger history, so restore cost increases with chain history.
- Peer frames remain authenticated, signed, and replay-protected, but are not encrypted. TLS/confidentiality is not implemented. Use loopback or a trusted isolated network only.
- The tests do not prove every filesystem, kernel, controller, or hardware power-loss mode; they are not a formal verification of consensus safety.
- This is a private engineering prototype, not production custody, an exchange, or public-chain infrastructure.

## User-machine verification required

The tests above ran in the development container, not on the user's Arch Linux machine. The supplied updater must complete locally before v1.2.0 is considered installed there. Its terminal output is needed to verify local compilers, sanitizer runtime behavior, the user's existing database content, backup path, and completion status.
