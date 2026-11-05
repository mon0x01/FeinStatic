# FeinStatic 2.1.0 — Bounded-memory snapshot transport

## Engineering change

Raft snapshot transfer used to create avoidable full-payload allocations on both sides of the wire. The sender loaded and retained the completed SQLite snapshot BLOB before sending its first segment. The receiver wrote every segment to durable SQLite staging but also appended all bytes to a second in-memory buffer. After restart, the staging loader reconstructed the whole durable prefix into memory before the next RPC.

Version 2.1.0 changes those transport boundaries:

- The leader reads only the requested range from SQLite with `sqlite3_blob_open` / `sqlite3_blob_read`.
- The receiver stores acknowledged segments and the next offset in SQLite, then advances only an offset counter in memory.
- Startup validates transfer metadata and the ordered segment lengths/offsets without copying payload bytes into RAM.
- At completion, the receiver assembles the full payload once, checks the exact expected size and SHA-256, and calls the existing atomic Ledger/Raft installer.
- If the final checksum is invalid, staged bytes are discarded instead of leaving a complete corrupt transfer that cannot make forward progress.

## Compatibility

- Authenticated Raft protocol: v8, unchanged.
- SQLite user_version: 6, unchanged; no migration is required.
- Maximum RPC snapshot segment: 512 KiB, unchanged.
- Maximum complete snapshot: 64 MiB, unchanged.
- Existing snapshot metadata, payload encoding and Ledger replay semantics remain compatible with v2.0.0.

## Explicit boundary

This release removes avoidable full-snapshot memory use during transport. It does **not** yet make Ledger synchronization independent of complete chain history. Compaction still serializes all blocks, the application install still creates one complete payload buffer, the Ledger still retains/replays the full chain, and the 64 MiB cap remains. The next architectural milestone is a versioned Ledger state checkpoint (account state, transaction replay index, supply and tip) plus separately retained/exportable block-history segments. That requires coordinated changes to Chain, Ledger persistence/recovery, historical transaction lookup and fork-choice semantics; it should not be approximated by merely increasing the snapshot cap.
