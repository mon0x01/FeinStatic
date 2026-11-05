# FeinStatic 2.0.0 — Verification report

## Release scope

v2.0.0 introduces crash-resumable Raft snapshot installation. Each accepted `InstallSnapshot` chunk is committed to SQLite staging before its next byte offset is acknowledged. The receiver can reload an acknowledged prefix after restart, negotiate that offset with a sender whose in-memory offset restarted at zero, and retry an install after a crash between durable receipt of the final chunk and application installation.

SQLite schema advances additively from v5 to v6. The authenticated peer wire protocol remains v8. Snapshot serialization still contains the complete Ledger history and remains capped at 64 MiB; the receiver assembles the complete payload in memory for final validation and installation. This release does not implement state-only snapshots, segmented Ledger export/import, unbounded transfer sizes, automatic peer discovery, or Byzantine fault tolerance.

## Automated test matrix

Validated in the development container on GCC 14.2.0, Clang 17.0.0, OpenSSL 3.5.7, and SQLite 3.46.1.

| Configuration | Result |
|---|---:|
| GCC Debug | 163 / 163 passed |
| GCC Release | 163 / 163 passed |
| GCC AddressSanitizer + UndefinedBehaviorSanitizer | 163 / 163 passed |
| Clang Release with `-Werror` | 163 / 163 passed |

Total: 652 successful test executions across four configurations; zero test failures.

## Snapshot staging coverage

The new tests cover:

- durable commit of each chunk before acknowledging the next expected byte offset;
- receiver restart with a partial 512 KiB prefix;
- sender restart from byte offset zero and receiver-directed offset renegotiation;
- completion and SHA-256 verification of the staged payload;
- a crash/failure after the final chunk is durable but before application installation;
- restart-time completion of the staged application install and clearing staging only after success;
- a direct SQLite v5 -> v6 migration that adds both staging tables while preserving the existing Ledger, Raft-state, and completed-snapshot data.

## Multi-process integration laboratories

**Three-process Raft chaos laboratory: PASS.** The real-process test elected a leader, committed a Ledger block, killed the leader, elected a replacement, restarted the former leader, isolated a leader from both peers, verified minority non-commit and majority progress, healed the partition, repaired the conflicting suffix, then transferred a compacted snapshot over authenticated TCP to a lagging process. The receiver installed snapshot index 128 and converged at Ledger height 139.

**Four-process joint-consensus membership laboratory: PASS.** The test ran three voters and one pre-provisioned learner. The learner remained offline while the voters committed 136 blocks and compacted at index 128. After restart it installed the snapshot and caught up, was promoted while the current leader was removed, restarted as a learner, and was re-promoted in the reverse transition. The final cluster converged to three voters and one learner.

These process labs validate the existing consensus and membership paths against the v2.0.0 Release binary. They do not simulate a receiver process being killed mid-snapshot over the actual TCP process lab; the new receiver-crash/resume test exercises the durable SQLite staging and Raft callbacks directly with real SQLite restarts.

## Demonstration and benchmark

The Release demonstration passed for Ledger, Match, and Flow. The synthetic same-price in-memory Match benchmark processed 1,000,000 order operations at 423,110.81 operations/second, with p99 latency of 4.62 microseconds in this container. It is a single-process microbenchmark and excludes persistence, networking, and distributed consensus; it is not production throughput.

## Not yet verified

- The new updater and schema migration have not yet been run on the user's Arch checkout; the updater will run the full matrix and compare every pre-existing SQLite table before pushing Git checkpoints.
- The v2.0.0 snapshot staging changes were not subjected to a separate ThreadSanitizer run. The new storage callback path is serialized by the existing Raft/server state boundary; this is not a formal concurrency proof.
- No independent cryptographic audit, Byzantine fault-tolerance proof, or production financial-system certification is claimed.
