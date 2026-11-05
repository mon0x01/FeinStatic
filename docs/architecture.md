# FeinStatic architecture (v1.9.0)

## Overview

`feinstatic_core` is a reusable C++20 domain library. The terminal executable is a client of public APIs; it does not own financial state transitions. FeinStatic combines a private native-asset Ledger, two off-chain financial engines, a shared settlement book, a local Ledger/settlement bridge, authenticated TCP peer communication, and a Raft runtime for crash-fault clusters with joint-consensus voter reconfiguration.

The major boundaries are:

- **Ledger:** native FST accounts, signed transactions, proof-of-work blocks, chain validation, durable mempool, and replay protection.
- **Match:** deterministic price-time-priority order book, reservations, execution, and trade history.
- **Flow:** constant-product AMM, pool reserves, swaps, LP positions, and checked integer arithmetic.
- **SettlementBook:** canonical off-chain account balances, available/reserved funds, internal transfers, and journal.
- **FinancialPlatform:** shared candidate-state boundary coordinating Match and Flow through one SettlementBook and one persisted platform snapshot.
- **BridgeService:** recoverable native-FST workflow between the private Ledger and off-chain SettlementBook.
- **Storage:** SQLite schema/migrations, block/wallet rows, durable mempool, financial snapshots, and Raft state.
- **Network:** bounded TCP framing, independent Ed25519 node identities, explicit trust stores, encrypted authenticated sessions, inbound token-bucket admission, a bounded inbound socket worker pool, and shared Raft/transaction-gossip dispatch.
- **Consensus:** transport-agnostic Raft kernel plus `RaftLedgerAdapter`, which maps committed log commands to Ledger block application.

## State ownership and financial units

Ledger balances and off-chain SettlementBook balances are separate accounting domains. A wallet address can identify a SettlementBook account, but linking it does not transfer Ledger funds. The Bridge is the explicit local workflow for confirmed native-FST deposits and collateral-backed withdrawals.

All monetary amounts use integer atoms. Match uses checked integer arithmetic for quote notional, fees, and reservations. Flow uses Boost.Multiprecision 256-bit intermediate values for constant-product calculations before checked conversion to 64-bit stored amounts. Floating-point values are not used as canonical persisted balances.

## Shared settlement and platform transaction boundary

Match and Flow each stage their engine transition before calling the shared SettlementBook batch API. The `FinancialPlatform` coordinator creates one candidate state, applies the requested operation, validates cross-engine invariants, serializes the complete snapshot, commits it through SQLite with an optimistic revision check, and only then publishes the candidate in memory.

The snapshot contains SettlementBook accounts/balances/journal, Match accounts/orders/trades/counters, Flow accounts/pools/LP positions/swaps/counters, and Bridge vault/deposit/withdrawal records. This is intentionally a bounded full-snapshot design. It favors straightforward atomicity and local recovery over high write throughput as state grows.

## Ledger and Bridge persistence boundary

Ledger block append and the off-chain FinancialPlatform snapshot remain distinct database transactions. Bridge operations therefore use a recoverable workflow rather than claiming a cross-domain atomic commit. Deposits are credited idempotently only after confirmation on the local chain. Withdrawals persist the customer debit and exact signed transaction before durable mempool submission, then reconcile after block confirmation or restart.

## SQLite schema

Schema v5 stores:

- `blocks` and `wallets` for the Ledger;
- `financial_state` for the versioned, checksummed FinancialPlatform snapshot;
- `mempool_transactions` for ordered durable transaction admission;
- `raft_state` for a revisioned, checksummed consensus-state snapshot;
- `raft_snapshots` for separately checksummed Ledger application snapshots keyed by Raft index.

The v1-to-v2 migration adds financial state, v2-to-v3 adds durable mempool rows, v3-to-v4 adds Raft state, and v4-to-v5 adds the separate application-snapshot table. These migrations are additive. v1.3.0 leaves existing Ledger, wallet, and financial-state payloads intact while allowing schema version to advance to 5.

The Raft state binds the configured cluster ID, sorted membership fingerprint, and exact Ledger checkpoint to term, vote, retained log suffix, snapshot boundary, commit index, and applied index. Each application snapshot has a separate SHA-256 checksum; checksums detect accidental corruption but are not authentication against a local attacker able to rewrite the database and recompute them.

## TCP peer networking

The request/response protocol is bounded and uses wire version 8. Every authenticated connection performs Ed25519 mutual challenge-response and binds both ephemeral X25519 public keys into the signed session transcript. HKDF-SHA-256 derives directional AES-256-GCM keys; every post-handshake application frame is encrypted and authenticated with session/type/sequence associated data, and also signed over its ciphertext. This supplies peer authentication, integrity, confidentiality for application payloads, and replay resistance. It is not TLS and does not hide transport metadata.

A single accept loop applies global and per-address token buckets before placing accepted sockets into a bounded queue. A fixed-size inbound worker pool (default four, maximum sixteen) performs handshake and frame I/O on worker-owned Asio contexts. The pending queue defaults to 64 sockets and is capped at 4096; when full, newly accepted sockets are closed instead of accumulating without bound. Each worker can wait on one connection at a time, with every socket operation bounded by the configured peer timeout. A per-node state mutex serializes all reads and writes of Ledger, Raft, trust-policy, and endpoint state. Request handlers prepare their response while holding that mutex, release it, and only then perform network writes. Thus slow handshakes and slow response readers no longer monopolize the accept loop or state lock, while Ledger/Raft mutation remains single-writer. In Raft mode, a fixed-size shared outbound worker pool dispatches separate bounded per-peer queues for consensus RPCs and best-effort transaction gossip. Raft traffic has priority with an eight-message streak limit to prevent permanent gossip starvation. Each peer has at most one in-flight message, preserving FIFO within each traffic class. Retryable failures are requeued at the head with capacity accounting retained; Raft and gossip share a peer-level circuit breaker with capped exponential backoff and stable peer-specific jitter. The lifecycle reports UNKNOWN, HEALTHY, DEGRADED, OFFLINE, HALF_OPEN, or DISABLED; one in-flight operation is the half-open recovery probe. A periodic durable-mempool scan reconstructs gossip after restart and relieves bounded queue backpressure. Operator health reports expose combined queue and failure counters.

## Raft runtime and Ledger application

The v0.9.0 Raft kernel remains a static, permissioned crash-fault state machine. v1.0.0 supplies the live TCP/Ledger integration through `NodeServer` and `RaftLedgerAdapter`; v1.2.0 adds the snapshot/compaction path and v1.3.0 adds durable joint-consensus membership transitions:

1. A network Raft RPC is accepted only from an authenticated cluster member. The transport peer ID must equal `RaftMessage::from`; `RaftMessage::to` must equal the local member ID.
2. The main server loop serializes `tick`, `receive`, proposals, and application. The outbound dispatcher workers only send already-created messages using immutable chain parameters.
3. Empty Raft commands are internal no-ops. Non-empty commands use a versioned `FSBC` envelope containing a canonical serialized Ledger block.
4. Only a ready Raft leader may create a block proposal. It validates its candidate block locally and submits its command to the replicated log.
5. Followers validate replicated blocks against their local Ledger state before acknowledging append success.
6. A committed entry advances `last_applied` in log order. The Ledger block append (when the entry is a block command), durable-mempool reconciliation, and new Raft snapshot/applied index are persisted in one SQLite transaction.
7. In-memory state is published only after the transaction succeeds. A failed durable state write fails the Raft node closed.
8. On restart, the adapter validates the durable snapshot's complete Ledger prefix and then checks that the retained applied Raft log suffix exactly accounts for the recovered Ledger suffix. Divergent or out-of-band Ledger state is rejected.
9. If a peer's next log index has been compacted, the leader streams its application snapshot in bounded chunks. The receiver validates the complete Ledger and atomically installs the Ledger rows, Raft state, and snapshot record; AppendEntries then transfers any suffix after that boundary.

Raft log indices and Ledger block heights are not interchangeable: no-op entries consume log indices without creating blocks. This matters when observing `commit_index`, `last_applied`, and Ledger height.

In Raft mode, direct legacy block submission and legacy pull-based chain synchronization are disabled so they cannot mutate a member's canonical chain outside consensus. Transactions can be admitted to any member's durable mempool and are gossiped best-effort to other enabled, pre-provisioned members. Gossip dissemination is not consensus: a transaction becomes canonical only after leader proposal, majority commitment, and ordered application.

## Cluster and fault assumptions

Raft voter membership is odd-sized (3, 5, 7, or 9) and can change through the two-entry joint-consensus transition. A shared, pre-provisioned roster may contain up to nine voter/learner endpoints; learners replicate but do not vote, and must catch up to the committed index before promotion. Every node shares the exact initial roster, cluster ID, Ledger checkpoint, and compatible chain parameters. Each member owns a database and trusts the other provisioned identities. The implementation models crash/recovery failures with non-Byzantine members. v1.2.0 compacts applied log prefixes and transfers bounded full-Ledger snapshots; v1.3.0 persists membership in both current and snapshot-boundary state. It does not provide Byzantine tolerance, peer discovery, TLS, a complete DDoS defense, or production distributed finality. v1.7 inbound token buckets bound connection/handshake attempt rates only. Snapshot payloads are capped at 64 MiB and contain the complete Ledger history, so this is not yet a state-only or unbounded snapshot design.

See [`networking.md`](networking.md), [`raft.md`](raft.md), and [`raft-networking-guide.md`](raft-networking-guide.md).
