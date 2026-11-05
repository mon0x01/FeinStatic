# Changelog

## 2.1.0 — 2026-11-05

### Added
- SQLite incremental-BLOB range reads for stored Raft snapshots, allowing the sender to emit bounded 512 KiB chunks without caching the full snapshot payload in RAM.
- Metadata-only recovery for incoming snapshot staging, including durable next-offset negotiation after receiver restart.
- Bounded range reads of staged incoming chunks, assembling the payload only at the existing atomic Ledger-install boundary.
- Regression coverage for multi-range reads, final partial ranges, out-of-range rejection, and restart/resume metadata.

### Changed
- Snapshot transfer now uses the durable SQLite staging store as the source of truth for transfer progress and payload bytes.
- Corrupt completed staging data is cleared when final digest validation fails, so a bad complete transfer does not remain permanently resumable.

### Compatibility and known limits
- Raft application protocol remains v8.
- SQLite remains schema v6; this release introduces no database migration.
- The 64 MiB snapshot limit remains in place.
- Final installation still materializes one complete snapshot payload and replays the complete Ledger history. State-only checkpoints, segmented historical export/import, history compaction, and larger-than-64-MiB snapshots are not part of this release.

## 2.0.0

- Added crash-resumable Raft snapshot installation with SQLite-durable chunk staging and additive schema migration v5 to v6.
- Added restart recovery for partially received and completed snapshot transfers.
