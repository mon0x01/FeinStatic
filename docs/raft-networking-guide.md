# Three-node Raft loopback lab (v1.9.0)

This procedure demonstrates the actual Raft-over-authenticated-TCP runtime on one machine. It is a development lab, not a public-network deployment guide.

## 1. Create one common Ledger checkpoint

Initialize a new base database and exit the application cleanly after initialization. Use a database that contains the wallet(s) you intend to use for test transfers and block rewards.

```bash
build/release/feinstatic init runtime/raft-base.db
```

The command prompts for a wallet passphrase, creates the initial Ledger, and enters the terminal menu. Exit through the menu so the SQLite connection closes cleanly. Copy it with SQLite's backup command to create three independent, identical databases:

```bash
sqlite3 runtime/raft-base.db ".backup 'runtime/raft-a.db'"
sqlite3 runtime/raft-base.db ".backup 'runtime/raft-b.db'"
sqlite3 runtime/raft-base.db ".backup 'runtime/raft-c.db'"
```

Do not share one writable database between nodes. Do not copy a live database file while a process has it open. All three databases must begin with the same Ledger checkpoint, wallets, chain parameters, and financial-platform snapshot.

## 2. Generate node identities

For new nodes, use encrypted identity vaults; each command prompts for a passphrase without echoing it:

```bash
build/release/feinstatic node-keygen-vault runtime/raft-a.vault
build/release/feinstatic node-keygen-vault runtime/raft-b.vault
build/release/feinstatic node-keygen-vault runtime/raft-c.vault
```

Existing raw 0600 identity files remain compatible. The node commands detect either format, but encrypted vaults require an interactive terminal to prompt for the passphrase.

Record each printed `peer ID` and public key. Distribute only the public node ID and public key. Never copy the private `*.identity` files between nodes or share them with other operators.

## 3. Configure bilateral member trust

Each node must trust the other two members. Substitute the exact IDs and public-key hex strings printed by `node-keygen`:

```bash
build/release/feinstatic peer-trust-add runtime/raft-a-trust.txt <NODE_B_ID> <NODE_B_PUBLIC_KEY_HEX>
build/release/feinstatic peer-trust-add runtime/raft-a-trust.txt <NODE_C_ID> <NODE_C_PUBLIC_KEY_HEX>

build/release/feinstatic peer-trust-add runtime/raft-b-trust.txt <NODE_A_ID> <NODE_A_PUBLIC_KEY_HEX>
build/release/feinstatic peer-trust-add runtime/raft-b-trust.txt <NODE_C_ID> <NODE_C_PUBLIC_KEY_HEX>

build/release/feinstatic peer-trust-add runtime/raft-c-trust.txt <NODE_A_ID> <NODE_A_PUBLIC_KEY_HEX>
build/release/feinstatic peer-trust-add runtime/raft-c-trust.txt <NODE_B_ID> <NODE_B_PUBLIC_KEY_HEX>
```

For a membership-administrator identity, generate a dedicated fourth identity. Add it to each member's local trust store with the `operator` role. Existing two-column trust stores load as ordinary `peer` entries; promote the already-present operator record by running the command below on each node (it updates the role atomically):

```bash
build/release/feinstatic peer-trust-add runtime/raft-a-trust.txt <OPERATOR_ID> <OPERATOR_PUBLIC_KEY_HEX> operator
build/release/feinstatic peer-trust-add runtime/raft-b-trust.txt <OPERATOR_ID> <OPERATOR_PUBLIC_KEY_HEX> operator
build/release/feinstatic peer-trust-add runtime/raft-c-trust.txt <OPERATOR_ID> <OPERATOR_PUBLIC_KEY_HEX> operator
```

The operator's own trust file must contain each target node's public key as an ordinary peer. Read-only observers should remain `peer`, not `operator`.

## 4. Define the pre-provisioned membership roster

Create `runtime/raft-members.txt` with the three member IDs and their reachable endpoints:

```text
<NODE_A_ID> 127.0.0.1 7431 voter
<NODE_B_ID> 127.0.0.1 7432 voter
<NODE_C_ID> 127.0.0.1 7433 voter
<NODE_D_ID> 127.0.0.1 7434 learner
```

Use the same complete file and same cluster ID on all nodes. The optional fourth column is `voter` or `learner` (default `voter`). The three-node quick-start can omit a learner row; to prepare for online replacement, add at least one learner and provision its endpoint, identity, and public key in every member's trust store before starting the cluster. The local identity's port in the file must match the `raft-node` listener port. Every node's trust file must include all remote roster members.

## 5. Start the members

Start each member in a separate terminal. Use a unique database, identity, trust file, endpoint port, and process per node:

```bash
build/release/feinstatic raft-node runtime/raft-a.db 127.0.0.1 7431 \
  runtime/raft-a.vault runtime/raft-a-trust.txt lab-cluster-01 runtime/raft-members.txt
```

```bash
build/release/feinstatic raft-node runtime/raft-b.db 127.0.0.1 7432 \
  runtime/raft-b.vault runtime/raft-b-trust.txt lab-cluster-01 runtime/raft-members.txt
```

```bash
build/release/feinstatic raft-node runtime/raft-c.db 127.0.0.1 7433 \
  runtime/raft-c.vault runtime/raft-c-trust.txt lab-cluster-01 runtime/raft-members.txt
```

Wait for a leader to be elected. The startup banner states the node ID, cluster, endpoint, member count, checkpoint, and crash-fault limitation.

## 6. Query status

Use a fourth database copied from the same base checkpoint, not one of the databases currently open by node processes. The operator identity must trust the target node, and the target node must trust the operator identity.

```bash
build/release/feinstatic raft-status-auth runtime/raft-operator.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-operator-trust.txt <NODE_A_ID>
```

Status reports role, leader, term, commit index, applied index, log index, Ledger height, and tip hash.

## 7. Submit and replicate a block

To include a signed transaction, submit it to any enabled Raft member's local mempool. v1.7.0 gossips the accepted transaction to the other enabled pre-provisioned members. The operator database must contain the same sender wallet and unlock passphrase:

```bash
build/release/feinstatic peer-submit-tx-auth runtime/raft-operator.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-operator-trust.txt <NODE_A_ID> \
  <SENDER_ADDRESS> <RECIPIENT_ADDRESS> 1.00000000 0.00010000
```

Then ask the current leader to propose a block (the miner address must be valid for the chain):

```bash
build/release/feinstatic raft-propose-block-auth runtime/raft-operator.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-operator-trust.txt <NODE_A_ID> <MINER_ADDRESS>
```

The returned index is the Raft log index. It may be ahead of the Ledger height because internal no-op entries also occupy log indices. An accepted proposal is not yet committed. Query all members and check that `commit_index` and `last_applied` reach the proposal index and that the Ledger height/tip converge.

Transactions are admitted to the addressed node's durable mempool and gossiped best-effort to enabled pre-provisioned members. Gossip is not consensus or finality; block inclusion still requires leader proposal and majority commitment. Direct block import and legacy chain synchronization are rejected in Raft mode, preventing those paths from bypassing consensus.

## Operating constraints

- Inbound handshake/frame I/O is handled by a bounded worker pool (default four workers, maximum sixteen; default queue capacity 64, maximum 4096). Stateful Ledger/Raft operations remain serialized. Tune `InboundAdmissionConfig::worker_count` and `maximum_queued_connections` for the node's connection profile. Queue overflow closes the new connection; clients should retry with backoff.

- Stable odd voter membership of 3, 5, 7, or 9 nodes; online voter changes use joint consensus within the pre-provisioned roster.
- Crash-fault model only; no Byzantine fault tolerance.
- Every member needs its own database and must start from the exact same checkpoint.
- Do not run multiple writers against one database.
- Authenticated protocol v8 encrypts application frames with ephemeral X25519, HKDF-SHA-256, and AES-256-GCM while retaining Ed25519 signatures and replay checks. This is not TLS; metadata remains visible and the channel has not received an independent cryptographic audit. Use loopback/controlled private networks only.
- Snapshot catch-up is supported from v1.2.0 (full Ledger payload, 64 MiB cap); v1.3.0 adds pre-promotion learner catch-up. v1.7 adds inbound token-bucket connection admission and best-effort transaction gossip. Peer discovery, automatic trust distribution, complete DDoS protection, and production finality are not provided.


## Joint-consensus voter replacement (v1.3.0)

A learner must first be included in the same roster and mutually trusted by all nodes. Start it with its own independent copy of the common Ledger checkpoint and its own identity/database. It receives snapshots and AppendEntries but cannot vote or become leader. Wait until `raft-status-auth` reports that it has applied the current leader's committed index.

A trusted operator can then request a new voter set from the current leader:

```text
feinstatic raft-change-members-auth db host port identity.vault trust.txt expected-peer-id voter-id-1,voter-id-2,voter-id-3
```

The state machine first commits `C_old,new` using a majority of both old and new configurations, then commits `C_new` using those same dual majorities. Status reports `joint-pending`, `joint-committed`, `final-pending`, or `stable`, together with the voter and learner sets. A request being accepted is not evidence that the change has committed; query all nodes until they report the same stable configuration. Removed voters become learners and can be re-promoted later after catch-up.

In v1.3.0, any identity trusted by a node could submit membership administration requests. v1.4.0 supersedes that rule: only an identity explicitly assigned the `operator` role in the target node's local trust store may request voter changes. Peer-ID discovery is not supported. v1.5.0 can explicitly redistribute a signed trust policy to one already-provisioned node at a time; v1.6.0 can update endpoints only within that fixed roster.


## Operator authorization and peer identity lifecycle (v1.4.0)

Only trust-store identities explicitly assigned the `operator` role can submit `raft-change-members-auth`. A regular trusted `peer` can still use allowed peer operations and query Raft status, but cannot change the voter set. Use a dedicated operator identity; do not give every node identity administrative rights.

For an observer or other non-member peer, prepare the management trust file for each target and use the v1.5.0 `raft-trust-policy-push` command in the next section. This installs a signed policy remotely and persists it before acknowledgement. The older `peer-trust-revoke trust.txt <PEER_ID>` command remains available for offline/local trust-file maintenance.

To rotate an operator or peer identity, use `peer-trust-rotate trust.txt <OLD_PEER_ID> <NEW_PUBLIC_KEY_HEX>` in the management policy. The command retains the old role for the new identity and records the old identity as revoked. Push the resulting policy to each target. For an identity still present in the target node's configured peer roster, policy replacement refuses trust revocation. In v1.6.0, first demote a voter via joint consensus, disable its endpoint if desired, and then follow the trust-policy removal sequence; endpoint disable alone does not remove the peer ID from the roster.


## 8. Signed trust-policy rollout (v1.5.0)

Trust-policy rollout is explicit and target-by-target. Prepare a management trust file containing the complete desired trusted identity set for the target node, including the target node's public key (for client-side server pinning), the signing identity with the `operator` role, every other pre-provisioned Raft peer, and at least one active operator after the change. This is an administrative policy file; it is not necessarily byte-for-byte the target node's own current inbound trust file because it also contains the target's own public key.

Push a new policy epoch to one node:

```bash
build/release/feinstatic raft-trust-policy-push runtime/raft-operator.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-a-management-trust.txt <NODE_A_ID> lab-cluster-01 1
```

The signer must already be an active operator in the policy and in the target node's existing trust store. The target verifies the authenticated encrypted session, Ed25519 policy signature, cluster ID, and a strictly increasing epoch, preserves all existing revocation tombstones, and persists its new trust store before acknowledging. The management trust file advances its epoch only after the remote acknowledgement. Use the next greater epoch for the next push from the same management file.

Push separately to each intended node. A target refuses a policy that removes a remote endpoint still in its pre-provisioned Raft roster; first complete the corresponding membership/endpoint removal. Revocation rejects future handshakes but does not terminate an already-established TCP session. Policy push does not automatically broadcast to other nodes.


## 9. Live endpoint management and outbound health (v1.6.0)

Every Raft node creates an owner-only endpoint registry at `<database>.endpoints` from its startup members file on first run. The registry is versioned, checksummed, cluster-bound, and tied to the exact peer-ID roster. Back it up alongside the database, identity vault, and trust store. Do not copy one node's endpoint registry to another node: the registry is cluster-bound and includes that node's configured peer roster/endpoints.

Query per-peer dispatcher health using an identity assigned `operator` in the target node's trust store:

```bash
build/release/feinstatic raft-peer-health-auth runtime/raft-a.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-a-management-trust.txt <NODE_A_ID>
```

Change the host/port of an already-provisioned remote peer, or re-enable a disabled endpoint:

```bash
build/release/feinstatic raft-peer-endpoint-set-auth runtime/raft-a.db 127.0.0.1 7431 \
  runtime/raft-operator.vault runtime/raft-a-management-trust.txt <NODE_A_ID> \
  <TARGET_PEER_ID> 127.0.0.1 7532 enabled
```

The final argument may be `disabled`. The endpoint update is persisted atomically and returns a new registry generation. Only the endpoint of a pre-provisioned remote peer can be changed; local listener binding, peer IDs, voter/learner membership, and the trust roster are not changed. The API refuses to disable an active voter. Use `raft-change-members-auth` to demote that voter through joint consensus first, verify all nodes report the new stable configuration, then disable its transport endpoint. Endpoint registry updates are local to the node receiving the RPC; apply the corresponding update to other nodes that need the new address.

The dispatcher uses a shared bounded worker pool (default four workers), one in-flight request per peer to preserve FIFO ordering, a per-peer queue limit (default 128), a global count/byte limit, heartbeat coalescing/drop under pressure, and exponential retry backoff capped at eight seconds. Health reports expose attempts, successes, failures, drops, queue depth, last error, last-success/failure timestamps, and retry delay. This is outbound scheduling only; inbound requests and Ledger/Raft state-machine transitions remain serialized on the node event loop.
