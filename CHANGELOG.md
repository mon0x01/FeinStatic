## 2.0.0

- Persist each incoming Raft snapshot chunk to SQLite with `synchronous=FULL` before acknowledging its byte offset.
- Resume an acknowledged snapshot prefix after receiver restart, including when the sender process restarts and requests offset zero.
- Recover completed-but-not-yet-installed snapshots on receiver startup; retain the full staged payload if atomic Ledger/Raft installation fails, and clear it only after successful installation.
- Add SQLite schema v5 -> v6 staging tables without changing the authenticated wire protocol v8 or rewriting existing Ledger/financial state.
- Add restart, offset negotiation, completed-stage recovery, checksum, storage-bound, and direct v5 -> v6 additive-migration regression coverage.
- Retain full-history snapshot serialization and its 64 MiB cap; this release makes transfer progress durable but does not introduce state-only or unbounded snapshots.

## 1.9.0

- Add a dedicated peer lifecycle state machine with `UNKNOWN`, `HEALTHY`, `DEGRADED`, `OFFLINE`, `HALF_OPEN`, and `DISABLED` states.
- Centralize retry gating across Raft RPC and transaction gossip so both traffic classes respect the same per-peer circuit breaker.
- Add capped exponential backoff from 250 ms to 30 seconds with stable peer-specific jitter; an expired circuit permits one half-open attempt because the dispatcher already enforces one in-flight operation per peer.
- Record per-peer success/failure timestamps and smoothed operation latency; render lifecycle state in the operator-only peer health CLI.
- Add deterministic unit tests for state transitions, backoff bounds/jitter, half-open recovery, disable/enable, and invalid configuration, plus an authenticated TCP retry integration assertion.
- Keep protocol v8 and the SQLite schema unchanged. Existing endpoint/trust configuration and cluster membership semantics remain compatible.

## 1.8.0

- Add a fixed-size inbound socket worker pool (default four, maximum sixteen) and bounded pending-connection queue (default 64, maximum 4096).
- Keep the accept loop non-blocking with respect to peer handshakes and request reads; each worker owns its Asio execution context and processes one accepted connection at a time.
- Serialize all Ledger, Raft, trust-policy, and endpoint state access under a per-node mutex. Construct response frames while protected, then release the mutex before network writes so slow readers do not hold the state lock.
- Fail closed on inbound queue saturation by closing newly accepted sockets. Existing global/per-address token-bucket admission is applied before queueing.
- Add integration coverage for concurrent stalled handshakes, twelve parallel status clients, bounded queue saturation/recovery, and lifecycle regression.
- Keep authenticated protocol v8 and SQLite schema unchanged.

## 1.7.0

- Upgrade the authenticated peer wire protocol to v8 so all peers explicitly negotiate the new networking behavior together; no SQLite schema migration is introduced.
- Add bounded inbound token-bucket admission before handshake parsing, with global and per-address refill/burst limits, bounded tracked-address state, and idle bucket expiry.
- Add automatic transaction gossip across the configured Raft member/learner endpoint roster. A newly admitted transaction is forwarded to other enabled peers except its source; duplicates are treated as successful delivery.
- Rebuild transaction gossip after node restart from the durable pending mempool, so volatile outbound queues are not the only record of pending dissemination. Periodically refill bounded gossip queues from the durable mempool to handle queue backpressure.
- Unify outbound dispatch with separate Raft and transaction-gossip queues, reserved gossip capacity, consensus-priority scheduling with bounded fairness, one in-flight message per peer, and independent retry backoff.
- Requeue failed outbound messages at the head of their original per-peer queue while retaining in-flight capacity accounting; do not silently lose retryable failures. Permanent transaction rejections are terminal and are not retried forever.
- Add real authenticated TCP coverage for inbound per-address rate limiting/refill and three-member transaction gossip, including offline receivers and origin restart with 130 durable pending transactions.
- Keep the inbound event loop and Ledger/Raft state mutation single-writer and serialized. Admission control is resource bounding, not a complete Internet-facing DDoS defense.

## 1.6.1

- Fixes a timing race in the four-process membership verification lab: applied-index convergence is not treated as proof that the leader has already processed the corresponding AppendResponse and advanced its replication `match_index`.
- Selects the currently elected leader immediately before proposing the membership transition, rather than reusing the node that happened to lead at initial startup.
- Retries only the Raft core's explicit pre-append "new voter is not caught up" rejection with a bounded deadline. The consensus safety guard remains unchanged; a lagging learner is never promoted.
- No wire-protocol, SQLite schema, or persistent-state format change.

## 1.6.0

- Add a per-node versioned/checksummed endpoint registry bound to the configured cluster ID and exact pre-provisioned peer roster; registry files are owner-only, atomically replaced, and fail closed on invalid permissions, owner, checksum, cluster binding, generation, or roster.
- Add authenticated operator RPC/CLI for live endpoint host/port updates and enabled/disabled state on already-provisioned remote peers, plus operator-only peer health reports.
- Refuse to disable an active Raft voter transport endpoint; a voter must first be demoted through joint consensus. Endpoint management does not add/remove peer IDs or perform discovery.
- Replace one outbound thread per remote peer with a fixed-size shared worker pool, per-peer FIFO/one-in-flight ordering, bounded global/per-peer queues, heartbeat coalescing/drop policy, and capped exponential retry backoff.
- Add endpoint persistence/restart, file-mode, corruption fail-closed, operator authorization, active-voter disable guard, operator-only health, endpoint persistence, and voter-disable safety coverage.
- Fix a NodeServer shutdown race by introducing a one-shot lifecycle state machine; stop-before-run requests are retained, and concurrent run entry is rejected. Add regression coverage for both cases.
- Keep protocol v7 and the SQLite schema unchanged; inbound request handling and Raft/Ledger state transitions remain serialized.

## 1.5.0

- Add owner-only passphrase-encrypted Ed25519 node identity vaults using PBKDF2-HMAC-SHA-256 and AES-256-GCM, while preserving legacy raw 0600 identity loading.
- Upgrade authenticated peer protocol from v6 to v7 for signed trust-policy push requests over encrypted sessions.
- Add cluster-bound, Ed25519-signed full-state trust policies with monotonic epochs, replay rejection, signature checks, role validation, and persistent revocation tombstones.
- Require the authenticated signer to be an active operator already trusted by the receiving node; retain at least one active operator after policy installation.
- Persist remote trust-policy replacement atomically before acknowledging the RPC, and retain per-node trust-store state across restart.
- Refuse trust-policy updates that would revoke any remote endpoint still present in the node's pre-provisioned Raft roster; membership removal and trust revocation remain separate ordered actions.
- Add tests for vault permissions/passphrase/tampering, policy cluster binding and anti-replay, tombstone preservation, live authenticated policy push, restart persistence, and post-revocation TCP handshake rejection.
- Keep the SQLite schema unchanged; trust-store text format adds policy metadata. Policy rollout is explicit and target-by-target, not automatic cluster broadcast.

## 1.4.0

- Upgrade authenticated peer wire protocol to v6 with mutually signed ephemeral X25519 key exchange, HKDF-SHA-256 directional session keys, and AES-256-GCM authenticated encryption for application frames.
- Bind encrypted frames to the handshake session, message type, and strict per-direction sequence using AEAD associated data; retain Ed25519 frame signatures and replay/order validation.
- Add explicit `peer` and `operator` trust roles; Raft voter-set changes now require an active operator identity rather than any authenticated peer.
- Add persisted trust revocation tombstones and local identity rotation that carries the former role to the replacement identity.
- Preserve v1.3 two-column trust-file compatibility by loading legacy entries as ordinary peers; operator privileges must be explicitly re-provisioned.
- Add X25519/HKDF/AES-GCM crypto tests, trust lifecycle tests, revoked-identity TCP handshake rejection, and operator-only membership RPC tests.
- Keep endpoint/key distribution pre-provisioned and local; revocation/rotation must be applied to each node's trust store. The encrypted channel is application-layer protection, not TLS, and has not received an independent cryptographic audit.

## 1.3.0

- Add durable checksummed Raft membership state with stable, joint-pending, joint-committed, and final-pending transition phases.
- Implement the two-entry `C_old,new` / `C_new` joint-consensus protocol; both old and new voter majorities are required while joint.
- Add authenticated non-voting learners, catch-up-before-promotion gating, and demotion of removed voters to learners.
- Persist current and snapshot-boundary membership so compaction, snapshot restore, and restart retain the matching configuration.
- Add authenticated membership-administration RPC and CLI status/change commands; increment TCP protocol version to 5.
- Add a four-process authenticated TCP lab covering learner catch-up from a compacted snapshot, leader removal, removed-node restart, re-promotion, and final convergence.
- Keep the roster pre-provisioned (maximum nine total voters and learners); no peer discovery, TLS, Byzantine fault tolerance, or separate operator-role policy is provided.

# Changelog

## 1.2.0

- Add a moving Raft snapshot boundary separate from the immutable bootstrap checkpoint; compact only committed and applied log prefixes.
- Add checksummed application snapshots in SQLite schema v5, written before the Raft state references the compacted boundary.
- Add InstallSnapshot RPCs with 512 KiB chunks, offset acknowledgements/retries, SHA-256 validation, and a 64 MiB payload cap.
- Atomically install a full validated Ledger history, mempool state, Raft state, and snapshot row in one SQLite transaction; resume log replication after the snapshot boundary.
- Automatically compact after a configurable default of 128 applied Raft entries.
- Preserve deserialization of v1 Raft state and message encodings; new cluster RPCs use message encoding v2.
- Test receiver restart during transfer, ledger restoration and restart, injected SQLite rollback, v3-to-v5 migration, and legacy serialization compatibility.
- Extend the real three-process TCP chaos lab: take one process offline, force compaction on the two-node majority, restart the lagging process, and verify durable snapshot installation plus Ledger convergence.

## 1.1.0

- Add a multi-process Raft chaos lab that launches three independent `feinstatic raft-node` OS processes, each with its own SQLite database, Ed25519 identity, trust store, and TCP listener.
- Add directed TCP fault proxies for deterministic bidirectional node isolation and partition healing without root privileges or external proxy dependencies.
- Exercise real process `SIGKILL`, majority leader replacement, durable restart/catch-up, minority no-quorum commit prevention, majority progress during isolation, and conflict repair after healing.
- Preserve per-node logs and failed workspaces for diagnosis; generate synthetic wallets, identities, trust files, and databases in an isolated temporary workspace.
- Keep the existing wire protocol and SQLite schema unchanged; no user database migration is required.

## 1.0.0

- Integrate the durable Raft kernel with the authenticated TCP peer protocol (wire version 4).
- Bind every incoming Raft RPC sender to the authenticated Ed25519 peer identity and require a configured cluster-member relationship.
- Add the RaftLedgerAdapter with a canonical versioned Ledger-block command format, leader-only block proposals, follower validation, ordered committed application, and recovered Ledger/log consistency checks.
- Persist Ledger block append, durable mempool reconciliation, and Raft applied-index snapshot in one SQLite transaction.
- Disable direct block import and legacy chain synchronization for Raft-enabled members to prevent consensus bypass.
- Add bounded per-member outbound RPC queues and isolated FIFO dispatcher workers.
- Add real authenticated loopback TCP cluster tests for three independent databases, quorum replication, Ledger convergence, restart recovery, leader failure/re-election, and former-leader catch-up.
- Retain SQLite schema v4; no v1.0.0 database migration is required.

## 0.9.0

- Add a reusable Raft consensus state machine for fixed permissioned crash-fault clusters (3/5/7/9 nodes).
- Add elections, durable per-term votes, leader no-op entries, AppendEntries log replication, majority commit, current-term commit rule, conflict repair, and ordered apply tracking.
- Bind consensus snapshots to cluster membership and exact Ledger checkpoint metadata.
- Add checksummed revisioned SQLite Raft snapshots and additive schema v3 -> v4 migration.
- Add deterministic three-node simulation tests for partitions, recovery, vote persistence, stale-candidate rejection, log conflict repair, and storage failures.
- Preflight durable log/snapshot bounds before publishing proposal, leader no-op, or follower AppendEntries changes.
- Make AppendEntries validation all-or-nothing; malformed later entries cannot partially mutate an in-memory log.
- Enforce aggregate message-size limits even when RaftNode::receive is called directly rather than through the TCP decoder.
- Use overflow-safe election/heartbeat timer arithmetic and saturating elapsed-time accumulation.

## 0.8.0

- Add deterministic fixed-difficulty fork choice and candidate-first chain reorganization.
- Add bounded block locators and divergent-suffix synchronization.
- Atomically replace canonical blocks and reconcile the durable mempool in SQLite.
- Requeue valid transactions from displaced blocks and drop transactions invalidated by the selected branch.
- Protect Bridge journal transaction IDs and pinned confirmation heights during operational peer synchronization.
- Add rollback, restart, fork convergence, requeue/conflict, and Bridge-reference regression tests.
- Upgrade network wire/protocol version to 3 for locator-based synchronization semantics.

## 0.7.0

- Add Ed25519 node identities, explicit trust stores, mutual challenge-response, and session-bound signed frames.
- Add authenticated TCP status, transaction relay, block relay, and synchronization.

## 0.6.0

- Add durable SQLite mempool persistence, restart validation, bounds, and atomic block/mempool reconciliation.

## 0.5.1

- Fix Boost.Asio timer cancellation compatibility and compile with BOOST_ASIO_NO_DEPRECATED.

## 0.5.0

- Add bounded TCP peer protocol, transaction/block relay, and append-only synchronization.

## 0.4.0

- Add native-FST Ledger / Settlement bridge with confirmation-gated deposits and collateral-backed withdrawals.
