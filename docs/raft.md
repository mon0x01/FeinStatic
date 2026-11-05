# Raft consensus, Ledger replication, snapshots, and joint membership (v2.0.0)

## Scope

v1.0.0 connects the durable Raft state machine introduced in v0.9.0 to the authenticated TCP peer runtime and the native Ledger state machine. A permissioned cluster with a pre-provisioned voter/learner endpoint roster can elect a leader, safely reconfigure its voters with joint consensus, replicate canonical Ledger-block commands, commit them with the required majorities, and apply committed blocks to each member's independent SQLite database.

This is an operational **crash-fault-tolerant prototype**, not a Byzantine-fault-tolerant network or production payment system. v1.2.0 adds applied-prefix compaction, bounded snapshot transfer, and atomic Ledger restore. v1.3.0 adds joint-consensus voter reconfiguration within a pre-provisioned endpoint roster. v1.4.0 encrypts authenticated application frames and restricts membership administration to local trust-store operator roles. v1.5.0 adds encrypted node-identity vaults and explicit signed trust-policy pushes; v1.6.0 adds live endpoint management within the pre-provisioned roster and a bounded shared outbound worker pool. v2.0.0 adds durable, restart-resumable incoming snapshot staging in additive SQLite schema v6 while preserving wire protocol v8. TLS interoperability, peer-ID discovery/roster expansion, cluster-wide policy broadcast, external finality, and production key custody remain out of scope.

## Cluster model and bootstrap invariants

- Stable voter membership is odd-sized: 3, 5, 7, or 9 nodes. A majority is `floor(N / 2) + 1`. The total pre-provisioned roster (voters plus learners) cannot exceed nine.
- Every provisioned peer uses one distinct Ed25519 node identity, one distinct SQLite database when running as a node, and the same `cluster_id` and sorted voter/learner roster.
- Every member must start from the exact same already-validated Ledger checkpoint. The persisted Raft state binds cluster ID, membership fingerprint, checkpoint height, and checkpoint hash.
- The local Raft node ID must equal the authenticated Ed25519 peer ID. Every remote roster entry must have exactly one configured endpoint and an entry in the node's trust store.
- Learners replicate log entries and snapshots but cannot vote, campaign, or lead. A learner must reach the leader's committed index before it can be promoted.
- Voter changes use `C_old,new` followed by `C_new`; during both transitions, a decision requires a majority of each configuration. Removed voters become learners.
- Peer IDs and voter/learner membership remain pre-provisioned. A newly introduced peer must be added to every node's startup roster and trust policy before the membership transition. v1.6.0 can update the address and enabled state of an existing remote peer at runtime, persisting the result in a cluster-bound endpoint registry; it cannot add or remove peer IDs. A remote endpoint cannot be disabled while that peer is an active voter; first demote it through joint consensus.
- The failure model is crash/recovery with non-Byzantine members. A compromised or malicious trusted member is outside the safety model.

Each member owns a separate database. Never start two writable FeinStatic processes against the same database file. Prepare all member databases from one closed, consistent source database before starting any Raft member. Do not copy a live WAL database with running writers.

## Runtime message flow

`NodeServer` binds Raft to the existing authenticated, signed TCP framing protocol (wire protocol version 8):

1. A peer connection performs mutual Ed25519 challenge-response while signing fresh ephemeral X25519 keys into the transcript; HKDF derives directional AES-256-GCM keys for subsequent frames.
2. The Raft handler verifies that the authenticated connection's peer ID is in the pre-provisioned voter/learner roster, `RaftMessage::from` equals that authenticated ID, and `RaftMessage::to` equals the local node ID.
3. The Raft state machine processes the message on the node's serialized server event loop. It does not run concurrently with Ledger mutation.
4. Generated responses are placed into bounded global and per-peer outbound queues. A fixed-size shared worker pool schedules one in-flight request per peer, preserving per-peer FIFO ordering without creating a thread per member. Heartbeats may be coalesced/dropped under pressure; failures use the per-peer circuit breaker with capped exponential retry backoff and peer-specific jitter, while Raft retransmission/election behavior supplies protocol-level recovery.
5. The main server loop drives `tick`, inbound `receive`, proposals, and committed application. `RaftNode` itself remains single-thread-affine and is never shared concurrently.

The outbound queues are bounded by global message count/bytes and per-peer message count. Under pressure, stale heartbeat work is coalesced or dropped before essential queued work; health counters report drops and failures. Retry delay grows exponentially, is jittered per peer, and is capped; Raft and gossip share one retry gate. The operator health report classifies each peer as UNKNOWN, HEALTHY, DEGRADED, OFFLINE, HALF_OPEN, or DISABLED. The v1.8.0 accept loop feeds a bounded inbound worker pool for handshake/frame I/O. All Raft/Ledger state-machine operations remain serialized behind one per-node state mutex; response writes occur after unlocking. The queue and token buckets bound local work but are not an Internet-facing admission-control system.

## Ledger command and application model

The Raft log stores opaque command bytes at the consensus layer. The v1.0.0 `RaftLedgerAdapter` defines the application command:

- Empty command: an internal Raft no-op; advances the applied index without changing Ledger height.
- Non-empty command: canonical serialized Ledger block wrapped in the versioned `FSBC` command envelope. The decoder rejects unknown versions, non-canonical encodings, trailing bytes, and oversized payloads.

Only a ready Raft leader can create a block proposal. It builds a block from its current Ledger tip and local durable mempool, validates the block locally, and proposes the encoded block command. It may not create a second block command while a previous block command remains unapplied.

Followers validate each replicated block against their own current Ledger state before acknowledging successful append. A committed entry is applied in index order. Ledger blocks, resulting durable-mempool reconciliation, and the Raft snapshot's `last_applied` advancement are written in **one SQLite transaction**. The in-memory Ledger and Raft state are published only after the transaction succeeds. A storage failure fails the node closed rather than allowing it to continue with divergent in-memory state.

On startup, the adapter checks that the persisted Raft applied history exactly matches the recovered Ledger suffix after the configured checkpoint. It rejects a Ledger that is behind the checkpoint, has a different checkpoint, contains blocks not accounted for by the applied Raft log, or differs from the blocks in that log. No-op entries consume Raft indices but do not increase Ledger height.

In Raft mode, legacy direct block submission and pull-based chain synchronization are disabled. This prevents a peer from mutating the canonical Ledger outside the replicated Raft log. Transactions can be submitted to any authenticated member's durable mempool and are automatically gossiped best-effort to enabled pre-provisioned members/learners. This is admission and dissemination, not commitment; the leader must still propose the transaction in a block and Raft must commit/apply that block.

## CLI

```text
feinstatic raft-node db address port identity.key trust.txt cluster-id members.txt
feinstatic raft-status-auth db host port identity.key trust.txt expected-peer-id
feinstatic raft-propose-block-auth db host port identity.key trust.txt expected-peer-id miner-address
feinstatic raft-change-members-auth db host port identity.key trust.txt expected-peer-id voter-id-1,voter-id-2,voter-id-3
feinstatic raft-trust-policy-push db host port operator.vault management-trust.txt expected-peer-id cluster-id epoch
feinstatic raft-peer-health-auth db host port identity.key trust.txt expected-peer-id
feinstatic raft-peer-endpoint-set-auth db host port identity.key trust.txt expected-peer-id target-peer-id endpoint-host endpoint-port enabled|disabled
```

The `members.txt` file contains one line per member, including the local member:

```text
<peer-id-a> 127.0.0.1 7431 voter
<peer-id-b> 127.0.0.1 7432 voter
<peer-id-c> 127.0.0.1 7433 voter
<peer-id-d> 127.0.0.1 7434 learner
```

Every node receives the same pre-provisioned roster and cluster ID. The voter count must be odd (3, 5, 7, or 9); voters plus learners cannot exceed nine. The fourth column is optional and defaults to `voter`. On first startup, each node creates `<database>.endpoints` from the remote member rows. This registry is owner-only, checksummed, cluster-bound, and bound to the exact configured peer-ID roster; later host/port and enabled-state changes are persisted there atomically. A corrupt or mismatched registry prevents startup rather than silently reverting to the members file. A learner is fully authenticated and replicated but cannot vote, campaign, or become leader. The local identity's port must match the listener port passed to `raft-node`. Each node's trust file must include every other provisioned member's public key. A separately trusted identity assigned the `operator` role may call `raft-change-members-auth`; the target server's trust file must contain that operator public key with the `operator` role, and the operator trust file must contain the target node's public key as a `peer`. See [`raft-networking-guide.md`](raft-networking-guide.md) for the loopback procedure.

Use `peer-submit-tx-auth` to admit a signed transaction into the leader's mempool, then `raft-propose-block-auth` to ask that leader to propose the next block. The proposal response means the leader accepted the proposal into its log; it does not mean the proposal has committed. Query status until `commit_index` and `last_applied` reach the returned log index on a majority/all intended members. For membership changes, use a separately trusted operator identity with `raft-change-members-auth`; the server's trust store is the authorization boundary. Status reports the phase and exact old/new voter sets. The wire protocol is version 8; v1.6.0 endpoint-management and health messages are additions within that protocol. All authenticated nodes and operator clients that use these new commands must run v1.6.0 together. Membership change and trust-policy requests are accepted only from identities assigned the `operator` role in the target node's current trust store. The trust-policy push file is the complete desired trust set for the target (including its other Raft peers and at least one active operator); it must also contain the target's public key so the client can pin the remote server. Use a strictly increasing positive per-source policy epoch. Push separately to each intended member. The target persists the policy before acknowledging and rejects any update that would remove a remote endpoint still in its pre-provisioned Raft roster. A revocation affects new handshakes; established sessions remain until closed.

## Persistent state and compaction

SQLite schema v4 stores the revisioned, checksummed `raft_state` row. Schema v5 adds `raft_snapshots`, which stores separately checksummed application snapshots keyed by the Raft index they include. Schema v6 adds `raft_snapshot_staging` and `raft_snapshot_staging_chunks` for one in-progress incoming transfer. Each chunk and the new expected offset are committed before its acknowledgement. These migrations are additive; v1.3.0 membership metadata remains embedded in the checksummed Raft state row.

The Raft state records two distinct boundaries:

- `checkpoint_height` / `checkpoint_hash`: immutable bootstrap Ledger checkpoint used to bind the cluster.
- `snapshot_index` / `snapshot_term`: the latest compacted Raft prefix. The retained log begins at `snapshot_index + 1`.

A local compaction captures the complete validated Ledger history plus the included Raft index, writes the snapshot object first, and only then persists the new Raft boundary and truncated log. A crash between those operations leaves an unreachable snapshot object while the old Raft log remains authoritative. The default automatic threshold is 128 applied entries and can be changed through `RaftConfig::snapshot_threshold_entries`.

The InstallSnapshot protocol sends at most 512 KiB per RPC and caps the complete application snapshot at 64 MiB. In v2.0.0, each accepted chunk is written to durable SQLite staging in its own transaction with `synchronous=FULL` before the receiver acknowledges the next byte offset. The staging metadata binds the authenticated source peer, snapshot index and term, total size, SHA-256 checksum, and snapshot-boundary membership bytes. Chunks are stored at exact offsets; gaps, overlaps, changed metadata, and out-of-bounds payloads are rejected.

After receiver restart, the receiver loads and validates the contiguous durable prefix. A sender that restarts its in-memory offset at zero receives the receiver's durable expected offset and can continue without retransmitting the prefix. If the receiver crashes after the final chunk is committed but before application installation, startup verifies the complete staged payload and retries installation. Ledger rows, durable mempool replacement, Raft state, and the completed snapshot record are installed through the existing atomic snapshot-install transaction. The staging rows are cleared only after that transaction succeeds. If installation fails, the node fails closed and retains the full staged payload for the next restart. The wire protocol remains version 8; the chunk message format is unchanged.

Snapshot installation validates the full candidate Ledger by replaying all blocks against the local chain parameters and checking the common genesis. One SQLite `BEGIN IMMEDIATE` transaction replaces Ledger block rows, clears/reconciles the mempool, stores the new Raft state, inserts the snapshot record, and prunes older snapshots. In-memory Ledger/Raft state is published only after the transaction succeeds. The leader then resumes ordinary AppendEntries from the snapshot boundary to catch up any later committed log suffix.

The per-entry apply transaction continues to write appended Ledger block rows, the reconciled durable mempool, and `last_applied` together. These transaction boundaries narrow crash windows; they do not constitute a formal proof against every filesystem, kernel, controller, or hardware power-loss mode.

## Validation coverage

The automated suite includes real loopback TCP multi-process tests using independent SQLite databases and separately authenticated node identities. A separate four-process membership lab provisions three voters and one learner, proves the learner must catch up before promotion, removes the current leader through joint consensus, restarts it as a learner, and re-promotes it in a second transition. The process lab verifies initial election, leader no-op commitment, block proposal, majority replication, leader `SIGKILL`, replacement election, former-leader restart/catch-up, minority partition prevention, majority progress, healing, and convergence. It then stops one real node process, commits 136 additional blocks on the remaining two-node majority, verifies the leader has a durable compacted snapshot at Raft index 128, restarts the lagging process, and verifies that it durably installs the snapshot over authenticated TCP before converging on the final Ledger height.

The in-process snapshot tests additionally cover multi-chunk transfer, durable receiver restart between chunks, sender restart/offset renegotiation, recovery after the final durable chunk but before installation, legacy v1 encoding reads, automatic compaction, and injected SQLite failure proving Ledger plus Raft snapshot installation rolls back atomically.

## Explicit limitations

- Crash faults only; no Byzantine fault tolerance, malicious-member safety, or Sybil resistance.
- Dynamic voter changes use the two-entry joint-consensus protocol (`C_old,new` then `C_new`). Both old and new majorities are required while joint. A newly promoted voter must already be pre-provisioned as a learner and caught up to the leader's committed index. Removed voters become learners, preserving authenticated replication and allowing later re-promotion. Peer IDs and trust identities are not discovered automatically: the full maximum-nine-node roster must be provisioned before startup. v1.6.0 lets an operator update host/port/enabled state for an existing remote peer on each node separately. Joint transitions are not Byzantine fault tolerant.
- Snapshot serialization currently includes the complete Ledger block history rather than a compact state-only image. The 64 MiB snapshot limit therefore bounds practical chain growth; larger chains need a state-only or segmented snapshot format.
- Snapshot staging is durable, but the receiver currently reloads and assembles the full payload in memory for checksum verification and Ledger replay. The complete transfer remains bounded at 64 MiB. State-only snapshots, segmented Ledger state, and larger-than-64-MiB transfers are not implemented.
- Snapshot installation validates and replays the complete Ledger history, so restore cost grows with chain history.
- Authenticated protocol v8 encrypts peer application frames and supports signed trust-policy push requests with X25519/HKDF-SHA-256/AES-256-GCM and retains Ed25519 signatures and replay protection. This is not TLS; transport metadata remains visible and the channel has not received an external cryptographic audit.
- No peer-ID discovery or NAT traversal. v1.7 adds bounded token-bucket inbound admission and automatic best-effort transaction gossip, but does not provide a complete DDoS-resistant admission policy. Endpoint changes are explicit and local to each node. Trust identity rotation remains an operator-managed trust-policy operation.
- Membership administration, endpoint health, and endpoint updates require an authenticated identity assigned the `operator` role in the target node's trust store. Ordinary `peer` identities cannot administer the cluster.
- The cluster is not externally audited and does not provide production financial finality, custody, or real-funds safety.
