# FeinStatic 2.0.0 — Crash-resumable Raft snapshot installation

## Summary

FeinStatic 2.0.0 makes acknowledged incoming Raft snapshot transfer progress durable. Previously, the receiver kept the partial `InstallSnapshot` byte buffer only in memory. A receiver crash therefore discarded the acknowledged prefix and forced the leader to transmit the snapshot from byte zero.

The new receiver persists each chunk in SQLite before acknowledging its next offset. It resumes a contiguous partial payload after restart, negotiates the durable offset with a sender whose in-memory offset restarted at zero, and retries installation if the receiver crashed after storing the final chunk but before applying the snapshot.

## Transfer lifecycle

1. The authenticated peer sends an `InstallSnapshot` chunk with snapshot metadata and an offset.
2. The receiver checks source membership, transfer identity, expected offset, chunk size, total size, and configured bounds.
3. The receiver commits the chunk and updated `next_offset` to SQLite (`synchronous=FULL`).
4. Only after the database transaction commits does the receiver acknowledge the new offset.
5. On receiver restart, staging metadata and ordered chunks are loaded. The receiver verifies chunk contiguity and the stored next offset.
6. If the sender starts again at offset zero, the receiver responds with its durable expected offset; transfer resumes from that point.
7. When the full payload is present, SHA-256 is checked, the candidate Raft membership/state is validated, and the Ledger/mempool/Raft snapshot is atomically installed.
8. Staging rows are cleared only after successful installation. A failed install retains the complete staged payload; startup retries it and fails closed if it cannot be applied.

## Storage migration

SQLite schema advances from v5 to v6 by adding:

- `raft_snapshot_staging`: one active transfer's source, snapshot index/term, total size, checksum, membership bytes, and next expected offset.
- `raft_snapshot_staging_chunks`: ordered chunk payloads keyed by byte offset.

The migration is additive. It does not rewrite existing Ledger blocks, wallet rows, financial state, mempool rows, Raft consensus state, or completed application snapshots. The authenticated peer wire protocol remains v8; no message encoding change is required.

## Compatibility and operations

- All cluster members should run the same FeinStatic application release during a coordinated upgrade.
- The updater must back up the complete project/runtime before the v5 -> v6 schema migration because the v1.9 binary does not understand schema v6.
- Existing protocol-v8 snapshot messages remain unchanged.
- A single incoming snapshot transfer is staged per node.
- Chunk size remains at most 512 KiB; total application snapshot size remains at most 64 MiB.

## Important limitations

This release makes transfer progress durable; it does not make the snapshot format scalable without bound. The application snapshot still serializes the complete Ledger history, is capped at 64 MiB, and is reassembled in memory for final checksum validation and Ledger replay. State-only snapshots, segmented state export/import, larger payload support, automatic peer discovery, and Byzantine fault tolerance remain future work.

## Validation added

The new regression tests persist a partial 512 KiB prefix, restart the receiver, check offset renegotiation after the sender starts at zero, stage the remaining payload, inject failure after the final chunk is durable, restart again, complete the atomic installation, and verify staging cleanup. A separate v5-to-v6 migration test verifies that exactly the two staging tables are added while existing Ledger, Raft-state, and completed-snapshot data remain readable. The complete release validation matrix is run by `scripts/verify-all.sh`.
