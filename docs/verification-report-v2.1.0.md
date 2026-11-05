# FeinStatic 2.1.0 verification report

## Source and scope

This report covers the bounded-memory Raft snapshot transport changes in v2.1.0. It does not claim completion of state-only Ledger checkpoints or unbounded history synchronization.

## Automated test matrix

| Configuration | Compiler/runtime | Result |
|---|---|---:|
| Debug | GCC 14.2.0 | 163 / 163 passed |
| Release | GCC 14.2.0 | 163 / 163 passed |
| ASan/UBSan | GCC 14.2.0 | 163 / 163 passed |
| Release, `-Werror` | Clang 17.0.0 | 163 / 163 passed |

Total: **652 successful test executions** across the four configurations.

New range-read regression coverage verifies incremental SQLite BLOB reads at the 512 KiB boundary, final partial range reconstruction, exact payload equality and out-of-range rejection. Receiver staging tests verify metadata-only recovery, durable offset negotiation after restart, range reconstruction of the durable prefix, and completed-transfer recovery after an injected installation failure.

## Application checks

- Release `feinstatic demo`: PASS.
- Release Match benchmark, 1,000,000 synthetic orders: PASS; 500,000 trades, 468,693.74 orders/second, p99 4.52 microseconds in this development container. This is an in-memory synthetic benchmark, not a network/persistence throughput claim.

## Independent-process labs

The 3-process Raft chaos lab completed leader election, block commitment, SIGKILL failover, former-leader restart/catch-up, minority non-commit, and partition healing/convergence. It reached the snapshot-transfer workload and committed 64 of 136 workload blocks before the development execution window terminated the lab process. Therefore the complete final TCP snapshot catch-up phase is **not marked PASS here**.

The 4-process joint-consensus lab did not return a complete result within the development execution window and is **not marked PASS here**.

The release updater reruns both independent-process labs on the target Arch system. Installation/release acceptance requires both to print their explicit `... LAB: PASS` result and exit successfully. These labs are not skipped or treated as passed by this report.

## Compatibility and limits

- Raft authenticated application protocol remains v8.
- SQLite schema remains v6; no migration is introduced.
- Segment size remains 512 KiB.
- Total application snapshot size remains capped at 64 MiB.
- Existing runtime data is verified by logical schema/data fingerprints before and after the update; no data rewrite is intended.
- The sender and receiver now avoid full-payload RAM copies during transfer. Final installation still assembles one complete payload buffer and validates/replays the full Ledger history. State-only snapshots, historical segment export/import, history pruning and larger-than-64-MiB snapshots remain future work.

## Security and production status

This is a crash-fault Raft prototype. The test matrix does not establish Byzantine safety, Internet-scale DoS resistance, external cryptographic audit, production key custody, formal filesystem crash-consistency, exchange/payment compliance or safety for real customer funds.
