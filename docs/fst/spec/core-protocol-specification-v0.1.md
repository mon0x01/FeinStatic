# FST Core Protocol Specification — v0.1

**Status:** DRAFT — initial versioned protocol specification.
**Classification:** PROVISIONAL (preferred architectural direction requiring validation;
not an approved production design).
**Milestone:** FST Milestone 2E — Core Protocol Specification and Node Architecture.
**Related decisions:** D-016, D-017, D-018, D-019, D-020, D-021, D-024, D-035, D-036.
**Related research:** RS-001 through RS-014.

This document specifies the FST core peer-to-peer protocol at a level sufficient for an
independent implementation team to understand the intended protocol boundaries. It does
not select a consensus algorithm, transaction model, block format, finality rule, or
state-transition semantics, all of which remain OPEN RESEARCH (decision D-017, D-015,
scope-v0.1.md).

Where the existing FeinStatic implementation (commit `7e9dda7`) provides an engineering
baseline, that baseline is referenced as **established implementation**. Where a design
direction exists but is not yet validated, it is marked **proposed specification**. Where
no decision has been made, it is marked **unresolved design**.

## 1. Protocol Goals and Scope

### 1.1 Goals

- Define a versioned, transport-independent peer-to-peer protocol for the FST financial
  network.
- Support independent, forkable implementations maintained by independent teams
  (principles P1, P4, P10).
- Permit participants to reject upgrades without losing access (principle P5).
- Protect against replay across network forks (decision D-036, APPROVED).
- Avoid any central administrator, master key, or unilateral protocol authority
  (principles P1, P2, P8).
- Provide a clear boundary between network-layer concerns (identity, discovery,
  session, message framing) and application-layer concerns (consensus, transactions,
  blocks, state transitions).

### 1.2 Non-goals (explicitly out of scope)

- Consensus algorithm selection or implementation (OPEN RESEARCH, D-017).
- Transaction serialization format (OPEN RESEARCH).
- Block format and block header structure (OPEN RESEARCH).
- Finality rules and fork-choice rules (OPEN RESEARCH).
- State-transition semantics and monetary policy (OPEN RESEARCH, D-015).
- Wallet design, key custody, or account/UTXO model selection (OPEN RESEARCH).
- Validator membership, selection, or slashing (OPEN RESEARCH).

## 2. Network Identity and Genesis/Network Separation

### 2.1 Network identity

Every independent FST network has a unique **network identifier** — a 32-byte value
derived from the network's genesis or a network-specific salt. The network identifier is
bound into:

- Signed node records (`SignedNodeRecord.network_identifier`, established implementation).
- Secure session context (`SecureSessionContext.network_identifier`, established
  implementation).
- Application-level handshake messages (proposed specification).

**Established implementation:** The network identifier is passed to `SecureSessionContext`
and stored in signed node records. Records with a mismatching network identifier are
rejected (wrong-network rejection, `validate_record` rule 3).

**Proposed specification:** The network identifier format and genesis derivation remain
unresolved. Candidates include a SHA-256 hash of a genesis document, a network-specific
constant, or a hash of genesis block contents. No format is selected.

**Unresolved design:** Whether the network identifier is immutable for the life of a
network, or may change across hard forks. Fork protection requirements (D-036, APPROVED)
require distinct identifiers per independent network, but the migration path for an
existing network changing its identifier is unresolved.

### 2.2 Node identity

Nodes are identified by a **NodeId**, a self-certifying identifier derived from the
canonical public key material:

- **v1 format:** `"fn1" + hex(sha256(public_key))` (established implementation, backward
  compatible with `peer_id_from_public_key()` in `src/network/peer_identity.cpp:320`).
- Versioned prefix (`"fn1"`) permits future formats (e.g., `"fn2"`) without breaking the
  identity abstraction (established implementation).
- `NodeId::matches(public_key)` verifies public key / identifier correspondence
  (established implementation).

**Established implementation:** `include/feinstat/network/identity.hpp`,
`src/network/identity.cpp` implement `NodeId`, `KeyProvider`, `Ed25519KeyProvider`,
`FileKeyProvider`, and `RotationRecord`.

**Proposed specification:** None beyond the established v1 format.

**Unresolved design:** Post-quantum key algorithm agility, key rotation lifecycle beyond
`RotationRecord`, and identity recovery after key compromise.

### 2.3 Signed node record

Nodes publish a **SignedNodeRecord** — a canonical, signed, versioned record asserting
identity and reachability (established implementation).

Fields (established implementation, `include/feinstat/network/node_record.hpp`):

| Field | Type | Description |
|-------|------|-------------|
| `format_version` | `uint8` | Record format version (1 = current) |
| `network_identifier` | `Bytes[32]` | Network/genesis hash for fork protection |
| `node_id` | `NodeId` | Self-certifying node identifier |
| `key_algorithm` | `uint8` | Public-key algorithm (1 = Ed25519) |
| `public_key` | `PublicKey[32]` | Ed25519 public key |
| `sequence` | `uint64` | Monotonic sequence number per node ID |
| `protocol_versions` | `vector<uint16>` | Supported protocol versions |
| `capabilities` | `vector<string>` | Advertised capabilities |
| `endpoints` | `vector<NodeEndpoint>` | Network transport addresses |
| `expiry_unix_ms` | `uint64` | Expiration timestamp (0 = no expiry) |
| `signature` | `Signature[64]` | Ed25519 signature over domain-separated canonical body |

**Canonical serialization** (established implementation): `ByteWriter`/`ByteReader` in a
fixed, deterministic order; signature covers `canonical("FST Signed Node Record v1") ||
canonical_body`.

**Validation rules** (established implementation, `validate_record`):

1. Unsupported critical-version rejection: `format_version` must be 1.
2. Unsupported key algorithm rejection: `key_algorithm` must be 1 (Ed25519).
3. Wrong-network rejection: `network_identifier` must match the expected network.
4. Node ID / public key correspondence: `node_id.matches(public_key)` must hold.
5. Expired-record rejection: `expiry_unix_ms` must be in the future (0 = no expiry).
6. Stale-sequence rejection: `sequence` must be strictly greater than the last accepted
   sequence for the node ID.
7. Invalid-signature rejection: Ed25519 signature must verify over the domain-separated
   canonical body.

**Strict bounds** (established implementation):

| Limit | Value |
|-------|-------|
| Maximum record size | 64 KiB |
| Maximum endpoints per record | 16 |
| Maximum capabilities per record | 32 |
| Maximum protocol versions per record | 16 |
| Maximum capability string length | 64 bytes |
| Maximum host string length | 253 bytes |

**Security invariant** (established implementation): A signed node record never grants
validator status, account authority, or permission to transfer funds. It is solely a
claim about node identity and reachability.

**Unresolved design:** Record renewal / re-issuance before expiry, revocation mechanism
for compromised records, and whether records may carry application-layer metadata beyond
capabilities and endpoints.

## 3. Message Envelope and Framing Conventions

### 3.1 Frame format

All messages over authenticated peer sessions use a length-prefixed frame (established
implementation, `docs/fst/architecture/message-dispatch.md`):

| Field | Size | Description |
|-------|------|-------------|
| Length | 4 bytes | Big-endian payload length |
| Type | 2 bytes | Message type (`PeerMessageType`) |
| Checksum | 32 bytes | SHA-256 of payload |
| Payload | Length bytes | Message payload |

Maximum payload: 1 MiB per message (proposed specification; existing code enforces
per-type limits but no global cap is visible in `message_dispatch.cpp`).

**Established implementation:** `PeerMessageType` enum in
`include/feinstat/network/message_dispatch.hpp`; `MessageDispatcher::send` and
`MessageDispatcher::dispatch` implement frame send/receive over `PeerSession`.

**Proposed specification:** A global maximum payload of 1 MiB; per-type limits as
specified in Section 7.

**Unresolved design:** Frame compression, framing over non-TCP transports (DTN, radio,
satellite), and length-prefix endianness for future transport adapters.

### 3.2 Session framing requirements

- All messages are sent only over authenticated sessions (established implementation:
  `MessageDispatcher::send`/`dispatch` reject unauthenticated sessions with
  `errc::not_a_stream`).
- Malformed or unsupported message types are rejected (established implementation).
- Checksum mismatch → connection terminated (established implementation).
- Oversized payloads → connection terminated (established implementation).

## 4. Protocol Versioning and Compatibility

### 4.1 Protocol version

The protocol version is a `uint16` value. The current baseline version is **8** (inherited
from FeinStatic protocol v8, established implementation: `MessageDispatcher::Config`
defaults `protocol_version{8}`).

**Established implementation:** Protocol version is carried in `SecureHandshakeResult`,
`SecureSessionContext`, `SignedNodeRecord.protocol_versions`, and
`MessageDispatcher::Config`.

**Proposed specification:** Version negotiation occurs during the peer handshake
(Section 5). Both peers advertise supported versions; the session uses the highest
mutually supported version. A peer that advertises no mutually supported version MUST
not establish an application session.

### 4.2 Compatibility rules

- **Same major version:** Backward-compatible within the same major version; new message
  types may be added and ignored by older peers.
- **Major version change:** Incompatible; nodes running different major versions MUST
  not establish application sessions (proposed specification).
- **Unknown message types:** Received but not acted upon; may be logged and ignored
  (proposed specification).
- **Unknown capabilities:** Advertised but not required; a peer MUST NOT disconnect
  solely because a peer advertises an unknown capability (proposed specification).

**Unresolved design:** Major version numbering scheme, deprecation policy, and minimum
supported version for mainnet.

### 4.3 Version upgrade signaling

Upgrade signaling is a mechanism for peers to advertise readiness for a protocol upgrade
without forcing it on others (principle P5 — participants can reject upgrades).

**Unresolved design:** The upgrade signaling mechanism (e.g., version bits in node
records, capability strings, or a dedicated upgrade message) is OPEN RESEARCH
(governance and on-chain upgrades, RS-010; D-010 APPROVED — participants can reject
upgrades). No signaling format is selected.

## 5. Peer Handshake and Authenticated Session Requirements

### 5.1 Handshake overview

The peer handshake establishes an authenticated, encrypted session between two nodes
before any application messages are exchanged (established implementation, Milestone 2D).

Handshake steps (established implementation):

1. **Transport connection:** TCP dial/accept (TLS transport adapter; future transports
   via adapter interface, proposed specification).
2. **TLS 1.3 mutual authentication:** OpenSSL 3.x via Boost.Asio SSL; TLS 1.2 and
   earlier disabled; self-signed X.509 certificate, no central CA
   (established implementation, `docs/fst/architecture/secure-sessions.md`).
3. **Application-level binding exchange:** After TLS completes, both peers exchange
   NodeId, SignedNodeRecord, TLS certificate DER, TLS certificate hash, and binding
   signature (Ed25519 over `SHA-256(TLS_cert_DER)`, domain-separated `"FST TLS Binding
   v1"`) (established implementation).
4. **Peer verification:** Each peer verifies TLS session credential control, binding to
   FST NodeId, correct network identity, correct peer-record identity, and protocol
   version compatibility (established implementation).
5. **Session registration:** On success, the peer is registered as verified in
   `PeerStore` via `StorePeerAuthenticator` (established implementation).

### 5.2 Authenticated session requirements

- **Mutual authentication:** Both sides present and verify credentials (established
  implementation).
- **Forward secrecy:** TLS 1.3 ephemeral key exchange (established implementation).
- **No central CA:** Self-signed certificates with cryptographic binding to node
  identity (established implementation).
- **Replay protection:** TLS 1.3 sequence numbers plus application-level session nonce
  (`SecureHandshakeResult.session_nonce`) (established implementation).
- **Certificate verification is mandatory:** No arbitrary certificate acceptance
  (established implementation).

### 5.3 Session lifecycle

- **Established:** Handshake complete, peer verified, application messages permitted.
- **Active:** Messages being exchanged; `last_seen_unix_ms` updated on activity
  (established implementation via `PeerSession::touch`).
- **Idle:** No recent activity; connection remains open.
- **Closing:** `kDisconnect` message sent, then TCP/TLS close (established
  implementation via `MessageDispatcher::disconnect`).
- **Failed:** Handshake or validation failure; connection closed, failure recorded,
  reconnection scheduled with bounded backoff (established implementation via
  `PeerLifecycle` circuit breaker).

**Unresolved design:** Session resumption, session migration across transports, and
session idle timeout values.

## 6. Capability Negotiation

### 6.1 Capability advertisement

Nodes advertise capabilities in their signed node record (`capabilities` field,
established implementation) and during the handshake (`SecureHandshakeResult.capabilities`,
established implementation).

**Established implementation:** `MessageDispatcher::Config` carries a `capabilities`
vector; `kCapabilityAdvertise` (type 10) and `kCapabilityAck` (type 11) message types
are defined but no handler logic is implemented.

**Proposed specification:** Capabilities are strings of maximum 64 bytes (established
bound). Examples: `"tx-gossip"`, `"block-relay"`, `"light-client-proxy"`,
`"archival-storage"`. Capabilities describe what a peer is willing to do, not what it is
authorized to do.

### 6.2 Negotiation rules

- A peer MUST NOT require a capability it does not itself advertise (proposed
  specification).
- Unknown capabilities are ignored (proposed specification, Section 4.2).
- Capability advertisement does not confer validator status or financial authority
  (proposed specification; consistent with the signed node record security invariant).

**Unresolved design:** Capability registry, standardized capability names, and
capability compatibility matrices.

## 7. Message Categories and Message Lifecycle

### 7.1 Message categories

The following message categories are defined. Baseline types (established
implementation) are marked; application-layer types are proposed or unresolved.

#### Session and handshake messages (established implementation)

| Type | Value | Description | Status |
|------|-------|-------------|--------|
| kReserved | 0 | Session handshake (internal) | Established |
| kProtocolNegotiate | 1 | Protocol version negotiation | Established |
| kProtocolNegotiateAck | 2 | Protocol negotiation ack | Established |
| kNodeIdentity | 3 | Node identity exchange | Established |
| kNodeIdentityAck | 4 | Node identity ack | Established |
| kDisconnect | 12 | Graceful disconnect | Established |
| kError | 255 | Error / rejection | Established |

#### Peer health and discovery messages (established implementation)

| Type | Value | Description | Status |
|------|-------|-------------|--------|
| kPing | 5 | Peer health check | Established |
| kPong | 6 | Ping response | Established |
| kPeerRecordAnnounce | 7 | Announce a peer record | Established |
| kPeerRecordRequest | 8 | Request peer records | Established |
| kPeerRecordResponse | 9 | Peer records response | Established |

#### Capability messages (established types, unimplemented handlers)

| Type | Value | Description | Status |
|------|-------|-------------|--------|
| kCapabilityAdvertise | 10 | Capability advertisement | Established type |
| kCapabilityAck | 11 | Capability ack | Established type |

#### Application-layer messages (unresolved)

| Category | Description | Status |
|----------|-------------|--------|
| Transaction | Transaction submission and dissemination | Unresolved (transaction model OPEN RESEARCH) |
| Block | Block propagation and state sync | Unresolved (block format OPEN RESEARCH) |
| State proof | Light-client state proofs | Unresolved (RS-009 OPEN RESEARCH) |
| Consensus | Consensus voting and agreement | Unresolved (D-017 OPEN RESEARCH) |

### 7.2 Message lifecycle

1. **Creation:** An application or network component constructs a message payload.
2. **Framing:** The payload is framed per Section 3.1 (length + type + checksum +
   payload).
3. **Transmission:** Sent over an authenticated session via `MessageDispatcher::send`.
4. **Receipt:** Received via `MessageDispatcher::dispatch`, which validates size,
   checksum, and type.
5. **Handling:** Dispatched to a registered handler, or rejected if no handler exists
   (established implementation returns `errc::protocol_error` for unhandled types).
6. **Acknowledgment (where applicable):** Some message types define explicit acks
   (e.g., `kProtocolNegotiateAck`, `kCapabilityAck`); others are fire-and-forget.

### 7.3 Per-message size limits (proposed specification)

| Message type | Maximum payload |
|--------------|-----------------|
| kPing | 64 bytes |
| kPeerRecordAnnounce / kPeerRecordResponse | 64 KiB |
| kCapabilityAdvertise | 256 bytes |
| kError | 1024 bytes |
| All other types | 64 KiB |
| Global frame cap | 1 MiB |

These match the existing `MessageDispatcher::validate_message` limits (established
implementation) plus a global cap (proposed specification).

## 8. Transaction Dissemination Boundaries

### 8.1 Boundary statement

Transaction dissemination is an application-layer concern. This specification defines
only the network-layer boundaries within which transaction messages may propagate;
it does not define transaction format, validity rules, fee markets, or mempool policy
(transaction model OPEN RESEARCH, RS-002/RS-003).

### 8.2 Dissemination rules (proposed specification)

- Transactions are carried as application-layer message payloads over authenticated
  sessions (proposed).
- A node MUST NOT forward a transaction it has not validated for syntactic well-formedness
  according to the local transaction model (proposed; validation rules unresolved).
- Transaction propagation scope (gossip vs. direct delivery to proposers/validators) is
  unresolved (transaction dissemination component of consensus taxonomy, RS-013
  Section 2.5).
- Transaction privacy (confidentiality, selective disclosure) is PROVISIONAL
  (D-020); specific mechanisms are OPEN RESEARCH (RS-003).

### 8.3 What is NOT specified

- Transaction serialization format (unresolved).
- Account vs. UTXO model (unresolved, D-016/D-015).
- Fee market and prioritization (unresolved).
- Mempool size, eviction, and replacement policy (unresolved).
- Replay protection at the transaction level (nonce or alternative; unresolved;
  network-level replay protection exists via session nonce and node-record sequence).

## 9. Block and State-Data Dissemination Boundaries

### 9.1 Boundary statement

Block and state-data dissemination are application-layer concerns. This specification
defines only network-layer boundaries; block format, finality, and state-transition
semantics remain OPEN RESEARCH (D-017, D-015).

### 9.2 Dissemination rules (proposed specification)

- Blocks and state data are carried as application-layer message payloads over
  authenticated sessions (proposed).
- Block propagation scope (gossip vs. direct delivery) is unresolved
  (agreement/ordering component, RS-013 Section 2.6).
- State synchronization for joining/rejoining nodes uses snapshot and streaming patterns
  preserved from FeinStatic as reference (D-032 PROVISIONAL); exact format is
  unresolved.
- Light-client state proofs (Merkle commitments, sync protocols) are OPEN RESEARCH
  (RS-009).

### 9.3 What is NOT specified

- Block format and header structure (unresolved).
- Finality rules and fork-choice (unresolved).
- State commitment scheme (unresolved).
- Pruning and archival policies (unresolved; addressed in node-roles.md).

## 10. Error Handling and Malformed-Message Rejection

### 10.1 Error categories

| Category | Condition | Action |
|----------|-----------|--------|
| Transport error | TLS handshake failure, connection reset | Close connection; record failure; bounded backoff reconnect |
| Authentication error | Binding signature invalid, network ID mismatch | Reject handshake; close connection |
| Framing error | Checksum mismatch, oversized payload, invalid length | Terminate connection |
| Protocol error | Unhandled message type, malformed payload | Return `errc::protocol_error`; may close session |
| Application error | Invalid transaction/block (application-layer) | Reject message; do not forward (proposed) |

### 10.2 Rejection behavior (established implementation)

- `MessageDispatcher::send`/`dispatch` reject unauthenticated sessions
  (`errc::not_a_stream`).
- `validate_message` returns non-zero for oversized payloads (`errc::message_size`).
- Unhandled message types return `errc::protocol_error`.
- Checksum mismatch and oversized payloads terminate the connection
  (`docs/fst/architecture/message-dispatch.md`, Section "Validation").

### 10.3 Error message format (proposed specification)

`kError` (type 255) carries a payload with:
- Error code (`uint16`)
- Human-readable description (UTF-8, max 1024 bytes)
- Optional offending message type (`uint16`)

Error messages MUST NOT reveal internal implementation details, stack traces, or key
material (proposed specification; consistent with P9 — security from protocol design).

**Unresolved design:** Standardized error code registry, error message localization, and
whether persistent peers are temporarily banned after repeated malformed messages.

## 11. Replay Protection and Network-Fork Separation

### 11.1 Network-fork separation (D-036, APPROVED)

Every independent network MUST have a unique network identity. The network identifier is
bound into signed node records and secure session contexts. Nodes MUST reject records
and sessions from a different network (wrong-network rejection, established
implementation).

**Fork protection requirements** (APPROVED, scope-v0.1.md Section "Fork protection
requirements"):

1. Every independent network must have a unique network identity.
2. Transactions must be protected against replay across networks.
3. Independent currencies must have distinct identifiers.

**Implementation mechanisms** (chain-ID format, signing scheme, token symbols) remain
OPEN RESEARCH (scope-v0.1.md).

### 11.2 Replay protection (established implementation)

- **Node-record level:** Monotonic sequence numbers prevent record rollback
  (stale-sequence rejection, rule 6).
- **Session level:** Application-level session nonce (`SecureHandshakeResult.session_nonce`)
  bound to each TLS session (established implementation).
- **Transport level:** TLS 1.3 sequence numbers (established implementation).

### 11.3 Transaction replay across networks (unresolved)

Transaction-level replay protection (chain ID in transaction signature, nonce, or
alternative) is OPEN RESEARCH (transaction model, RS-002/RS-003). No mechanism is
selected.

## 12. Upgrade Signaling Requirements and Compatibility Expectations

### 12.1 Requirements (principle P5, APPROVED)

Participants MUST retain the ability to reject proposed upgrades without losing access to
the network. Upgrade signaling MUST NOT confer unilateral authority to any participant or
organization (principles P1, P2, P8).

### 12.2 Compatibility expectations (proposed specification)

- Nodes running different major protocol versions MUST not establish application
  sessions (Section 4.2).
- Nodes running the same major version but different minor versions MAY establish
  sessions, using the highest mutually supported minor version.
- New message types and capabilities MUST be ignorable by older peers within the same
  major version.

### 12.3 Upgrade mechanism (unresolved)

The upgrade signaling mechanism is OPEN RESEARCH (RS-010, governance and on-chain
upgrades; D-010 APPROVED — participants can reject upgrades). Candidates include version
bits in node records, capability strings, or a dedicated upgrade message. No mechanism is
selected. Fork legitimacy (P6, P7) means no fork has exclusive legitimacy after an
upgrade disagreement.

## 13. Extensibility for Future Transports

### 13.1 Transport independence (D-019, PROVISIONAL)

Network identity, key management, and peer records MUST NOT depend on TCP, IP, or any
specific transport (established design principle; `NodeId` contains no address, port, or
transport hint). Node records carry transport-typed endpoints (TCP, QUIC, mesh, radio,
satellite, DTN) (established implementation).

### 13.2 Transport adapter interface (proposed specification)

Future transport adapters (QUIC, mesh radio, satellite, DTN/store-and-forward) SHALL
implement a common interface providing:
- Authenticated session establishment (binding to TLS 1.3 or equivalent secure channel).
- Frame send/receive per Section 3.1.
- Endpoint address serialization for `NodeEndpoint`.

No transport adapter beyond TCP/TLS is implemented (established implementation;
`BootstrapSource` interface keeps discovery transport-independent).

### 13.3 Delay- and disruption-tolerant networking (unresolved)

DTN support (store-and-forward, intermittent connectivity) is OPEN RESEARCH
(communication domain, RS-005; D-019 PROVISIONAL). Consensus protocols assume reliable
point-to-point channels; adaptation to store-and-forward links is unresolved
(RS-013 Section 8.10).

## 14. Summary of Classification

| Element | Classification | Basis |
|---------|---------------|-------|
| Network identifier concept | Proposed specification | D-036 APPROVED; format unresolved |
| NodeId v1 format | Established implementation | `identity.cpp` |
| SignedNodeRecord format and validation | Established implementation | `node_record.cpp` |
| Frame format (length + type + checksum + payload) | Established implementation | `message_dispatch.cpp` |
| PeerMessageType enum (baseline types) | Established implementation | `message_dispatch.hpp` |
| TLS 1.3 mutual auth + node binding | Established implementation | `secure_session.cpp` |
| Peer handshake flow | Established implementation | `network_runtime.cpp`, `secure_session.cpp` |
| Capability advertisement (types, unimplemented handlers) | Established types | `message_dispatch.hpp` |
| Capability negotiation rules | Proposed specification | This document |
| Transaction dissemination | Unresolved | Transaction model OPEN RESEARCH |
| Block/state dissemination | Unresolved | Block format OPEN RESEARCH |
| Error handling and malformed rejection | Established + proposed | `message_dispatch.cpp` + this document |
| Replay protection (session + node-record) | Established implementation | `secure_session.hpp`, `node_record.cpp` |
| Transaction replay across networks | Unresolved | D-036 mechanisms OPEN RESEARCH |
| Upgrade signaling | Unresolved | RS-010 OPEN RESEARCH |
| Transport extensibility | Proposed specification | D-019 PROVISIONAL |

## 15. Cross-References

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope; fork
  protection requirements (D-036).
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) —
  Approved engineering principles (P1–P14).
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision
  classifications (D-015, D-016, D-017, D-018, D-019, D-020, D-024, D-035, D-036).
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) —
  FeinStatic baseline and transformation findings.
- [`../architecture/node-identity-peer-discovery-implementation.md`](../architecture/node-identity-peer-discovery-implementation.md) —
  Node identity, signed node records, peer store, bootstrap discovery, peer exchange.
- [`../architecture/secure-sessions.md`](../architecture/secure-sessions.md) — TLS 1.3
  secure session design (Milestone 2D).
- [`../architecture/message-dispatch.md`](../architecture/message-dispatch.md) — Message
  dispatch protocol and frame format (Milestone 2D).
- [`../architecture/peer-admission.md`](../architecture/peer-admission.md) — Dynamic
  peer admission (Milestone 2D).
- [`../architecture/node-roles.md`](../architecture/node-roles.md) — Node role
  architecture (this milestone).
- [`../security/threat-model-v0.1.md`](../security/threat-model-v0.1.md) — Network
  threat model (this milestone).
- [`../research/research-register.md`](../research/research-register.md) — Research
  register (RS-001 through RS-014).
- [`../security/security-research-register.md`](../security/security-research-register.md)
  — Security research register.
- [`../glossary.md`](../glossary.md) — FST glossary of terms.

## 16. Open Questions

1. What is the network identifier format and genesis derivation? (OPEN RESEARCH)
2. What is the transaction serialization format? (OPEN RESEARCH)
3. What is the block format and header structure? (OPEN RESEARCH)
4. What are the finality and fork-choice rules? (OPEN RESEARCH, D-017)
5. What is the state-transition semantics / monetary policy? (OPEN RESEARCH, D-015)
6. What is the upgrade signaling mechanism? (OPEN RESEARCH, RS-010)
7. What is the transaction-level replay protection across forks? (OPEN RESEARCH)
8. What transport adapters beyond TCP/TLS are required? (OPEN RESEARCH, D-019)
9. What is the DTN/store-and-forward adaptation strategy? (OPEN RESEARCH, RS-005)
10. What is the capability registry and standardized capability names? (Proposed;
    unresolved)
