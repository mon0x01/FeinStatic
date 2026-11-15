# FST Network Threat Model — v0.1

**Status:** DRAFT — initial threat model.
**Classification:** PROVISIONAL (preferred direction requiring validation; not an
approved production security design).
**Milestone:** FST Milestone 2E — Core Protocol Specification and Node Architecture.
**Related decisions:** D-001, D-002, D-003, D-008, D-016, D-017, D-018, D-019, D-020,
D-023, D-024, D-025, D-035, D-036.
**Related research:** RS-001 through RS-014; security register entries.

This threat model identifies realistic threats to the FST network layer and core
protocol. For each threat it documents attacker capabilities, assets at risk, trust
assumptions, existing mitigations, missing mitigations, residual risks, and needed
research or engineering work.

**No claim of production security:** FST does not claim to be production-secure or fully
Sybil-resistant. Consensus, Sybil resistance, validator membership, and monetary policy
remain OPEN REARCH (decisions D-015, D-016, D-017). This threat model applies to the
network and protocol layer as specified in the core protocol specification
(`spec/core-protocol-specification-v0.1.md`) and implemented in Milestones 2C–2D.1.

## 1. Threat Model Methodology

- Threats are listed individually with a consistent structure.
- Each threat distinguishes **implemented protections** (existing code, Milestones 2C–2D.1)
  from **planned protections** (proposed specification, not yet implemented).
- No threat claims complete mitigation; residual risks are acknowledged.
- Research-first: each threat lists the research or engineering work needed before the
  threat can be considered addressed (principle P12).
- Sources are not fabricated; where no authoritative source exists, the limitation is
  stated.

## 2. Threat Inventory

| # | Threat | Status of protections |
|---|--------|----------------------|
| T-01 | Malicious peers | Partially mitigated (implemented) |
| T-02 | Sybil attacks | Not mitigated (OPEN RESEARCH) |
| T-03 | Eclipse attacks | Partially mitigated (implemented) |
| T-04 | Network partitioning | Partially mitigated (implemented) |
| T-05 | Denial of service / resource exhaustion | Partially mitigated (implemented) |
| T-06 | Message replay and network confusion | Partially mitigated (implemented) |
| T-07 | Identity-key compromise | Partially mitigated (implemented) |
| T-08 | Malicious bootstrap sources | Partially mitigated (implemented) |
| T-09 | Traffic analysis and metadata exposure | Not mitigated (OPEN RESEARCH) |
| T-010 | Malformed protocol messages | Mitigated (implemented) |
| T-011 | Byzantine participants | Not mitigated (OPEN RESEARCH) |
| T-012 | Data corruption and inconsistent state | Partially mitigated (implemented) |
| T-013 | Malicious or compromised independent software implementations | Not mitigated (planned) |

## 3. Individual Threat Analyses

### T-01: Malicious Peers

**Attacker capabilities:** A peer that follows the network identity and cryptographic
protocols but behaves maliciously after session establishment: sends invalid messages,
withholds messages, sends conflicting information to different peers, advertises false
capabilities or endpoints, or attempts to exhaust node resources through high message
volume.

**Assets at risk:** Node availability, peer-store integrity, message-delivery reliability,
local resource consumption (CPU, memory, bandwidth, file descriptors).

**Relevant trust assumptions:** Nodes do not trust peers for state correctness; peers are
authenticated but not trusted (P9 — security from protocol design and independent
verification).

**Existing mitigations (implemented):**
- Mutual TLS 1.3 authentication with node-record binding (`secure_session.cpp`,
  `network_runtime.cpp`).
- Peer authentication via `StorePeerAuthenticator` backed by `PeerStore`
  (established implementation).
- Per-peer connection limits (max 2 per peer) and total connection limits
  (max 64 total, 32 inbound, 32 outbound) (`peer-admission.md`).
- Bounded message payloads and per-type size limits (`message_dispatch.cpp`,
  `kMaxPingPayloadBytes`, `kMaxPeerRecordBytes`, etc.).
- Peer lifecycle circuit breaker with capped exponential backoff (250 ms – 30 s)
  and jitter (`peer_lifecycle.cpp`, feinstat-baseline.md).
- Wrong-network rejection: records/sessions from a different network identifier are
  rejected (`node_record.cpp` validation rule 3).
- Rate limiting on peer-record exchange per peer (`peer_exchange.cpp`).

**Missing mitigations (planned):**
- Peer reputation or behavior-based throttling beyond connection limits.
- Per-peer message rate limiting (beyond payload size limits).
- Misbehavior scoring and peer banning/disconnection for persistent bad behavior.
- Adaptive connection limits under load.

**Residual risks:** A malicious peer can still consume connection slots, send valid but
useless messages, or selectively withhold messages to disrupt synchronization.

**Research or engineering work needed:** Peer behavior scoring, adaptive rate limiting,
and misbehavior-based peer disqualification (engineering; no new research dependency).

### T-02: Sybil Attacks

**Attacker capabilities:** An adversary creates a large number of identities (node IDs,
key pairs, signed node records) to gain disproportionate influence over peer discovery,
message dissemination, or (if consensus is implemented) validator participation.

**Assets at risk:** Network topology integrity, peer-discovery reliability, consensus
quorum integrity (future), partition resilience.

**Relevant trust assumptions:** Identity creation is cheap and unbounded; a public key or
node ID authenticates a participant but does NOT limit how many identities an adversary
can create (identity-peer-discovery-research.md Section 2.2).

**Existing mitigations (implemented):** None specific to Sybil resistance. Node
authentication exists, but authentication ≠ Sybil resistance.

**Missing mitigations (planned):** None selected. Sybil resistance is a separate,
unresolved research question (D-016 PROVISIONAL, D-017 OPEN RESEARCH, RS-001, RS-002).

**Residual risks:** HIGH. An adversary can create unlimited identities and potentially
dominate peer discovery, eclipse victims, or (in future) consensus participation. No
Sybil-resistance mechanism is selected.

**Research or engineering work needed:** Sybil-resistance mechanism research (RS-002:
proof of work, proof of stake, or alternative); consensus Sybil-resistance layer
(RS-013). This is OPEN RESEARCH and must not be implemented as production design
(decision D-016/D-017 classifications).

### T-03: Eclipse Attacks

**Attacker capabilities:** An adversary isolates a victim node from the rest of the
network by controlling all or most of the victim's peer connections, feeding it a
selective view of the network (false peer records, withheld blocks/transactions, or
conflicting state).

**Assets at risk:** Victim's view of chain state, synchronization correctness,
transaction submission, fork-choice integrity (future).

**Relevant trust assumptions:** A node needs at least one honest peer to maintain an
accurate network view. Bootstrap services are not trust roots.

**Existing mitigations (implemented):**
- Multiple independent bootstrap sources (no single source is a central authority;
  `bootstrap.cpp`, `peer-admission.md`).
- Peer exchange: nodes learn additional peers from connected peers (`peer_exchange.cpp`).
- Signed node record validation prevents forged peer records (`node_record.cpp`).
- Duplicate connection resolution limits connections per peer (max 2)
  (`peer_admission.cpp`).
- Connection limits bound the total number of peers a node can connect to
  (`peer-admission.md`).

**Missing mitigations (planned):**
- Diverse peer selection across address prefixes, ASes, or networks (not implemented).
- Periodic re-announcement and peer refresh (not implemented).
- Outbound connection diversity requirements (not implemented).
- Detection of topological isolation (e.g., all peers from a single source).

**Residual risks:** A well-positioned adversary controlling bootstrap sources and
peer-exchange responses could eclipse a newly joining or infrequently connected node.

**Research or engineering work needed:** Diverse peer selection, eclipse-detection
heuristics, and peer-source diversity tracking (engineering; research on DHT-based
discovery is OPEN RESEARCH, RS-005).

### T-04: Network Partitioning

**Attacker capabilities:** A network-level adversary partitions the network into
disconnected components, preventing message flow between them.

**Assets at risk:** Network liveness, global consistency, synchronization, settlement
finality (future).

**Relevant trust assumptions:** CAP theorem — during a partition, global linearizability
and full availability cannot both hold (consensus-validator-research.md Section 3.7).
FLP — deterministic consensus cannot guarantee liveness under full asynchrony with even
one crash failure (Section 3.5). Offline operation is bounded and reconciled later
(D-018 PROVISIONAL).

**Existing mitigations (implemented):**
- Peer lifecycle circuit breaker handles temporary disconnections with bounded backoff
  (`peer_lifecycle.cpp`).
- Bounded reconnection with jitter after partition heals.
- Snapshot and streaming patterns preserved from FeinStatic for state recovery
  (D-032 PROVISIONAL).

**Missing mitigations (planned):**
- Partition detection and local-mode operation (offline operation D-018 PROVISIONAL;
  mechanisms unresolved).
- Reconciliation protocol after partition heals (unresolved).
- Multi-transport redundancy (internet + mesh + radio) to reduce partition likelihood
  (D-019 PROVISIONAL; transport adapters beyond TCP unimplemented).

**Residual risks:** During a partition, nodes in the minority component cannot synchronize
with the majority. Safety under the consensus fault bound is preserved (future), but
liveness is sacrificed until reconnection (consensus-validator-research.md Section 8.5).

**Research or engineering work needed:** Offline operation design (RS-007, D-018),
multi-transport adaptation (RS-005, D-019), consensus partition tolerance (RS-013).

### T-05: Denial of Service and Resource Exhaustion

**Attacker capabilities:** An adversary floods a node with connections, messages, peer
records, or invalid data to exhaust CPU, memory, bandwidth, file descriptors, or storage.

**Assets at risk:** Node availability, network participation, peer-store integrity.

**Relevant trust assumptions:** Nodes have bounded resources; unbounded input must be
rejected.

**Existing mitigations (implemented):**
- Maximum total connections (64), inbound (32), outbound (32), per peer (2)
  (`peer-admission.md`).
- Maximum payload sizes per message type and global 1 MiB frame cap
  (`message_dispatch.cpp`).
- Peer-store bounded storage: `max_peers` config; `RejectedStoreFull` when limit
  reached (`peer_store.cpp`).
- Signed node record strict bounds: 64 KiB max record, 16 endpoints, 32 capabilities,
  16 protocol versions, 64-byte capability strings, 253-byte host strings
  (`node_record.cpp`).
- Inbound token-bucket admission (global + per-address) from FeinStatic baseline
  (feinstat-baseline.md, networking section).
- Bounded inbound worker pool (4–16 workers) and bounded pending queue (64–4096)
  (feinstat-baseline.md).
- Secret cleansing on key destruction (`OPENSSL_cleanse`, `identity.cpp`).

**Missing mitigations (planned):**
- Per-peer message rate limiting (not implemented).
- Connection-rate limiting (beyond admission token buckets).
- Memory bounds for handshake state and session buffers.
- Disk space monitoring and graceful degradation when storage is full.
- TLS handshake rate limiting to prevent TLS-based exhaustion.

**Residual risks:** A determined adversary can still exhaust node resources through
connection floods, message floods, or record floods despite bounded limits.

**Research or engineering work needed:** Per-peer rate limiting, connection-rate
limiting, memory/disk exhaustion handling (engineering).

### T-06: Message Replay and Network Confusion

**Attacker capabilities:** An adversary captures and retransmits valid messages (replay),
or confuses nodes by mixing messages from different networks or sessions (network
confusion).

**Assets at risk:** Session integrity, state consistency, fork integrity, replay across
networks.

**Relevant trust assumptions:** Network identifier distinguishes independent networks
(D-036 APPROVED). Session nonces bind messages to sessions.

**Existing mitigations (implemented):**
- TLS 1.3 sequence numbers provide transport-level replay protection
  (`secure_session.cpp`).
- Application-level session nonce (`SecureHandshakeResult.session_nonce`) bound to each
  TLS session (established implementation).
- Monotonic sequence numbers on signed node records prevent record rollback
  (`node_record.cpp` validation rule 6).
- Wrong-network rejection: records/sessions from a different network identifier are
  rejected (`node_record.cpp` validation rule 3).
- Expired-record rejection: records past expiry are not accepted
  (`node_record.cpp` validation rule 5).
- 0-RTT not used in TLS 1.3 (replay concerns; `secure-sessions.md`).

**Missing mitigations (planned):**
- Transaction-level replay protection across networks (chain ID in transaction
  signature or nonce; OPEN RESEARCH, D-036 mechanisms unresolved).
- Message-level replay protection for application messages beyond session nonce
  (unresolved).

**Residual risks:** Transaction-level replay across forked networks is not protected
(mechanism unresolved). Application-message replay outside the session context is
unaddressed.

**Research or engineering work needed:** Chain-ID-based transaction replay protection
(D-036, OPEN RESEARCH mechanisms); application-message replay protection (engineering).

### T-07: Identity-Key Compromise

**Attacker capabilities:** An adversary obtains a node's long-term Ed25519 identity
private key, allowing it to impersonate the node, sign fraudulent node records, or bind
fraudulent TLS certificates.

**Assets at risk:** Node identity integrity, session authentication, peer trust,
potentially funds (if identity key controls financial authority — not the case for node
identity per security invariant).

**Relevant trust assumptions:** Node identity keys are long-term Ed25519 keys stored in
mode-0600 files or AES-256-GCM encrypted vaults (established implementation). Node
identity does NOT grant validator status or financial authority (signed node record
security invariant).

**Existing mitigations (implemented):**
- Private keys stored in mode-0600 files or AES-256-GCM + PBKDF2-HMAC-SHA256 encrypted
  vaults (`FileKeyProvider`, `identity.cpp`).
- Private key cleansing with `OPENSSL_cleanse` on destruction (`identity.cpp`).
- Domain-separated signing prevents cross-context signature reuse
  (`KeyProvider::sign_domain`, `identity.cpp`).
- TLS certificate key is ephemeral and separate from the long-term identity key
  (`secure-session.md`).
- Node-record rotation via `RotationRecord` binds old and new identities
  (`identity.cpp`).
- Monotonic sequence numbers prevent rollback to old records after rotation
  (`node_record.cpp`).

**Missing mitigations (planned):**
- HSM/TPM/enclave-backed key storage (`KeyProvider` abstraction provides boundary;
  no production backend implemented).
- Key compromise detection and emergency rotation (not implemented).
- Revocation of compromised identity keys or node records (not implemented).
- Threshold signatures or DKG for distributed key management (OPEN RESEARCH,
  RS-004/RS-013).

**Residual risks:** Compromise of a long-term identity key allows impersonation until
detected and rotated. No revocation mechanism exists.

**Research or engineering work needed:** HSM/TPM key storage backend (engineering),
key-compromise detection and revocation (engineering/research), threshold signatures
(OPEN RESEARCH, RS-004).

### T-08: Malicious Bootstrap Sources

**Attacker capabilities:** An adversary operates a bootstrap source that serves stale,
incorrect, or malicious peer records, or withholds records from targeted nodes.

**Assets at risk:** Peer discovery reliability, initial connectivity, eclipse resistance
for new nodes.

**Relevant trust assumptions:** Bootstrap sources are NOT trust roots; they provide
connectivity assistance only (peer-admission.md Section "Bootstrap Authority").

**Existing mitigations (implemented):**
- Multiple independent bootstrap sources; no single source is privileged
  (`bootstrap.cpp`, `peer-admission.md`).
- All bootstrap records validated by signature, sequence, expiry, and network ID
  (`bootstrap.cpp`, `node_record.cpp`).
- Bootstrap source is not a trust root for membership or consensus
  (`peer-admission.md`).
- Discovery source tracking in `PeerStore` (`Bootstrap`, `PeerExchange`, etc.)
  (`peer_store.cpp`).

**Missing mitigations (planned):**
- Bootstrap source reputation or health tracking (not implemented).
- Dynamic bootstrap source discovery (static list only; `bootstrap.cpp`).
- Cross-validation of bootstrap records across multiple sources (not implemented).
- Bootstrap source authentication beyond record self-certification (unresolved;
  node-identity-peer-discovery-implementation.md Limitation 5).

**Residual risks:** A malicious bootstrap source can withhold records (denial of initial
connectivity) or serve records for malicious peers (facilitating eclipse). It cannot
forge records for other nodes.

**Research or engineering work needed:** Dynamic bootstrap discovery, multi-source
cross-validation, bootstrap source health tracking (engineering; source discovery
unresolved).

### T-09: Traffic Analysis and Metadata Exposure

**Attacker capabilities:** A network observer (ISP, AS-level adversary, or passive
monitor) analyzes traffic patterns, timing, packet sizes, and connection metadata to
infer node relationships, transaction activity, or network topology.

**Assets at risk:** User privacy, node relationships, network topology, transaction
confidentiality (metadata layer).

**Relevant trust assumptions:** TLS 1.3 encrypts payloads but not metadata (packet
timing, sizes, connection endpoints). Strong cryptographic privacy is PROVISIONAL
(D-020); specific mechanisms are OPEN RESEARCH (RS-003).

**Existing mitigations (implemented):**
- TLS 1.3 application-layer encryption of all message payloads
  (`secure_session.cpp`, `secure-sessions.md`).
- No plaintext transaction contents over the wire (transactions not yet implemented).

**Missing mitigations (planned):**
- Padding or traffic morphing to obscure message sizes and timing (not implemented).
- Mix networks, onion routing, or anonymous transport (not implemented; research
  needed).
- Metadata minimization in node records and handshakes (not implemented).
- Dummy traffic or cover traffic (not implemented).

**Residual risks:** HIGH. Traffic metadata (who talks to whom, when, how much) is
exposed. No privacy mechanism beyond encryption is implemented.

**Research or engineering work needed:** Traffic-analysis resistance research (RS-003,
RS-005), privacy-preserving transport (OPEN RESEARCH, D-020).

### T-010: Malformed Protocol Messages

**Attacker capabilities:** An adversary sends malformed, oversized, or invalidly
checksummed messages to crash handlers, consume resources, or trigger undefined behavior.

**Assets at risk:** Node availability, handler integrity, connection stability.

**Relevant trust assumptions:** All inbound messages are untrusted until validated.

**Existing mitigations (implemented):**
- Frame checksum (SHA-256 of payload) verification on receipt
  (`message-dispatch.md`, `message_dispatch.cpp`).
- Oversized payload rejection → connection terminated
  (`message-dispatch.md`).
- Per-type size limits via `validate_message` (`message_dispatch.cpp`).
- Unhandled message types return `errc::protocol_error` (`message_dispatch.cpp`).
- Unauthenticated sessions cannot send/receive messages
  (`message_dispatch.cpp`).
- Checksum mismatch → connection terminated (`message-dispatch.md`).

**Missing mitigations (planned):** None identified beyond existing implementation;
message validation is considered implemented for baseline message types.

**Residual risks:** Application-layer message types (transactions, blocks, consensus)
are not yet defined; their validation logic is future work.

**Research or engineering work needed:** Validation logic for future application-layer
message types (engineering; no new research).

### T-011: Byzantine Participants

**Attacker capabilities:** Participants (peers, future validators) deviate arbitrarily
from the protocol: equivocate, withhold messages, send conflicting state, collude, or
attempt safety violations.

**Assets at risk:** Consensus safety (future), state consistency, fork integrity,
settlement finality.

**Relevant trust assumptions:** Consensus algorithm and Byzantine fault threshold are
unresolved (D-017 OPEN RESEARCH). Classical BFT requires 3f+1 replicas for f Byzantine
faults with a known fixed set (consensus-validator-research.md Section 3.2).

**Existing mitigations (implemented):** None at the consensus level. Network-layer
message authentication exists but does not address Byzantine agreement.

**Missing mitigations (planned):** None selected. BFT consensus is an OPEN RESEARCH
direction, not a selected implementation (D-017, P9).

**Residual risks:** HIGH. No Byzantine fault tolerance exists in the current baseline
(Raft is crash-fault only, feinstat-baseline.md). Byzantine participants can disrupt
consensus (future) or network-layer coordination.

**Research or engineering work needed:** Consensus algorithm selection (RS-001, D-017),
Byzantine fault threshold analysis (RS-013 Section 9.2), formal verification
(RS-013 Section 10).

### T-012: Data Corruption and Inconsistent State

**Attacker capabilities:** Data corruption from disk errors, bit rot, or software bugs;
inconsistent state from partial writes, crashes during commit, or divergent state
across nodes.

**Assets at risk:** State integrity, ledger correctness, crash recovery.

**Relevant trust assumptions:** Local storage may corrupt; SQLite checksums detect but
do not authenticate corruption.

**Existing mitigations (implemented):**
- SQLite WAL mode, `synchronous=FULL`, `PRAGMA quick_check(1)` on open
  (feinstat-baseline.md).
- `BEGIN IMMEDIATE` transactions for atomicity (feinstat-baseline.md).
- SHA-256 checksums on persisted payloads (corruption detection, not authentication)
  (feinstat-baseline.md).
- Additive schema migrations with `PRAGMA user_version` guard
  (feinstat-baseline.md).
- Atomic `upsert()` persistence in `PeerStore` (`peer_store.cpp`).
- Snapshot staging and chunks tables for durable resumable transfers
  (`raft_state`, `raft_snapshots`, `raft_snapshot_staging`; feinstat-baseline.md).

**Missing mitigations (planned):**
- Authenticated (signed/MACed) persisted state, not just checksummed (unresolved;
  checksums detect corruption but do not authenticate origin).
- Periodic state consistency verification (not implemented).
- Redundant storage or erasure coding (not implemented).

**Residual risks:** Checksums detect corruption but do not prevent or authenticate it.
Consistent state across nodes depends on consensus (future, unresolved).

**Research or engineering work needed:** Authenticated persisted state (engineering),
cross-node state consistency (depends on consensus, OPEN RESEARCH D-017).

### T-013: Malicious or Compromised Independent Software Implementations

**Attacker capabilities:** An independent team implements an FST client with malicious
intent (censoring transactions, selective forwarding) or with bugs (crashing peers,
sending invalid state). Independent implementations are a goal (P4) but introduce
implementation diversity risk.

**Assets at risk:** Network interoperability, state consistency, censorship resistance,
node availability.

**Relevant trust assumptions:** Independent implementations are expected but not trusted
for correctness (P4 — independent teams must be able to maintain the software).

**Existing mitigations (implemented):** None specific to implementation diversity.
Protocol specification exists (this document) but no test vectors, reference
implementation, or interoperability harness.

**Missing mitigations (planned):**
- Protocol test vectors for interoperability testing (not created).
- Independent implementation compatibility matrix (not created).
- Specification conformance testing harness (not created).
- Formal specification for machine-checkable conformance (unresolved).

**Residual risks:** Independent implementations may diverge in protocol interpretation,
causing network splits or incompatibility. Malicious implementations can censor or
misbehave within protocol bounds.

**Research or engineering work needed:** Test vectors, conformance harness, and
independent-implementation research (RS-012, independent client implementation count,
OPEN RESEARCH D-035). Specification precision for independent implementation.

## 4. Implemented vs Planned Protections Summary

| Protection | Status | Threats addressed |
|------------|--------|------------------|
| TLS 1.3 mutual auth + node binding | Implemented | T-01, T-07 |
| Signed node record validation | Implemented | T-01, T-03, T-06, T-08 |
| Peer-store monotonic sequence | Implemented | T-01, T-06 |
| Connection limits (64/32/32/2) | Implemented | T-01, T-05 |
| Per-type payload size limits | Implemented | T-01, T-05, T-010 |
| Frame checksum + termination on mismatch | Implemented | T-010 |
| Session nonce replay protection | Implemented | T-06 |
| Wrong-network rejection | Implemented | T-06 |
| Key cleansing + mode-0600 storage | Implemented | T-07 |
| Multiple bootstrap sources | Implemented | T-03, T-08 |
| Peer lifecycle circuit breaker | Implemented | T-01, T-04 |
| Peer exchange | Implemented | T-03 |
| Sybil resistance | Not implemented (OPEN RESEARCH) | T-02 |
| Byzantine consensus | Not implemented (OPEN RESEARCH) | T-011 |
| Traffic analysis resistance | Not implemented (OPEN RESEARCH) | T-09 |
| Per-peer rate limiting | Not implemented (planned) | T-01, T-05 |
| Transaction replay across forks | Not implemented (OPEN RESEARCH) | T-06 |
| Key revocation / HSM storage | Not implemented (planned) | T-07 |
| Test vectors / conformance harness | Not implemented (planned) | T-013 |

## 5. Residual Risk Statement

FST does not claim production security or full Sybil resistance. The most significant
unmitigated threats are Sybil attacks (T-02), Byzantine participants (T-011), and
traffic analysis/metadata exposure (T-09), all of which depend on OPEN RESEARCH
decisions (D-016, D-017, D-020). Network-layer threats (T-01, T-03, T-05, T-06, T-07,
T-08, T-010, T-012) are partially mitigated by existing implementation but retain
residual risks requiring engineering work.

## 6. Cross-References

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope; fork
  protection requirements (D-036); offline operation (D-018).
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) —
  Approved engineering principles (P1–P14).
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision
  classifications (D-015, D-016, D-017, D-018, D-019, D-020, D-023, D-024, D-025,
  D-035, D-036).
- [`../spec/core-protocol-specification-v0.1.md`](../spec/core-protocol-specification-v0.1.md)
  — Core protocol specification (this milestone).
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) —
  FeinStatic baseline and transformation findings.
- [`../architecture/node-identity-peer-discovery-implementation.md`](../architecture/node-identity-peer-discovery-implementation.md) —
  Node identity, signed node records, peer store, bootstrap discovery, peer exchange.
- [`../architecture/secure-sessions.md`](../architecture/secure-sessions.md) — TLS 1.3
  secure session design (Milestone 2D).
- [`../architecture/message-dispatch.md`](../architecture/message-dispatch.md) — Message
  dispatch protocol (Milestone 2D).
- [`../architecture/peer-admission.md`](../architecture/peer-admission.md) — Dynamic
  peer admission (Milestone 2D).
- [`../architecture/node-roles.md`](../architecture/node-roles.md) — Node role
  architecture (this milestone).
- [`../research/research-register.md`](../research/research-register.md) — Research
  register (RS-001 through RS-014).
- [`../security/security-research-register.md`](../security/security-research-register.md)
  — Security research register.
- [`../glossary.md`](../glossary.md) — FST glossary of terms.

## 7. Open Questions

1. What Sybil-resistance mechanism will gate permissionless participation? (OPEN RESEARCH,
   RS-002, D-016)
2. What consensus algorithm provides Byzantine fault tolerance? (OPEN RESEARCH, RS-001,
   D-017)
3. What transaction-level replay protection prevents cross-fork replay? (OPEN RESEARCH,
   D-036 mechanisms)
4. What privacy mechanisms protect against traffic analysis? (OPEN RESEARCH, RS-003,
   D-020)
5. What key storage backend (HSM/TPM/enclave) replaces file-based storage? (Unresolved)
6. What key compromise detection and revocation mechanism exists? (Unresolved)
7. What per-peer rate limits and behavior scoring are used? (Unresolved)
8. What test vectors and conformance harness ensure interoperability? (Unresolved, RS-012,
   D-035)
