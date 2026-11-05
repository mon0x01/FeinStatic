# FeinStatic

**FeinStatic** is a modular C++20 financial-infrastructure and distributed-systems prototype. It combines a private native-asset Ledger, an off-chain order-matching engine, an automated market maker, shared settlement, a Ledger/settlement bridge, authenticated peer networking, and a crash-fault Raft cluster with snapshot recovery and joint-consensus voter changes.

Release: **1.8.0**. The terminal application is line-oriented. This is educational/private test infrastructure, not production financial infrastructure or a public cryptocurrency.

## Version 1.8.0 changes

- Replaces synchronous one-connection-at-a-time handling with a bounded inbound socket worker pool (default four workers, maximum sixteen) and a bounded pending-connection queue (default 64, maximum 4096).
- Keeps the accept loop lightweight; independent workers perform bounded handshake and frame I/O using worker-owned Asio contexts.
- Serializes every Ledger, Raft, trust-policy, and endpoint state operation behind one per-node state mutex. Response frames are prepared while the state is protected, then written after releasing the mutex so a slow remote reader cannot hold the state boundary.
- Rejects new accepted sockets when the worker queue is full instead of allowing unbounded connection accumulation. Existing global/per-address token-bucket admission remains in front of the queue.
- Adds real TCP tests for multiple stalled handshakes while a fourth worker continues serving, twelve parallel status clients, queue saturation/recovery, and the concurrent-run lifecycle regression.
- Keeps authenticated wire protocol v8 and SQLite schema unchanged. All members continue to use independent SQLite databases.

## Version 1.7.0 changes

- Adds global and per-address token-bucket admission before peer handshake parsing, with bounded address tracking and idle expiry.
- Adds automatic transaction gossip across enabled, pre-provisioned Raft members and learners. The durable mempool is the recovery source after restart; bounded queue backpressure is handled by periodic refill.
- Refactors outbound dispatch into separate bounded Raft and gossip queues, reserves capacity for consensus, applies an eight-message Raft priority streak to prevent permanent gossip starvation, and uses independent retry timers.
- Fixes retry semantics: a failed retryable message is requeued at the head of its peer queue and its in-flight capacity remains reserved until delivery or terminal rejection.
- Adds authenticated multi-process TCP tests for inbound rate limiting and transaction dissemination through origin restart with receivers offline.
- Upgrades the authenticated peer wire protocol to v8. The SQLite schema is unchanged.

## Version 1.6.1 changes (previous release)

- Hardens the real-process joint-consensus lab against the asynchronous boundary between follower application and the leader processing the corresponding AppendResponse. The lab uses the current leader at transition time and retries only the explicit pre-append "new voter is not caught up" rejection while the leader receives the replication acknowledgement.
- The retry never bypasses the Raft promotion guard: no membership command is appended until the leader's `match_index` satisfies the committed-index requirement. Other errors remain immediate failures; a bounded timeout retains logs and reports the last rejection.
- No Raft wire-protocol, SQLite schema, or persistent-state format change.

## Version 1.6.0 changes

- Adds a per-node, owner-only, checksummed endpoint registry at `<database>.endpoints`. It is bound to the configured cluster ID and exact pre-provisioned peer roster; invalid ownership, permissions, checksum, cluster binding, generation, or roster causes startup to fail closed.
- Adds authenticated operator commands `raft-peer-health-auth` and `raft-peer-endpoint-set-auth` to inspect outbound peer health and update the host/port or enabled state of an already-provisioned remote peer without restarting the process.
- Prevents disabling the transport endpoint for an active Raft voter. The voter must first be demoted through joint consensus; only then can its transport be disabled. Endpoint management does not discover, add, or remove peer IDs from the provisioned roster.
- Replaces one outbound thread per remote peer with a fixed-size shared worker pool. It preserves FIFO ordering with one in-flight RPC per peer, bounds global/per-peer queues, coalesces or drops stale heartbeat work under pressure, and applies capped exponential retry backoff.
- Adds operator-only per-peer health counters: queue depth, attempts, successes, failures, dropped messages, consecutive failures, retry delay, timestamps, last error, endpoint, and registry generation.
- Makes `NodeServer::run()` a one-shot lifecycle operation: a stop requested before the run thread starts is never lost, and concurrent/repeated run entry is rejected.
- Keeps wire protocol v7 and the SQLite schema unchanged. The inbound server event loop and Ledger/Raft state machine remain serialized; only outbound RPC dispatch is concurrent. The pool is bounded, but this is not a general Internet-facing DoS admission-control system.

## Version 1.5.0 changes

- Adds passphrase-encrypted node identity vaults using PBKDF2-HMAC-SHA-256 and AES-256-GCM. Vault files are owner-only, use bounded KDF work factors, and retain compatibility with existing raw 0600 identity files.
- Adds signed, cluster-bound trust-policy bundles with monotonic per-node epochs. Only an already authenticated active operator can install a policy; stale/replayed epochs, wrong-cluster bundles, malformed entries, and invalid signatures are rejected.
- Preserves local revocation tombstones across policy replacement, rejects attempts to resurrect revoked identities, and refuses policies that remove every active operator.
- Adds an authenticated protocol-v7 request to push a trust policy to one already-provisioned Raft node. The node persists the new trust store before acknowledging; the source advances its local policy epoch only after the remote acknowledgement.
- Prevents a policy push from revoking a remote endpoint still present in that node's pre-provisioned Raft roster. The endpoint roster has no online removal command yet, so a provisioned node identity cannot be revoked through policy push in this release.
- Adds integration coverage for remote policy persistence across restart and rejection of a revoked identity's subsequent TCP handshake.
- Policy rollout is explicit and one target at a time; there is no automatic cluster broadcast, peer discovery, CA, or endpoint discovery. The encrypted application-frame channel is not TLS and has not received an independent cryptographic audit.

## Version 1.4.0 changes

- Upgrades authenticated peer sessions to protocol v6 with mutually signed ephemeral X25519 key agreement, HKDF-SHA-256 directional keys, and AES-256-GCM frame encryption/authentication. Ed25519 signatures and replay/order checks remain in place.
- Separates ordinary `peer` trust from `operator` trust. Only an active operator may request Raft voter-set changes.
- Adds trust revocation tombstones and local identity rotation commands: `peer-trust-revoke` and `peer-trust-rotate`.
- Loads legacy two-column trust files as ordinary peers. Existing operator identities must be explicitly promoted in each node's trust store after upgrade.
- Adds real TCP rejection coverage for revoked identities and retains the three-process crash/snapshot and four-process joint-consensus labs.
- The channel uses application-layer encryption, not TLS; IP/port/size/timing metadata remain visible. Key/trust distribution was local/manual before v1.5.0 and the protocol has not received an independent cryptographic audit.

## Version 1.3.0 changes

- Adds durable Raft membership state with canonical, checksummed configuration encoding and explicit stable, joint-pending, joint-committed, and final-pending phases.
- Implements the two-entry joint-consensus voter transition (`C_old,new` then `C_new`): every commitment during the joint phase requires a majority of both the old and new voter sets.
- Adds pre-provisioned non-voting learners. New voters must already exist in the shared authenticated endpoint/trust roster and be caught up to the leader's committed index before promotion; removed voters become learners.
- Persists current membership and snapshot-boundary membership so restart, compaction, and snapshot installation preserve the exact configuration corresponding to each log boundary.
- Adds authenticated operator CLI support for membership changes and exposes membership phase, voters, and learners in Raft status. Network protocol version is 5; all nodes and operator clients must be updated together.
- Adds four-process authenticated TCP testing: learner catch-up from a compacted snapshot, leader removal, final-configuration propagation, removed-node restart, and reverse promotion. The existing three-process crash/partition/snapshot lab remains integrated.
- Preserves the v1.2 bootstrap configuration fingerprint for deployments without learners; clusters using learners bind the full initial provisioned roster into a new fingerprint.

## Version 1.2.0 changes

- Adds Raft application snapshots and safe compaction of committed, already-applied log prefixes. The immutable bootstrap checkpoint remains separate from the moving snapshot boundary.
- Stores snapshot payloads separately from the Raft consensus-state blob in additive SQLite schema v5. A snapshot object is written and checksummed before Raft state can reference it; interrupted compaction can leave an orphan but cannot discard the authoritative log first.
- Adds bounded `InstallSnapshot` transfer: 512 KiB chunks, byte-offset acknowledgements/retries, an end-to-end SHA-256 check, and a 64 MiB maximum application snapshot.
- Installs a received snapshot by validating the complete Ledger chain and atomically replacing Ledger blocks, mempool state, Raft state, and the durable snapshot row in one SQLite transaction.
- Automatically compacts after 128 applied Raft entries by default; the threshold is configurable through `RaftConfig::snapshot_threshold_entries`.
- Adds bounded lagging-member catch-up through snapshot installation followed by the remaining AppendEntries suffix. An interrupted in-memory chunk transfer restarts from byte zero after receiver process restart.
- Retains v1 Raft-state and Raft-message deserialization compatibility; all cluster nodes must still be updated together because v1.2 emits the v2 message encoding.
- Adds tests for multi-chunk transfer, receiver restart mid-transfer, real Ledger snapshot restoration, SQLite rollback under injected install failure, schema v3-to-v5 migration, and legacy encoding reads.
- Extends the real three-process TCP chaos lab to take one node offline, commit 136 blocks on the remaining majority, confirm leader compaction at Raft index 128, then restart the lagging OS process and verify it durably installs the snapshot and catches up.

## Version 1.1.0 changes

- Adds a repeatable multi-process Raft failure lab using three independently spawned `feinstatic raft-node` processes, separate SQLite databases, independent Ed25519 identities, and bilateral trust.
- Adds per-directed-link TCP fault proxies. The lab injects a 10 ms one-time connection delay, `SIGKILL` leader termination, bidirectional minority isolation, active connection drops, and partition healing without root privileges or third-party proxy tools.
- Verifies majority election and commit after process death, durable former-leader restart/catch-up, refusal to commit from an isolated minority, continued majority progress, conflict repair, and convergence of term/commit/applied/log indexes and Ledger tip with no uncommitted suffix.
- Integrates the process lab into `scripts/verify-all.sh`; failed workspaces and per-node logs are retained for diagnosis.
- No network wire-version or SQLite schema change; existing schema v4 databases require no migration.

## Version 1.0.0 changes

- Integrates the v0.9.0 Raft state machine with the authenticated TCP peer protocol (wire version 4), including live RequestVote, VoteResponse, AppendEntries, and AppendResponse RPC delivery.
- Binds each Raft sender/recipient ID to the Ed25519 identity authenticated on the TCP session; only allow-listed cluster roster members can send consensus RPCs.
- Adds `RaftLedgerAdapter`: canonical, bounded Ledger-block commands; leader-only block proposals; follower-side block validation; ordered committed-entry application; and exact Ledger/Raft recovery validation.
- Persists Ledger block append, durable mempool reconciliation, and the Raft `last_applied` snapshot in one SQLite transaction. In-memory state is published only after durable commit.
- Disables direct block import and pull-based legacy chain synchronization on Raft-enabled members so those paths cannot bypass consensus.
- Uses one bounded FIFO outbound dispatcher per remote member so an unavailable peer does not hold up healthy peers while each peer's Raft message order remains preserved.
- Adds real loopback TCP cluster tests with three independent SQLite databases and authenticated identities: election, majority block replication, Ledger convergence, bypass rejection, restart recovery, leader failure/re-election, and former-leader catch-up.
- The suite contains **131 automated test cases** and passes GCC Debug, GCC Release, GCC ASan/UBSan, and Clang Release with `-Werror`.

Scope: Raft mode is a permissioned **crash-fault-tolerant prototype**. It is not Byzantine fault tolerant. Voter changes use joint consensus within a pre-provisioned endpoint roster (at most nine total voters plus learners). v1.5.0 adds explicit signed trust-policy pushes; v1.6.0 adds live endpoint management; v1.7.0 adds bounded inbound connection admission and automatic best-effort transaction gossip with durable-mempool recovery. Peer-ID discovery and roster expansion remain unimplemented. Inbound admission uses configurable global/per-address token buckets but is not a complete Internet-facing DDoS defense. Transaction gossip is dissemination, not consensus or finality. Snapshot payloads are full serialized Ledger histories with a 64 MiB cap; chunk assembly is in memory and restarts from byte zero after a receiver process restart. Authenticated protocol v8 encrypts application frames with ephemeral X25519, HKDF-SHA-256, and AES-256-GCM. This is not TLS and has not received an independent cryptographic audit. See [`docs/raft.md`](docs/raft.md), [`docs/networking.md`](docs/networking.md), and [`docs/raft-networking-guide.md`](docs/raft-networking-guide.md).

## Version 0.9.0 changes (historical)

- Added the transport-agnostic durable Raft state machine, fixed odd-sized membership, durable voting, leader no-op, majority replication, current-term commit, conflict repair, ordered applied-index tracking, and the additive SQLite schema v3 -> v4 migration.
- Added simulator tests for partitions, election safety, vote persistence, log repair, commit safety, checkpoint binding, and storage failures. v1.0.0 adds the live authenticated TCP and Ledger integration described above.

## Version 0.8.0 changes

- Adds a separate deterministic fork-choice policy for independently validated chains. Since every block in the current protocol has the same fixed PoW difficulty, greater height represents greater accumulated work; equal-height branches use a deterministic lexicographically smaller tip-hash tie-break.
- Adds bounded chain locators so peers exchange only the divergent suffix after a common locator match instead of downloading the entire chain.
- Changes peer synchronization from append-only mutation to candidate-first validation: the complete candidate branch is downloaded, its advertised tip is rechecked, and the full resulting chain is replay-validated before canonical state changes.
- Adds atomic SQLite chain replacement plus durable-mempool reconciliation. A database failure rolls back both canonical blocks and mempool state; memory is published only after commit.
- Reconsiders transactions from displaced blocks in deterministic order. Valid detached transactions are requeued; transactions already confirmed on the candidate chain or invalidated by candidate state are dropped.
- Protects Bridge journal references during the operational `peer-sync` command. Deposits and confirmed withdrawals cannot be orphaned or moved from their recorded block height; a pending withdrawal must remain confirmed or present in the durable mempool.
- Adds tests for longer-branch adoption, equal-height tie convergence, shorter divergent-chain rejection, mempool requeue/conflict dropping, atomic database rollback, locator-based synchronization, restart recovery, and Bridge-reference protection.

Important: this is deterministic local fork choice, not distributed consensus. The fixed-difficulty assumption is part of the current protocol. The equal-height hash tie-break is grindable and provides convergence only, not Sybil resistance, Byzantine agreement, or finality. Variable difficulty must not be introduced without replacing height-based work comparison with true cumulative work.

## Version 0.7.0 changes

- Adds independent Ed25519 node identities, stored separately from customer wallets and created as mode-0600 private identity files.
- Adds explicit peer trust allow-lists with canonical node IDs derived from public keys and bounded trust-store parsing.
- Adds mutual challenge-response authentication with fresh nonces, chain-parameter binding, and a pinned expected server identity.
- Signs each authenticated application frame over a connection-specific handshake transcript digest, message type, payload, and monotonic per-direction sequence number; tampered, replayed, or out-of-order frames are rejected, including cross-session replay.
- Restricts the legacy unauthenticated listener to loopback. Remote binds require an authenticated listener and explicit peer allow-list.
- Adds `node-keygen`, `peer-trust-add`, and authenticated node status/sync/transaction/block relay commands.
- Adds loopback integration tests for authenticated status, transaction relay, block synchronization, untrusted-client rejection, server identity pinning, plaintext-client rejection, identity-file permissions, trust-store validation, and cross-session frame replay resistance.

At v0.7.0, authenticated frames were signed, bound to fresh session nonces, and replay-protected, but **not encrypted**. v1.0.0 subsequently integrated Raft RPCs with authenticated TCP; v1.2.0 added log compaction/snapshot recovery; v1.3.0 added joint-consensus voter changes; v1.4.0 added encrypted application frames and operator roles; v1.5.0 added encrypted identity vaults and signed remote trust-policy pushes; v1.6.0 added a durable endpoint registry and bounded shared outbound worker pool. Current limitations include crash-fault-only consensus, pre-provisioned peer-ID rosters, bounded inbound I/O workers with serialized state access, no TLS interoperability, no automatic peer discovery, and no production finality. Do not expose nodes to untrusted networks.

## Two-node authenticated lab quick start

Each node needs its own database, Ed25519 node identity, and allow-list entry for the other node. Generate identities with `node-keygen`, exchange only their public ID/key, then use `peer-trust-add` to create bilateral trust files. Start a listener with `node-listen-auth`; use `peer-status-auth` and `peer-sync-auth` from the other node. See [`docs/networking.md`](docs/networking.md) for the complete workflow.

Authenticated protocol v8 encrypts application frames using ephemeral X25519, HKDF-SHA-256, and AES-256-GCM, while retaining Ed25519 identity/frame authentication. It is application-layer encryption, not TLS. Use controlled private networks only pending independent cryptographic review. See `docs/consensus.md` for reorganization rules and limitations.

## Version 0.6.0 changes

- Makes the Ledger mempool durable in SQLite. Accepted signed transactions survive process restart and retain admission order for sequential sender nonces.
- Revalidates persisted transaction IDs, signatures, replay status, nonce sequence, and balances against the recovered chain. Corrupt or invalid durable mempool state fails recovery closed.
- Bounds pending state to 10,000 transactions and 32 MiB of serialized transaction data.
- Commits accepted transactions to SQLite before publishing them to the in-memory mempool.
- Commits mined/imported blocks and the resulting mempool snapshot in one SQLite transaction. Storage failure rolls back both sides of the transition.
- Adds an additive SQLite schema v2 -> v3 migration that preserves existing Ledger and FinancialPlatform records.
- Makes network-relayed transactions survive receiver restart and updates Bridge withdrawal retry to recognize recovered pending transactions.
- Adds restart, tamper, migration, failure-injection, atomicity, and network-recovery tests.

## Version 0.5.1 changes

- Fix Boost.Asio timer cancellation compatibility with newer Boost releases that remove the deprecated `cancel(error_code&)` overload.
- Compile the networking core with `BOOST_ASIO_NO_DEPRECATED` so removed/deprecated Asio APIs are caught during normal builds.
- Preserve the v0.5.0 wire protocol and network behavior; this is a source/toolchain compatibility maintenance release.

## Version 0.5.0 changes

- Adds a bounded TCP peer protocol with genesis/consensus-parameter handshake, checksummed framing, and connection timeouts.
- Adds peer status, signed transaction relay, validated block relay, and pull-based block synchronization.
- Adds bounded TCP block-range synchronization with full candidate validation before append. (Fork choice and reorganization were added later in v0.8.0.)
- Adds atomic multi-block validation/persistence per imported batch and mempool reconciliation.
- Adds CLI commands for `node-listen`, `peer-status`, `peer-sync`, `peer-submit-tx`, and `peer-relay-tip`.
- Adds loopback TCP integration tests for transaction/block relay, 70-block synchronization, restart recovery, duplicate/replay rejection, wrong-genesis peers, divergent chains, invalid blocks, and batch atomicity.

## Version 0.4.0 changes

- Adds a native-FST Ledger ↔ Settlement bridge with a dedicated immutable vault address.
- Credits deposits only after the transfer is included in the locally validated Ledger chain; credits are idempotent by transaction ID.
- Adds signed withdrawals with aggregate collateral checks across all off-chain FST account balances and Flow pool reserves.
- Persists the customer debit and exact signed withdrawal transaction before mempool submission. Interrupted requests can be retried without re-signing; transactions mined just before a process interruption are reconciled on the next Bridge entry.
- Tracks pending withdrawal collateral encumbrances, including the network fee, and detects the chain-confirmed / snapshot-not-yet-reconciled crash window.
- Extends the financial-platform snapshot to v2 while retaining backward compatibility with v0.3.0 v1 snapshots.
- Adds automated deposit replay, unconfirmed deposit rejection, wrong-recipient rejection, unowned sender rejection, pending withdrawal recovery, chain confirmation reconciliation, vault configuration, collateral shortfall, and legacy snapshot migration tests.
- Keeps the non-interactive `verify <database.db>` command and expands it with bridge vault, liabilities, pending reserves, and collateral diagnostics.

## Current implementation status

| Area | Implementation |
|---|---|
| Ledger wallets and signed transfers | Implemented; encrypted local wallet secrets |
| TCP peer transport and framing | Implemented; bounded frames, authenticated identities, signed sequence-numbered sessions |
| Peer transaction/block relay | Implemented; allow-listed Ed25519 peers, signed frames, remote Ledger validation |
| Pull-based block synchronization | Implemented; bounded locators, divergent suffix download, candidate validation and atomic chain replacement |
| Deterministic local fork choice / reorganization | Implemented for fixed-difficulty chains; separate from distributed consensus |
| Raft consensus and Ledger replication | Crash-fault prototype with 3/5/7/9-voter configurations, joint-consensus voter changes inside a pre-provisioned roster, authenticated TCP RPCs, majority replication, snapshots and atomic Ledger application |
| Blockchain, PoW, mining and replay recovery | Implemented; single-node private chain with durable, bounded mempool |
| Match order book and matching | Implemented; deterministic price-time priority |
| Flow pools, swaps and LP accounting | Implemented; integer AMM arithmetic |
| Shared off-chain settlement | Implemented; Match and Flow use one canonical balance/reservation book |
| Match persistence | Implemented through atomic financial-platform snapshots in SQLite |
| Flow persistence | Implemented through the same atomic financial-platform snapshots in SQLite |
| Persistent trade, swap and settlement history | Implemented in the platform snapshot and balance journal |
| Ledger ↔ off-chain settlement bridge | Implemented for native FST in the private single-node chain; confirmed deposit claims, signed withdrawals, collateral checks, replay protection and crash recovery |
| Distributed consensus / finality | Operational permissioned Raft prototype with crash-fault joint-consensus membership changes; v1.1 adds process-level failure tests, v1.2 adds compaction and snapshot catch-up, and v1.3 adds authenticated TCP voter reconfiguration; no Byzantine tolerance, TLS, peer discovery, or production finality |
| Smart-contract execution / on-chain AMM | Not implemented |
| Full-screen FTXUI interface | Not implemented; current interface is a terminal menu |

## Raft process-level failure lab (v1.1.0+)

The full verification script now launches three independent FeinStatic OS processes and injects directed TCP partitions. It kills and restarts a leader, checks durable catch-up, isolates a minority leader, verifies that it cannot commit without a quorum, confirms the connected majority continues, then heals the partition and checks convergence. See [`docs/raft-process-testing.md`](docs/raft-process-testing.md).

Run the lab independently after a Release build:

```bash
python3 scripts/raft-process-chaos.py \
  --binary build/release/feinstatic \
  --prepare-helper build/release/feinstatic_raft_lab_prepare
```

## Architecture

```text
                       FeinStatic terminal / CLI
                          /                 \
                  Ledger API          NodeServer / PeerClient
                     |                 authenticated TCP v4
                  Ledger <----------> RaftLedgerAdapter
                     |                      |
                     |                 Raft state machine
                     |                 elections / log / quorum
                     |                      |
                     +-----------+----------+
                                 |
                        one member SQLite DB
             blocks + durable mempool + raft_state (atomic apply)

             BridgeService -> Ledger / SettlementBook boundary

                 +------------------------------------+
                 |           FinancialPlatform       |
                 |                                    |
                 | SettlementBook <----> Match        |
                 | shared balances       order book   |
                 | available/reserved    trades       |
                 | journal               reservations|
                 |            <-------> Flow          |
                 |                        AMM / LP    |
                 +------------------------------------+
                            |
                 revisioned financial_state snapshot
```

The three domain engines have independent public interfaces. `FinancialPlatform` coordinates the off-chain Match and Flow engines through one shared `SettlementBook`. Account identifiers can be Ledger wallet addresses. Linking an address establishes identity only; it does not move funds. The explicit Bridge workflow is the only implemented path that converts a confirmed native-FST Ledger deposit into an off-chain FST credit or requests a collateral-backed native-FST withdrawal.

For an existing database that already contains synthetic FST liabilities, see [`docs/bridge.md`](docs/bridge.md) before funding the vault: unclaimed native-FST vault funding can collateralize existing liabilities, while claiming a funding transaction adds a new off-chain credit rather than replacing a synthetic one.

### Shared settlement rules

- Every platform account is registered with the shared SettlementBook, Match, and Flow.
- Each asset balance has `total`, `reserved`, and derived `available = total - reserved` amounts.
- Match limit orders reserve the required quote or base amount. Flow swaps, liquidity actions, and internal transfers can spend only available funds; they cannot bypass Match reservations.
- Every balance change adds sequenced journal entries with operation and reference metadata.
- Match trades, resting orders, order reservations, Flow pools, LP positions, swaps, and settlement balances are persisted together.
- An operation mutates a candidate state, validates all engine/account invariants, serializes the candidate, commits one SQLite snapshot transaction, and only then publishes the candidate as the live in-memory state.
- Optimistic revision checking rejects a stale writer rather than silently overwriting a newer platform state.

### Financial units

All financial balances are unsigned integer atoms. One FST or USD is 100,000,000 atoms. No binary floating-point values control a financial state transition.

The persistent Match market is `FST/USD`: quantity is a whole number of FST lots; price is USD per whole FST, represented in quote-asset atoms. The Flow AMM uses asset atoms directly in pool reserves and swaps.

## Components

### Ledger

- OpenSSL EVP Ed25519 key generation, signatures, and verification.
- SHA-256 addresses and block/transaction hashing.
- AES-256-GCM wallet-secret encryption with PBKDF2-HMAC-SHA256 key derivation.
- Canonical transaction serialization, nonce validation, balance checks, replay protection, and mempool admission.
- Hash-linked blocks, configurable local Proof of Work, mining subsidy, transaction fees, and total-supply accounting.
- SQLite persistence, schema versioning, block validation, durable mempool admission/recovery, and replay-based restart recovery.
- Hidden interactive passphrase input using POSIX `termios` with terminal restoration on normal completion and handled signals.

### Match

- Limit and immediate-or-cancel market orders.
- Descending bid prices, ascending ask prices, FIFO within each price level.
- Price-time priority and execution at the resting maker price.
- Full/partial fills, multi-level matches, cancellation, self-trade prevention, order/trade history.
- Checked notional arithmetic and available/reserved balance validation.
- Preflight-and-commit order submission: later-level failure cannot leave earlier fills committed.
- Persistent order book, trade tape, sequence counters, and shared reservations through `FinancialPlatform`.

### Flow

- Constant-product pools with configurable fee basis points.
- Swap quotes and execution in both directions, fee accounting, price-impact estimate, and minimum-output protection.
- Initial and proportional liquidity provision, LP shares, and liquidity redemption.
- Boost.Multiprecision fixed-width 256-bit intermediate arithmetic, checked conversion to 64-bit asset amounts.
- Persistent pools, reserves, LP positions, swap history, and shared settlement balance changes through `FinancialPlatform`.

## Requirements

- GCC 12+ or Clang 15+ with C++20.
- CMake 3.25+ and Ninja.
- OpenSSL 3.x development headers/libraries.
- SQLite 3.24+ development headers/libraries.
- Boost 1.74+ headers (`Boost.Multiprecision` is header-only).

On Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja git openssl sqlite boost
```

The CMake presets use two build jobs by default, suitable for smaller machines.

## Build and verify

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
```

Release and ASan/UBSan:

```bash
cmake --preset release
cmake --build --preset release --parallel 2
ctest --preset release --output-on-failure

cmake --preset asan
cmake --build --preset asan --parallel 2
ctest --preset asan --output-on-failure
```

Run the complete verification script (GCC Debug/Release/ASan, Clang Release + Werror, tests, demo, benchmark):

```bash
./scripts/verify-all.sh
```

## Run

Synthetic demonstration:

```bash
./build/debug/feinstatic demo
```

Create a private Ledger database and initial wallet:

```bash
./build/debug/feinstatic init ./runtime/feinstatic.db
```

Open it later:

```bash
./build/debug/feinstatic open ./runtime/feinstatic.db

# Non-interactive validation after restart or after an update
./build/release/feinstatic verify ./runtime/feinstatic.db
```

The interactive application opens the normal Ledger menu. Select **7. Shared financial platform (persistent Match + Flow)** to enter the shared off-chain platform.

### First platform workflow

1. Use **Link an existing Ledger wallet** to register a wallet address as a platform account. This links identity only; it does not transfer Ledger FST into off-chain settlement.
2. Use **Add synthetic TEST asset credit** to create explicit demo balances such as FST or USD. Enter an audit reason. These funds are synthetic off-chain test balances, not Ledger issuance.
3. Open Match to place/cancel orders and inspect the order book and trade history.
4. Open Flow to create a pool, add/remove liquidity, quote swaps, and execute swaps.
5. Use **Validate shared platform state** to check settlement journal/balances, Match book/reservations/trades, Flow pool/LP invariants, and account registration consistency.
6. Optionally configure a dedicated Bridge vault wallet in the Ledger / Settlement Bridge menu. Do not link the vault as a customer settlement account. The Bridge documentation describes funding, claiming deposits, collateralization, and withdrawals.
7. Exit and reopen the same Ledger database. The off-chain platform revision, balances, orders, trades, pools, LP positions, swaps, and bridge journal should recover.

The platform currently configures a persistent `FST/USD` Match market. Flow pools can be created for any two supported asset IDs. Test credits are intentionally explicit in the CLI so synthetic funds cannot be mistaken for real deposits.

## Persistence model

SQLite schema version 5 stores the Ledger chain (`blocks`), encrypted wallets (`wallets`), shared financial snapshot (`financial_state`), durable mempool (`mempool_transactions`), revisioned Raft consensus state (`raft_state`), and checksummed application snapshots (`raft_snapshots`). The v2 -> v3 migration adds the durable mempool; v3 -> v4 adds the Raft state; v4 -> v5 adds the application-snapshot table. These migrations are additive and do not rewrite existing blocks, wallets, or financial snapshots. The embedded FinancialPlatform snapshot format is independently versioned; v0.4.0 writes snapshot version 2. The financial-state row contains a monotonically increasing revision, SHA-256 payload checksum, and versioned binary snapshot. The v2 snapshot includes SettlementBook accounts/balances/journal, Match accounts/orders/trades/counters, Flow accounts/pools/LP positions/swaps/counters, and Bridge vault/deposit/withdrawal records. v1 snapshots from v0.3.0 remain readable and are upgraded on the next platform mutation.

The Ledger mempool preserves admission order and is bounded to 10,000 transactions / 32 MiB of serialized transaction data. On restart, each row is deserialized, its transaction ID is recomputed, its signature is checked, and the pending sequence is revalidated against the recovered chain. A malformed or stale entry fails recovery rather than being silently ignored. `Ledger::submit` commits a transaction row before publishing the in-memory admission. Mining and peer block import commit block(s) and the reconciled mempool rows in the same SQLite transaction.

Every platform mutation is persisted under `BEGIN IMMEDIATE` with an expected-revision check. On success the row revision increments; a stale process receives a revision-conflict error. The v1-to-v2 migration is additive: existing Ledger block and wallet rows are left untouched. A checksum detects accidental payload corruption; it is not a keyed MAC and does not protect against an attacker who can rewrite both payload and checksum.

Ledger block append and the off-chain FinancialPlatform snapshot remain separate commits. The Bridge is a recoverable workflow, not a cross-domain atomic transaction: deposits are credited after chain confirmation and are idempotent by transaction ID; withdrawals debit the platform and persist the exact signed Ledger transaction before mempool submission, then reconcile after block inclusion. On restart, pending withdrawals can be retried or recognized as already mined. This design narrows failure windows but does not create a distributed transaction or distributed finality.

## TCP peer networking

For Raft-enabled multi-node operation, provision one independent database checkpoint and Ed25519 node identity per member, exchange only public node IDs/public keys, create bilateral trust files with `peer-trust-add`, and start each member with `raft-node`. Use `raft-status-auth` to inspect status and `raft-propose-block-auth` to request a leader proposal. The complete three-node procedure is in [`docs/raft-networking-guide.md`](docs/raft-networking-guide.md). Legacy non-Raft authenticated peer commands remain available as described in [`docs/networking.md`](docs/networking.md).

The older `node-listen`, `peer-status`, `peer-sync`, `peer-submit-tx`, and `peer-relay-tip` commands remain available for local loopback development only. The unauthenticated listener refuses non-loopback binds. Authenticated protocol v8 encrypts application frames using ephemeral X25519, HKDF-SHA-256, and AES-256-GCM while retaining Ed25519 authentication and replay protection. This is application-layer encryption, not TLS; transport metadata remains visible and the protocol has not received an external cryptographic audit. Use controlled private networks only.

Each cluster member must use its own database, and only one writable process may open that database at a time. Ledger and Raft state-machine transitions are serialized on the server event loop; bounded per-peer workers dispatch Raft RPCs and best-effort transaction gossip. Accepted transactions are disseminated to enabled pre-provisioned members, and pending durable mempool entries are re-queued after restart. Gossip is not consensus; the leader must still propose a block and Raft must commit it. Peer discovery remains manual. The Raft cluster supports joint-consensus voter changes only within its pre-provisioned endpoint/trust roster; it remains crash-fault-only, not Byzantine consensus or production finality.

## Benchmark

```bash
./build/release/feinstatic_bench 1000000
```

This synthetic benchmark measures one process and an in-memory Match order book. It does not measure the persistent FinancialPlatform coordinator, SQLite commit cost, networking, concurrency, market-data fan-out, or a production exchange. Always record build type, compiler, CPU, workload, and exact output; never generalize results across machines.

## Limitations and security boundary

- Ledger has authenticated TCP peer transport and a Raft-enabled replication mode. Non-Raft synchronization still uses deterministic local fork choice. Raft mode replicates block commands with majority commitment and atomic Ledger application, but provides crash-fault tolerance only: no Byzantine fault tolerance, dynamic membership, TLS/confidentiality, snapshot transfer/log compaction, or production finality.
- Ledger supports native test-asset transfers, not general smart contracts. Flow is an application-level AMM, not an on-chain smart contract.
- Match and Flow persist off-chain state. Their balances are not automatically backed by on-chain FST, real USD, bank deposits, or market assets. When the Bridge is configured, it reports aggregate FST collateralization; explicit synthetic test credits can create liabilities that the vault does not cover, in which case withdrawals are blocked.
- Wallet secrets are encrypted at rest. Hidden input is not a hardware wallet or production custody design. Passphrases/private keys exist in process memory while in use; selected temporary buffers are cleansed, but no complete secure-memory guarantee is made.
- SQLite WAL and synchronous FULL are enabled; schema and application state are validated on open. SHA-256 snapshot checksums are corruption checks, not authentication. This is not a formal crash-consistency proof or an independent security audit.
- The financial platform serializes a full state snapshot on each mutation; this favors atomicity and a simple local MVP over large-scale write throughput. A journal/event-sourcing or normalized-table design would be a future scaling step.
- The Bridge is native-FST-only and uses one local vault wallet. It supports one pending withdrawal at a time, requires the vault to cover all customer FST liabilities plus pending signed payouts/network fees, and does not provide cancellation of a signed pending transaction.
- The legacy non-Raft synchronization service still uses deterministic local fork choice; Raft-enabled members use the integrated authenticated RPC/majority-commit path. Raft voter changes use joint consensus within a pre-provisioned non-Byzantine crash-fault-only roster. Authenticated peer application frames are encrypted under protocol v8; this is not TLS and has not received independent cryptographic review. There is no production financial finality, production custody, external auditor, payment rail, real USD, or real customer funds.
- The UI is line-oriented; no FTXUI full-screen interface, public network API, or broker connection is supported.

## Documentation

- [`docs/architecture.md`](docs/architecture.md)
- [`docs/bridge.md`](docs/bridge.md)
- [`docs/blockchain.md`](docs/blockchain.md)
- [`docs/exchange.md`](docs/exchange.md)
- [`docs/dex.md`](docs/dex.md)
- [`docs/security.md`](docs/security.md)
- [`docs/bridge.md`](docs/bridge.md)
- [`docs/networking.md`](docs/networking.md)
- [`docs/raft.md`](docs/raft.md)
- [`docs/raft-networking-guide.md`](docs/raft-networking-guide.md)
- [`docs/consensus.md`](docs/consensus.md)
- [`docs/testing.md`](docs/testing.md)
- [`docs/verification-report.md`](docs/verification-report.md)
- [`docs/benchmark-results.md`](docs/benchmark-results.md)
- [`docs/verification-report-v0.3.0.md`](docs/verification-report-v0.3.0.md) (historical)
- [`docs/benchmark-results-v0.3.0.md`](docs/benchmark-results-v0.3.0.md) (historical)

## Publish to GitHub

The source update archive excludes `.git`, `build/`, and `runtime/`. Your existing checkout's Git history and local data are preserved by the Arch update helper. Configure your Git identity, commit, add your remote, and push manually:

```bash
git status
git add .
git commit -m "FeinStatic 1.0.0: authenticated Raft Ledger replication"
git remote add origin <YOUR_REPOSITORY_URL>
git push -u origin main
```

Do not run `git init` again if the checkout already has `.git`.
