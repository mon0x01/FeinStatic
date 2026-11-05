# FeinStatic peer networking (v2.0.0)

## Scope

The TCP peer layer provides bounded request/response framing, chain-parameter compatibility checks, transaction relay, legacy block relay/synchronization, and authenticated peer identities. v1.0.0 adds live Raft RPC dispatch and replicated Ledger block application for a fixed permissioned crash-fault cluster. The Raft cluster path is described in detail in [`raft.md`](raft.md) and the three-node lab procedure in [`raft-networking-guide.md`](raft-networking-guide.md).

The legacy non-Raft peer mode remains available for development, but uses deterministic local fork choice rather than distributed consensus. In Raft mode, direct block import and pull-based chain synchronization are disabled to prevent bypassing the replicated log.

## Peer lifecycle supervision and circuit breaking (v1.9.0)

Every configured outbound peer now has a lifecycle state derived from actual authenticated RPC outcomes:

- `UNKNOWN`: no successful operation has completed in the current enabled endpoint epoch.
- `HEALTHY`: at least one authenticated operation succeeded and there are no consecutive transport failures.
- `DEGRADED`: one to four consecutive transport failures have occurred.
- `OFFLINE`: five or more consecutive transport failures have occurred; the breaker is open until its retry deadline.
- `HALF_OPEN`: the breaker cooldown elapsed and the single allowed in-flight operation is testing recovery.
- `DISABLED`: an operator disabled the peer endpoint. This is distinct from an unreachable peer.

Raft messages and transaction gossip share one peer-level retry gate. A failure in either traffic class blocks both classes until the same retry deadline; this prevents heartbeat and gossip workers from independently hammering an unavailable endpoint. Backoff starts at 250 ms, doubles per consecutive transport failure, and caps at 30 seconds. A stable peer-ID/failure-generation mix adds up to 20% deterministic jitter so a cluster's peers do not all retry at the same instant. The existing one-in-flight-per-peer scheduler provides the half-open single probe. A successful authenticated exchange closes the breaker and resets the consecutive-failure count. A terminal application-level rejection is counted as a completed transport exchange and does not open the circuit.

The operator-only `raft-peer-health-auth` command now displays the lifecycle state in addition to queue and retry counters. Endpoint enable/disable still requires an active operator and cannot disable a current voter until that voter is demoted through joint consensus. Changing an endpoint resets its current lifecycle epoch; it does not alter membership or trust.

The state machine is process-local. It does not persist health history across restart; after restart a configured endpoint begins `UNKNOWN` and its queues are reconstructed from the durable mempool/Raft state. This deliberately avoids making stale failure history a source of permanent unavailability. There is no automatic peer discovery or automatic trust enrollment.

## Bounded inbound I/O workers (v1.8.0)

The listener retains one accept loop, but no longer performs the peer handshake or request processing inline. Every accepted socket first passes the existing global and per-address token buckets, then enters a bounded FIFO connection queue. A fixed worker pool (default 4, configurable from 1 to 16 through `InboundAdmissionConfig::worker_count`) owns independent Asio contexts and processes one connection per worker. The pending queue defaults to 64 and is configurable from 1 to 4096 through `InboundAdmissionConfig::maximum_queued_connections`. If the queue is full, the accepted socket is closed immediately; clients must retry.

Handshake reads, challenge exchange, and request-frame reads run concurrently. All operations that inspect or mutate Ledger, Raft, trust-policy, or endpoint state are protected by a per-node state mutex. The handler builds the response under the state lock and releases the lock before sending bytes, so a slow or non-reading peer cannot hold the state boundary during response transmission. This is bounded I/O concurrency, **not parallel Ledger execution**: state transitions still occur one at a time. Each individual socket operation is timeout-bounded. Shutdown drains the pending queue and joins active workers; active operations exit when their configured socket timeout or peer disconnect completes.

The worker pool does not provide a complete DoS defense. Token buckets and the bounded queue cap local resource use after TCP accept, but do not replace kernel SYN-flood protection, upstream bandwidth limits, per-request CPU budgets, or an Internet-facing abuse mitigation layer.

## Node identity and trust

A node identity is independent from customer Ledger wallets. Generate one identity per node:

```bash
build/release/feinstatic node-keygen-vault runtime/node-a.vault
```

The command prompts twice for a passphrase without echoing it. The vault stores a 32-byte Ed25519 seed encrypted with PBKDF2-HMAC-SHA-256 and AES-256-GCM. It is created with mode `0600`, must be owned by the current user, and uses a bounded KDF work factor. The node ID is derived from the public key. Share only the printed node ID and public key; never share the vault or passphrase. Existing raw `node-keygen` 0600 identity files remain readable for compatibility; new deployments should use vaults. The private seed exists in process memory while the vault is unlocked, so this is not hardware-backed key custody.

The text trust store is an explicit allow-list. New rows use `<peer-id> <public-key-hex> <peer|operator|revoked-peer|revoked-operator>`; legacy two-column rows load as ordinary `peer` identities. Add a remote peer:

```bash
build/release/feinstatic peer-trust-add runtime/trusted-peers.txt \
  <REMOTE_NODE_ID> <REMOTE_64_BYTE_HEX_PUBLIC_KEY> peer
```

The CLI verifies that the supplied node ID is derived from the supplied public key. Trust stores are bounded. A server accepts only clients present in its local trust store. A client pins the expected remote peer ID and checks it against its trusted public key. The `identity.key` placeholder in older examples may be a legacy raw file or an encrypted `.vault`; the loader detects the format.

Trust is bilateral: A must trust B for A to accept B's requests, and B must trust A for B to accept A's requests. There is no automatic peer discovery or certificate authority. v1.5.0 adds signed trust-policy replication to one already-provisioned node at a time. v1.6.0 adds live host/port and enabled-state management for those pre-provisioned peers, but does not discover or add peer IDs.

## Authenticated handshake and session frames

Authenticated protocol v8 uses mutual Ed25519 challenge-response with fresh nonces and chain-parameter binding. Both peers exchange ephemeral X25519 public keys, sign those keys into the handshake transcript, and derive independent directional AES-256-GCM keys with HKDF-SHA-256. Every post-handshake frame is encrypted; its session binding, type, and monotonically increasing sequence number are AEAD associated data, and its ciphertext retains an Ed25519 signature. Receivers reject invalid signatures, AEAD tags, repeated sequence numbers, gaps, and out-of-order frames.

This is application-layer encryption, not TLS. It does not hide IP/port, packet-size, or timing metadata and has not received an independent cryptographic audit. Use controlled private networks only. Membership administration and trust-policy installation are restricted to active `operator` identities. `peer-trust-revoke` records a local revocation tombstone; `peer-trust-rotate` replaces an identity while retaining its role.

### Encrypted identity vaults

`node-keygen-vault runtime/node-a.vault` generates an Ed25519 node identity and encrypts its private seed at rest. Existing node commands detect the vault format and prompt for its passphrase through a hidden terminal input. Keep an offline backup of the vault and passphrase separately. Losing either means losing the node identity; vault encryption does not protect a key while the process is running or from a compromised host.

### Signed trust-policy push

`raft-trust-policy-push db host port operator.vault management-trust.txt expected-peer-id cluster-id epoch` sends a complete signed trust policy to one existing Raft node. The management trust file must contain the target node's public key (for the client-side pinned connection), the signing identity as an active `operator`, and the complete desired set of trusted node/operator identities for the target. Use a new positive epoch greater than the management file's stored policy epoch. The remote node verifies the encrypted authenticated session, active operator identity, signature, cluster ID, and strictly increasing epoch, then writes its trust store before acknowledging. The management file advances its own epoch only after that acknowledgement.

Policies preserve local revocation tombstones and cannot remove the last active operator. A target also refuses a policy that revokes any remote endpoint still present in its pre-provisioned Raft roster; complete the Raft membership/endpoint removal first. Push the policy separately to each intended node. Existing sessions are not forcibly disconnected, so a revoked identity is rejected on its next handshake, not retroactively removed from an established connection. Policy updates do not automatically propagate to other nodes.

The legacy unauthenticated `node-listen` listener is restricted to loopback. Remote binds require `node-listen-auth` or the Raft-enabled `raft-node` command with explicit trust configuration.

## Protocol and bounds

Wire protocol version 8 retains the bounded framing and adds Raft RPC, Raft status with membership metadata, block-proposal, and joint-consensus membership-change request/response messages. A frame includes the `FSN1` magic, wire version, message type, bounded payload length, sequence metadata, SHA-256 payload checksum, Ed25519 signature (zero only for pre-authentication handshake frames), and payload. The maximum frame payload is 16 MiB. Transaction and block serializers enforce their own smaller bounds. Network operations are time-bounded.

Raft RPCs use the same authenticated session framing. A member accepts a Raft message only when the authenticated transport peer ID equals `message.from`, `message.to` equals the local member ID, and the sender belongs to the configured voter/learner roster. Learners may send append/snapshot acknowledgements but cannot vote or campaign.

## Raft-enabled runtime

Start a static cluster member with:

```bash
build/release/feinstatic raft-node db address port identity.key trust.txt cluster-id members.txt
```

The members file has one `<peer-id> <host> <port> [voter|learner]` row per cluster member, including the local node. Every node uses the same cluster ID and peer IDs. The local node's line must match the listener port. Each node's local trust store contains every other member's public key. v1.6.0 persists the remote host/port/enabled state in `<database>.endpoints`; the registry is initialized from this file and then becomes the live endpoint source. Peer IDs and voter/learner roster membership remain pre-provisioned and are not changed by endpoint RPCs. The full procedure is in [`raft-networking-guide.md`](raft-networking-guide.md).

Status and proposals use authenticated client commands:

```bash
build/release/feinstatic raft-status-auth db host port identity.key trust.txt expected-peer-id
build/release/feinstatic raft-propose-block-auth db host port identity.key trust.txt expected-peer-id miner-address
```

Only a ready Raft leader accepts a block proposal. The result reports an accepted Raft log index; it does not guarantee commitment. Query members until the commit and applied indices converge. Remember that no-op entries consume Raft indices without creating Ledger blocks.

Transactions are admitted to the addressed member's durable mempool by `peer-submit-tx-auth`. In Raft mode, an accepted new transaction is queued for best-effort gossip to every other enabled pre-provisioned voter/learner; the sender is excluded. Receivers validate and persist the transaction in their own mempool, and duplicates count as successful delivery. Gossip does not commit a transaction or make it part of the canonical Ledger: only a leader proposal followed by Raft majority commitment and ordered application does that.

The outbound dispatcher also periodically scans the durable mempool and refills bounded gossip queues. This recovers dissemination after a process restart, because outbound queues themselves are volatile. Per-peer pending gossip IDs suppress repeated queueing while a delivery is pending or already completed in that process. Queue saturation defers work to a later scan rather than making the queue unbounded.

## Runtime threading and persistence

The accept loop applies token-bucket admission and then queues accepted sockets. Bounded inbound workers handle handshake and request-frame I/O concurrently. Every operation that reads or mutates Ledger, Raft, trust-policy, or endpoint state is serialized by the per-node state mutex, while response bytes are written after releasing it. The per-address bucket map is bounded by `maximum_tracked_addresses`; idle entries expire after `address_idle_expiry`. `InboundAdmissionConfig` exposes global/per-address refill rates and burst sizes, and Raft mode reads it from `RaftNetworkConfig::inbound_admission`. Defaults are deliberately generous for a busy nine-member private cluster; operators should tune them to the expected source-IP topology. The admission gate and bounded worker queue limit local connection work but do not protect upstream bandwidth or constitute a full DDoS defense.

Outbound Raft RPCs and transaction gossip use a fixed-size shared worker pool (default four, maximum eight), not one thread per peer. Each peer has separate bounded FIFO queues for Raft and gossip, one in-flight message per peer, and reserved capacity so gossip cannot consume the configured Raft queue budget. Raft messages receive priority, but an eight-message streak limit prevents permanent gossip starvation. Raft and gossip share one peer-level circuit breaker with capped exponential backoff and stable peer-specific jitter. Retryable failures are requeued at the head of the same per-peer queue while the in-flight capacity reservation is retained; permanent transaction rejection is terminal. Stale heartbeat work can be coalesced or dropped under pressure.

Committed block application persists the Ledger block append, durable mempool reconciliation, and Raft `last_applied` snapshot in one SQLite transaction. In-memory state is published only after commit. On restart, the adapter verifies the persisted applied log against the recovered Ledger chain.

Each node must use its own SQLite database. Do not open one database from multiple writable processes. Every member must start from the same exact checkpoint and the same pre-provisioned voter/learner roster. The initial roster can contain at most nine total identities; later voter sets are committed through joint consensus without discovering new endpoints or trust entries.

## Legacy non-Raft peer mode

The legacy authenticated `node-listen-auth`, `peer-status-auth`, `peer-sync-auth`, `peer-submit-tx-auth`, and `peer-relay-tip-auth` commands remain available for development. The older unauthenticated listener and peer commands are restricted to loopback. Non-Raft sync uses candidate-first validation, fixed-difficulty fork choice and atomic reorganization; that policy is not distributed consensus.

## Remaining work and explicit limits

Raft mode is a crash-fault-tolerant prototype. Voter membership can change through joint consensus, but only within a pre-provisioned endpoint roster of at most nine voters plus learners. New endpoints are not discovered. v1.5.0 trust-policy pushes remain explicit and target-by-target. v1.7.0 adds token-bucket admission and automatic best-effort transaction gossip; v1.8.0 adds bounded concurrent inbound I/O while preserving serialized state access; v1.9.0 adds peer lifecycle states and a unified retry circuit breaker; v2.0.0 makes acknowledged Raft snapshot-transfer chunks durable across receiver restarts without changing wire protocol v8. These controls are not an Internet-scale abuse defense or a consensus/finality mechanism. The system does not support Byzantine fault tolerance, NAT traversal, automatic peer discovery, or production finality. Protocol v8 provides application-layer frame confidentiality but is not TLS and has not received an independent cryptographic audit. Run only on loopback or a controlled private network.
