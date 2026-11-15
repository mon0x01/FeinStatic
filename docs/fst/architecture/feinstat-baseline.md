# FeinStatic Technical Baseline

**Status:** HISTORICAL REFERENCE — records the existing FeinStatic architecture as of
commit `a8eac41` (release v2.1.0).
**Classification:** This document describes existing implementation; it is not an FST
design decision. Transformation assessments are initial findings, not irreversible
implementation decisions.

This document records the existing FeinStatic architecture and the initial assessment of
each subsystem for reuse, redesign, or replacement in FST. The FeinStatic source code,
tests, release notes, and Git history are preserved unchanged.

## Existing subsystems

| Subsystem | Location | Initial transformation assessment |
|-----------|----------|-----------------------------------|
| Ledger data structures | `src/ledger/`, `include/feinstat/ledger/` | Potentially reusable after specification review |
| Cryptographic primitives | `src/crypto/`, `include/feinstat/crypto/` | Reuse only after cryptographic and security review |
| Raft consensus | `src/consensus/raft.cpp`, `raft_membership.cpp` | Preserve as reference; research replacement consensus |
| TCP networking | `src/network/node.cpp`, `peer_identity.cpp`, `peer_lifecycle.cpp` | Preserve useful engineering patterns; research new P2P networking architecture |
| Match order book | `src/exchange/matching_engine.cpp` | Preserve matching algorithms as reference |
| Flow AMM | `src/flow/amm.cpp` | Potentially reuse mathematical and arithmetic components |
| SettlementBook | `src/settlement/settlement_book.cpp` | Preserve useful accounting concepts as reference |
| FinancialPlatform | `src/platform/financial_platform.cpp` | Requires architectural redesign |
| BridgeService | `src/bridge/bridge_service.cpp` | Existing single-vault architecture conflicts with decentralisation |
| SQLite persistence | `src/storage/sqlite_store.cpp` | Reuse appropriate persistence engineering after scalability assessment |
| Snapshot transfer | `src/storage/sqlite_store.cpp`, `src/consensus/raft.cpp` | Preserve useful crash-recovery and streaming patterns |
| CLI | `apps/feinstatic-tui/main.cpp` | Existing interface is a development tool, not the eventual network interface |
| Tests | `tests/` | Preserve and extend testing patterns |

## Key architectural characteristics of FeinStatic v2.1.0

### Language and build
- C++20, CMake 3.25+, Ninja.
- Dependencies: OpenSSL 3.x, SQLite 3.24+, Boost 1.74+ (headers only for Multiprecision).
- Build presets: debug, release, asan (ASan/UBSan), clang-release (-Werror).
- CI: GitHub Actions on ubuntu-24.04 running `scripts/verify-all.sh`.

### Ledger
- Native FST asset with Ed25519 wallets, AES-256-GCM-encrypted secrets (PBKDF2-HMAC-SHA256).
- SHA-256 addresses (`fs1…` prefix), signed transfers, nonce and replay protection.
- Fixed-difficulty Proof of Work mining with block reward and fees.
- Hash-linked blocks, total-supply accounting.
- Durable bounded mempool (10,000 transactions / 32 MiB serialized data) in SQLite.
- Restart replay recovery with revalidation.

### Financial engines
- **Match:** Price-time-priority FST/USD limit order book, reservations, IOC orders,
  cancellation, self-trade prevention, atomic preflight-and-commit execution.
- **Flow:** Constant-product AMM pools, LP shares, swaps, fees, price-impact estimates,
  Boost.Multiprecision 256-bit intermediate arithmetic with checked u64 conversion.
- **SettlementBook:** Shared account balances with total/reserved/available amounts,
  journal entries, internal transfers.
- **FinancialPlatform:** Candidate-state coordinator; every mutation serialises a full
  snapshot, commits via `BEGIN IMMEDIATE` with optimistic revision checking, publishes
  in-memory only after commit.

### BridgeService
- Native-FST-only workflow between the private Ledger and off-chain SettlementBook.
- Single dedicated vault wallet; one pending withdrawal at a time.
- Confirmed deposits credited idempotently after chain confirmation.
- Signed withdrawals persisted before mempool submission, with retry and reconciliation.
- Collateral check: vault balance must cover off-chain liabilities plus pending reserves
  plus network fees.
- **Conflict with FST:** Single-vault custody is a centralised trust point (principles P1,
  P2, P8).

### Networking (protocol v8)
- Authenticated TCP peer transport with Ed25519 mutual challenge-response.
- Ephemeral X25519 key agreement, HKDF-SHA-256 directional keys, AES-256-GCM frame
  encryption, per-message Ed25519 signatures, sequence/replay protection.
- Application-layer encryption, **not** TLS; no independent cryptographic audit.
- Inbound token-bucket admission (global + per-address), bounded inbound worker pool
  (4–16 workers), bounded pending queue (64–4096).
- Shared outbound dispatcher with per-peer split Raft/gossip queues, one in-flight per
  peer, 8-message Raft priority streak.
- Peer lifecycle circuit breaker (6 states: Unknown, Healthy, Degraded, Offline,
  HalfOpen, Disabled) with capped exponential backoff (250 ms – 30 s) and jitter.
- Endpoint registry (`<db>.endpoints`): checksummed, owner-only (0600), atomic
  rename+fsync, bound to cluster ID and exact roster.

### Consensus (Raft)
- Transport-agnostic Raft state machine: elections, AppendEntries, InstallSnapshot,
  commit, apply, compaction.
- Joint-consensus voter transitions (C_old,new → C_new), learners, max 9 voters.
- Pre-provisioned peer roster; no peer discovery.
- Crash-fault tolerance only; not Byzantine fault tolerant.
- `RaftLedgerAdapter` maps committed block commands (FSBC-enveloped canonical blocks) to
  Ledger blocks; application snapshots serialise full Ledger history (64 MiB cap).
- `RaftStateStore`: revisioned + checksummed raft_state row, raft_snapshots table,
  raft_snapshot_staging and chunks tables (durable resumable transfers, v2.0.0/v2.1.0).
- Deterministic local fork choice (height = work, lexicographic tip-hash tie-break).

### Persistence
- SQLite schema v6: blocks, wallets, financial_state, mempool_transactions, raft_state,
  raft_snapshots, raft_snapshot_staging, raft_snapshot_staging_chunks.
- WAL journal mode, synchronous=FULL, `PRAGMA quick_check(1)` on open.
- `BEGIN IMMEDIATE` transactions; file permissions restricted to 0600 after writes.
- Additive schema migrations (v2→v6); `PRAGMA user_version` guard.
- SHA-256 checksums on persisted payloads (corruption detection, not authentication).

### CLI and tests
- Line-oriented terminal menu (`apps/feinstatic-tui/main.cpp`, ~1,349 lines).
- Commands: demo, init, open, verify, node-keygen, peer-trust, raft-node,
  raft-status/propose/change-members/peer-health/peer-endpoint-set, trust-policy-push,
  peer-status/sync/submit-tx/relay-tip.
- 163 test cases across 18 test files (per documentation; not re-executed in this audit).
- Multi-process Raft chaos lab and joint-consensus membership lab harnesses
  (`scripts/raft-process-chaos.py`, `scripts/raft-membership-process.py`).

## Transformation findings summary

FeinStatic is a well-engineered **crash-fault, permissioned, private-test distributed
ledger prototype**. It cannot be incrementally extended into FST — an independent,
globally decentralised, permissionless financial network — because its core consensus
(Raft), trust model (pre-provisioned rosters, operator roles, endpoint registry),
networking (authenticated TCP only), and cryptography (authenticated encryption, not
privacy) are architecturally incompatible with the FST vision.

The strongest technically justified path is **selective reuse plus foundational
redesign**: preserve Ledger data structures, financial engines, persistence layer, and
networking primitives as reference/components, but replace consensus with a Byzantine
fault-tolerant permissionless protocol, rebuild networking for multi-transport P2P with
discovery, add privacy-preserving cryptography, and remove all privileged/centralised
trust assumptions.

## Cross-references

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope.
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — Approved engineering principles.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications.
