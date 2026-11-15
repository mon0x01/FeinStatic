# FST Identity, Peer Discovery and P2P Network Architecture Research

**Research ID:** RS-014 (PROPOSED — see research-register.md)
**Status:** IMPLEMENTATION BEGUN (research document; implementation in Milestone 2C)
**Milestone:** FST Milestone 2B — Foundational architecture research; implemented in Milestone 2C
**Scope:** Identity model, peer discovery, network connectivity foundations
**Constraint:** The research document remains a specification reference. Implementation code
is in `src/network/identity.cpp`, `src/network/node_record.cpp`, `src/network/peer_store.cpp`,
`src/network/bootstrap.cpp`, and `src/network/peer_exchange.cpp`. Research is not marked
complete; the Noise handshake pattern, DHT adoption, and post-quantum agility remain OPEN
RESEARCH.
**Classification:** PROVISIONAL — preferred architectural direction requiring validation;
not an approved production design. No new project decision is approved by this milestone.

---

## 1. Executive architectural findings

FST requires a modular, permissionless, multi-transport peer-to-peer network layer that
is independent of the unselected consensus mechanism (D-017, OPEN RESEARCH) and of the
provisional permissionless validator participation model (D-016). The network layer must
not introduce any unilateral control point (P1, P2, P8), must survive the disappearance of
founding developers (P10), and must permit participants to reject upgrades (P5).

Existing FeinStatic v2.1.0 networking is a **permissioned, crash-fault, authenticated TCP**
prototype (protocol v8) with pre-provisioned peer rosters, an operator trust store, and no
peer discovery. It is architecturally incompatible with FST's permissionless,
multi-transport vision but contains reusable cryptographic primitives, peer-lifecycle
engineering patterns, and authenticated-frame construction that can be selectively reused
(D-025, PROVISIONAL).

The recommended architectural direction is a **modular, transport-independent network
core** built around:

1. A seven-domain identity model (account, node, validator, session, service, human,
   endpoint) that strictly separates financial control from network identity.
2. A hybrid discovery architecture combining multiple independent bootstrap sources,
   authenticated signed node records, peer exchange, and optional local discovery — with
   no single trusted discovery authority.
3. A Noise-protocol-inspired secure channel for session establishment, evaluated
   independently from peer discovery and node identity.
4. Clean component boundaries permitting future adapters for internet, TCP/QUIC, mesh,
   radio, satellite, and store-and-forward/delay-tolerant transports.

All designs in this document are PROVISIONAL and require independent technical review,
threat modeling, and prototype validation before implementation.

---

## 2. FST-specific requirements and constraints

### 2.1 Approved principles and decisions that bind this architecture

| ID | Classification | Requirement | Source |
|----|----------------|-------------|--------|
| D-001 | APPROVED | Independent global financial network; not a bank plugin | scope-v0.1.md |
| D-002/D-003 | APPROVED | No unilateral organisational control; no permanent privileged protocol authority | principles P1, P2 |
| D-005 | APPROVED | Independent teams must be able to maintain the software | principles P4 |
| D-006 | APPROVED | Participants can reject proposed upgrades | principles P5 |
| D-008 | APPROVED | No central emergency administrator or unilateral master key | principles P8 |
| D-010 | APPROVED | Network survives disappearance of founding developers | principles P10 |
| D-011 | APPROVED | Research-first engineering for every module | principles P12 |
| D-016 | PROVISIONAL | Permissionless validator participation; exact mechanism unresolved | scope-v0.1.md |
| D-017 | OPEN RESEARCH | BFT research direction; no consensus algorithm selected | scope-v0.1.md |
| D-018 | PROVISIONAL | Offline-payment architecture is a separate research concern | scope-v0.1.md |
| D-019 | PROVISIONAL | Modular multi-transport communication (internet, mesh, radio, satellite, DTN) | scope-v0.1.md |
| D-020 | PROVISIONAL | Strong cryptographic privacy direction; mechanisms unresolved | scope-v0.1.md |
| D-025 | PROVISIONAL | FeinStatic TCP networking patterns reusable; new P2P architecture to be researched | decision-register.md |
| D-036 | APPROVED | Fork protection: unique network identity, replay protection, distinct currency identifiers | scope-v0.1.md |

### 2.2 Architectural constraints

- **No implementation of consensus, wallets, monetary policy, or payment logic** in this milestone (scope-v0.1.md out-of-scope list).
- **No modification of existing C++, headers, CMake, tests, or runtime files** (AGENTS.md preservation rules).
- **Identity must not double as Sybil resistance.** A public key or node ID authenticates a participant; it does not limit how many identities an adversary can create. Sybil resistance is a separate, unresolved research question (D-016, D-017).
- **Transport independence.** Network identity, key management, and peer records must not depend on TCP, IP, or any specific transport.
- **No central discovery authority.** Bootstrap services provide connectivity assistance only, not membership or consensus authority.
- **Relays are not trusted financial intermediaries.** Relay traffic must remain end-to-end authenticated and encrypted; relays must not learn transaction contents or be able to forge sessions.
- **Fork protection (D-036).** Network identity must be distinct per independent network; nodes must not conflate identities across forks.

### 2.3 What must be common across all FST clients vs implementation-specific

| Common (protocol-level, all clients) | Implementation-specific (per client) |
|---|---|
| Identity domain model and separation rules | Key storage backend (file, HSM, TPM, enclave) |
| Node-ID derivation from public key | Cryptographic library choice (OpenSSL, BoringSSL, libsodium, Rust crypto) |
| Signed node-record format and validation rules | Transport adapter implementation (TCP, QUIC, mesh radio) |
| Secure-channel handshake properties (mutual auth, forward secrecy, replay protection) | Concurrency model (thread-per-connection, io_uring, async) |
| Peer-record lifecycle states and expiry semantics | Peer-health scoring weights and thresholds |
| Bootstrap-source interface and minimum trusted assumptions | Inbound admission policy constants |
| Fork/network identity separation and replay scope | CLI and operator tooling |
| End-to-end authentication across relays | Local discovery mechanism (mDNS, LAN broadcast) |

---

## 3. Identity-domain model and trust relationships

FST distinguishes **seven identity domains**. Conflating any two is a design defect.
Each domain has its own key material, lifecycle, trust roots, and rotation policy.

### 3.1 The seven identity domains

| Domain | Purpose | Key material | Lifetime | Trust root |
|---|---|---|---|---|
| Account identity | Control over financial assets and transactions | Signing key (e.g. Ed25519) | Long-term; user-controlled | User passphrase / key store |
| Node identity | Identifies a running FST network participant | Long-term node key | Long-term per node; rotatable | Node key file / vault |
| Validator identity | Participation in future validator selection / consensus | Validator signing key | Tied to validator tenure; open research (D-016/D-017) | Consensus-specific (unselected) |
| Session identity | Authenticated network connection; ephemeral material | Ephemeral key pair per session | Single connection or bounded window | Ephemeral; bound to node identity at handshake |
| Service identity | Network services/capabilities offered by a node | Service key or derived sub-identity | Operational; rotatable | Node identity delegates to service |
| Human/organisation identity | Optional external compliance layer | External credential (DID, certificate, etc.) | Optional; not required by base protocol | External authority (optional) |
| Network endpoint | Mutable reachable address (IP:port, onion, radio bearer) | None (address, not key) | Ephemeral; changes with topology | Observed reachability, not identity |

### 3.2 Relationships and separation rules

- **Account identity is independent of node identity.** One person may operate many nodes
  with one account, or many accounts on one node. Financial control must never be
  derivable from a node ID, and vice versa.
- **Validator identity is independent of node identity.** A validator may run multiple
  nodes; a node may host multiple validators (future research). Validator keys must not be
  conflated with node keys because validator selection, slashing, and rotation are
  consensus-layer concerns (OPEN RESEARCH under D-017).
- **Session identity is ephemeral and bound to node identity at handshake.** The session
  key proves possession of the node key during the handshake, then encrypts traffic. The
  session key is never a long-term identity.
- **Service identity delegates from node identity.** A node may advertise services (relay,
  bootstrap, light-client sync) under sub-identities derived from or authorized by its node
  key.
- **Human/organisation identity is optional.** The base protocol must not require any
  external identity. Compliance layers (if any) sit above the protocol and are out of
  scope for this milestone (D-018 offline operation is separate; compliance is RS-011).
- **Network endpoint is not identity.** Endpoints are mutable, observer-relative, and
  unverifiable as identity. A node moving from IP A to IP B retains the same node ID.

### 3.3 Public-key-derived identities and self-certifying identifiers

- Node IDs are derived from the node public key: `node_id = "fn1" || hex(sha256(pubkey))`.
  This is the existing FeinStatic pattern (peer_id_from_public_key in
  `src/network/peer_identity.cpp:320`), preserved as a reference component (D-025).
- Self-certifying: possession of the corresponding secret key proves ownership of the ID.
  The ID itself carries no authority; trust is established through signed records and
  handshake authentication, not through ID possession alone.
- Algorithm agility: the hash function and key type used for ID derivation must be
  versioned in node records so a future migration (e.g. post-quantum KEM + signature) can
  be negotiated without breaking all nodes simultaneously. No specific post-quantum
  algorithm is selected (OPEN RESEARCH).

### 3.4 Why a public key / node ID does not solve Sybil resistance

An adversary can generate unlimited key pairs and therefore unlimited node IDs at negligible
cost. Node ID authenticity (proof of key possession) is orthogonal to Sybil resistance
(cost or scarcity of identity creation). FST must not claim that authenticated node IDs
prevent eclipse or Sybil attacks. Sybil resistance remains OPEN RESEARCH (D-016, D-017) and
must be addressed at the validator-selection / consensus layer, not the discovery layer.

---

## 4. Key lifecycle and identity security

### 4.1 Key generation

- Node keys are generated locally at first run using a CSPRNG. The existing FeinStatic
  pattern uses OpenSSL `RAND_bytes` (`src/crypto/crypto.cpp:30`) and Ed25519 key generation
  (`src/crypto/crypto.cpp:249`). No key is ever generated by a bootstrap service or
  external party.
- Key generation must occur in a single, auditable location with clear entropy source
  requirements. Fallback to `/dev/urandom` or OS CSPRNG is acceptable; userspace PRNGs are
  not.

### 4.2 Secure storage

- Long-term node secrets are stored as raw 32-byte Ed25519 seeds in mode-0600 files owned
  by the running user (existing pattern: `NodeIdentity::load`/`save_new`,
  `src/network/peer_identity.cpp:129`/`save_new`).
- Optional passphrase-encrypted vault using AES-256-GCM with PBKDF2-HMAC-SHA256
  (existing pattern: `save_vault_new`/`load_vault`, PBKDF2 iterations 100,000–5,000,000,
  default 310,000). This preserves the FeinStatic engineering pattern (D-025) while
  allowing operators to choose encrypted-at-rest storage.
- Secrets must never be written to a Ledger database, trust store, log, or memory dump.
  The existing `NodeIdentity` destructor and move operations explicitly cleanse secret
  memory (`OPENSSL_cleanse`, `src/network/peer_identity.cpp:208`-`212`).

### 4.3 Key rotation, compromise, and recovery

- **Rotation:** A node generates a new key pair, publishes a signed rotation record binding
  old node ID → new node ID signed by the old key, and updates its node record. Peers cache
  the binding for a bounded window. The existing `PeerTrustStore::rotate`
  (`src/network/peer_identity.cpp:401`) provides a monotonic revocation+replacement pattern
  for the trust store; FST's discovery layer needs an equivalent signed record mechanism.
- **Compromise:** If a node key is suspected compromised, the operator revokes the old ID
  (signed revocation record) and rotates to a new key. Revocation is monotonic: a revoked
  ID cannot be reactivated (existing `PeerTrustStore` monotonic tombstone rule,
  `src/network/peer_identity.cpp:528`-`537`). Recovery requires out-of-band or
  gossip-propagated revocation; there is no central authority to force revocation.
- **Multiple nodes per operator:** One operator may run many nodes, each with independent
  keys. There is no operator-level master key (P8). Node keys are not linkable to operator
  identity by default.
- **Multiple identities per machine:** One physical machine may run many node processes,
  each with independent identity files and ports. No assumption of one-node-per-host.
- **Identity continuity across transport changes:** Node ID is transport-independent. A
  node may change IP, port, or transport (TCP → QUIC → radio) while retaining the same
  node ID; endpoint updates are carried in signed node records.

### 4.4 Privacy implications of publicly linkable identities

- A publicly linkable node ID allows observers to correlate a node's presence across
  networks, uptime, and neighbor sets. This is metadata exposure, not financial leakage
  (account keys are separate).
- Mitigations (PROVISIONAL, require research): ephemeral session keys, periodic node-key
  rotation, unlinkable session identifiers, optional Tor-like or mix-network transport
  adapters (future), and minimizing metadata in node records (no geo-location, operator
  name, or hardware identifiers).
- Strong cryptographic privacy is a PROVISIONAL direction (D-020); specific mechanisms are
  OPEN RESEARCH.

### 4.5 Decentralized Identifiers (DIDs)

- W3C DID Core v1.1 (March 2026 Candidate Recommendation Snapshot) provides a generic
  framework for decentralized identifiers and DID documents. It is **reviewer-supplied
  findings only; the specification has not been independently inspected.**
- Evaluation: DIDs could provide optional interoperability with external identity systems
  (e.g. a human/organisation identity layer). However, DID infrastructure is **not required**
  for base FST node identities. Node IDs are self-certifying public-key hashes and do not
  need DID resolution, DID documents, or a DID method registry to function.
- Recommendation: treat DID interoperability as an optional, out-of-base-protocol concern.
  Do not add DID dependencies to the network core. Re-evaluate if a human/organisation
  identity layer is specified in future research (RS-011).

---

## 5. Peer-discovery architecture comparison

Four approaches are compared. No approach is selected automatically; the recommendation
(section 14) is a hybrid.

### Approach A — Kademlia-based discovery (libp2p-style)

**Description.** Nodes are identified by Peer IDs derived from public keys. A Kademlia DHT
routes lookups by XOR distance between Peer IDs and target IDs. Nodes maintain routing
tables (k-buckets) of peers closer to themselves. Signed records (e.g. libp2p signed
peer records) can be stored and retrieved.

| Aspect | Assessment |
|---|---|
| Peer routing | Distributed, scalable O(log n) lookups; no central index |
| Signed records | Record signatures and validation limit arbitrary injection |
| DHT poisoning | Malicious nodes can store/return false records; requires record validation and source reputation |
| Sybil placement | Attacker can generate many IDs and position them near targets in the ID space (eclipse risk) |
| Stale records | No inherent expiry; requires TTL, re-publish, and garbage collection |
| NAT traversal | libp2p provides AutoNAT, circuit relay, hole punching (reviewer-supplied findings; spec not independently inspected) |
| Maturity | High (libp2p widely deployed); complexity high |
| Decentralization | High; no bootstrap authority required after bootstrapping |
| Cross-client | libp2p is multi-language; adopting it imports external protocol versioning and governance |

**Reviewer-supplied findings:** Public keys derive Peer IDs; Identify exchanges peer info and
addresses; Kademlia provides distributed peer routing; signed records and validation help
prevent arbitrary record injection; NAT traversal and relay protocols address reachability;
relay connections can remain end-to-end encrypted but relays do not provide anonymity.
**Status:** Reviewer-supplied; libp2p specs not independently inspected.

### Approach B — Signed node-record discovery (Ethereum ENR / Discovery v5)

**Description.** Each node publishes a signed Ethereum Node Record (ENR) containing its
public key, sequence number, and endpoint(s). Discovery v5 is a Kademlia-inspired protocol
operating over authenticated ENRs. Records are gossiped; nodes verify signatures and use
sequence numbers to accept newer versions.

| Aspect | Assessment |
|---|---|
| Record signatures | Cryptographic binding of endpoint to node ID; prevents endpoint hijacking by third parties |
| Sequence numbers | Monotonic per-record versioning enables freshness and replacement |
| Endpoint updates | Node re-signs with incremented sequence; peers adopt higher-sequence records |
| Identity binding | Public key embedded in record; signature verifies holder controls the key |
| Record freshness | Sequence numbers alone do not bound real-time age; requires TTL or re-publish intervals |
| Maturity | Ethereum Discovery v5 spec marked work in progress (reviewer-supplied); not an immutable standard |
| Decentralization | High; records self-certify, no central signer |
| Complexity | Moderate; requires signature verification on every record |

**Reviewer-supplied findings:** ENRs are signed node records; records include sequence
numbers and endpoint information; Discovery v5 is Kademlia-inspired over authenticated node
records; spec is work in progress. **Status:** Reviewer-supplied; ENR/discv5 specs not
independently inspected.

### Approach C — Hybrid bootstrap and peer exchange (Bitcoin-style)

**Description.** New nodes obtain initial peers from DNS seeds, cached peer lists from prior
runs, and address exchange (`addr` messages) with connected peers. Peer selection is
randomized among known addresses.

| Aspect | Assessment |
|---|---|
| Operational simplicity | Very high; few moving parts, easy to operate |
| Bootstrap sources | DNS seed responses are not inherently authenticated (reviewer-supplied) |
| Concentration risk | Exclusive dependence on seed providers exposes nodes to isolation; seed operator can return only attacker-controlled peers |
| Peer exchange | Unauthenticated `addr` gossip can be poisoned; requires rate limits, address validation, and diversity checks |
| Authentication | None inherent; any peer can advertise any address |
| Maturity | Very high (Bitcoin, decades of deployment) |
| Decentralization | Moderate; DNS seeds are operated by multiple independent parties but are a trusted bootstrapping set |

**Reviewer-supplied findings:** Bootstrap can use DNS seeds, cached peers, and peer address
exchange; DNS seed responses are not inherently authenticated; exclusive dependence on seed
providers can expose nodes to isolation. **Status:** Reviewer-supplied; Bitcoin devguide
not independently inspected.

### Approach D — Hybrid FST discovery (recommended direction, PROVISIONAL)

**Description.** Combine:
1. **Multiple independently operated bootstrap sources** — each supplies an initial set of
   signed node records. No single source is trusted; records are validated by signature,
   not by source authority.
2. **Authenticated signed node records** — ENR-style records binding node ID → endpoint,
   signed by the node key, with sequence numbers and expiry.
3. **Distributed discovery** — optional Kademlia-inspired routing over node records for
   finding peers beyond bootstrap (research direction; not selected as sole mechanism).
4. **Peer exchange** — authenticated peers share records of other peers they have recently
   connected to, subject to rate limits and validation.
5. **Optional local discovery** — link-local discovery (e.g. mDNS/LAN) for mesh and radio
   deployments where internet bootstrap is unavailable.

| Aspect | Assessment |
|---|---|
| Minimum trusted assumptions | Bootstrap source authenticity is not assumed; records are self-certifying. Multiple independent sources reduce concentration risk. |
| Resilience to source disappearance | Node caches previously authenticated peers; any single bootstrap source can fail without partitioning the node. |
| Resilience to malicious source | A malicious source can only supply records; records must be cryptographically valid and reachable to be used. Source reputation and record diversity mitigate. |
| Operational complexity | Higher than Approach C; requires record validation, expiry, source management. |
| Decentralization | High; no central membership authority. |
| Cross-client | Requires a common signed-record format; independent clients can interoperate if they implement the same record and validation rules. |

### Comparison matrix

| Criterion | A: Kademlia DHT | B: Signed records (ENR/discv5) | C: Bootstrap + peer exchange | D: Hybrid FST (PROVISIONAL) |
|---|---|---|---|---|
| Security (record authenticity) | Medium (depends on validation) | High (cryptographic signatures) | Low (unauthenticated) | High (self-certifying records) |
| Decentralization | High | High | Moderate (seed concentration) | High (multiple sources) |
| Operational complexity | High | Moderate | Low | Moderate–High |
| Scalability | High (O(log n) routing) | Moderate | Moderate | Moderate |
| Churn resilience | Moderate (routing-table maintenance) | Moderate | High (cached peers) | High |
| Discoverability | High (distributed lookup) | Moderate (gossip) | Moderate (seed-dependent) | High |
| Privacy | Medium (routing exposes ID proximity) | Medium (signed records link ID↔endpoint) | Low (plain address gossip) | Medium–High (design-dependent) |
| Implementation maturity | High (libp2p) | Moderate (discv5 WIP) | Very high (Bitcoin) | Low (to be defined) |
| Cross-client interoperability | High (libp2p multi-lang) | Moderate (Ethereum ecosystem) | High (Bitcoin P2P) | TBD (common format required) |
| Trust assumptions | DHT node honesty | Signer key security | Seed operator honesty | Record validity + source diversity |

**Key finding:** No single approach satisfies all FST constraints. Approach C is simplest
but has unauthenticated records and seed concentration risk incompatible with P1/P2.
Approach A provides the best routing but highest complexity and DHT-specific attack
surface. Approach B provides strong record authentication but is immature (discv5 WIP) and
Ethereum-coupled. Approach D combines the operational simplicity of C with the
authentication of B and optional routing of A, while distributing trust across multiple
bootstrap sources.

**Selection:** Approach D is the recommended direction, marked PROVISIONAL. The signed-node-record
format, bootstrap-source interface, and peer-exchange rules must be specified and validated
before implementation.

---

## 6. Bootstrap, routing and connectivity architecture

### 6.1 Bootstrap boundaries

Bootstrap services provide **connectivity assistance only**. They do not:
- Authorize network membership,
- Determine consensus participation,
- Sign blocks or transactions,
- Act as a trust root for node identity.

A node trusts **signed records**, not bootstrap operators. A bootstrap source that
supplies invalid or unreachable records is simply ignored; a malicious source cannot force
a node to connect to attacker-controlled peers unless the attacker also controls the
node's key (which bootstrap cannot provide).

### 6.2 Multiple independently operated bootstrap sources

- A node ships with a list of bootstrap source identifiers (URLs or network addresses)
  from multiple independent operators.
- On first start, the node queries a subset of sources and collects signed node records.
- Records are validated by signature against the embedded node public key; the source is
  not part of the trust decision.
- If all configured sources are unavailable, the node falls back to cached peers from
  prior runs and optional local discovery.
- Source lists are updatable by operators (configuration, not consensus) to allow
  replacing disappeared or malicious sources.

### 6.3 Cached and previously authenticated peers

- A node persists a peer store of previously connected peers: node ID, last-known
  endpoint(s), last-seen timestamp, connection success/failure count, and the signature
  of the last authenticated record.
- On restart, the node attempts reconnection to cached peers before consulting bootstrap
  sources. This enables operation when bootstrap is unavailable (P10 founder-independence
  implication: the network must not depend on any single operator's infrastructure being
  online).
- Cached peer entries expire after a configurable idle period; expired entries are not
  used unless re-authenticated through a fresh signed record.

### 6.4 Signed and versioned node records

A node record contains (provisional schema, field names subject to ADR):
- Node ID (public-key hash),
- Node public key,
- Signature over the record body by the node key,
- Sequence number (monotonic per node ID),
- Endpoint(s): transport, address, port (multiple endpoints permitted for multi-transport),
- Expiry timestamp,
- Optional service advertisements (relay, bootstrap, light-client sync).

Validation rules:
- Signature must verify against the embedded public key,
- `peer_id_from_public_key(public_key)` must equal the declared node ID,
- Sequence number must be greater than the last accepted sequence for that node ID,
- Expiry must be in the future,
- Endpoints must be syntactically valid for their declared transport.

Records are versioned (format version field) to permit future cryptographic migration.

### 6.5 Record freshness, expiry, and revocation

- Every record carries an expiry. Expired records are not accepted for new connections.
- Nodes re-publish updated records before expiry to remain discoverable.
- Revocation: a node signs a revocation record (old node ID, new node ID or tombstone,
  signed by old key). Revocation records are gossiped and cached. Revocation is monotonic
  (consistent with FeinStatic's `PeerTrustStore` monotonic tombstone pattern,
  `src/network/peer_identity.cpp:528`-`537`). There is no central revocation authority.
- Revocation does not delete historical connection records; it prevents new connections
  using the revoked key.

### 6.6 Peer discovery and address exchange

- Connected peers exchange signed node records of other peers they have recently
  authenticated (peer exchange / gossip).
- Exchanged records are validated by signature before being added to the peer store.
- Rate limits per peer prevent address-flooding and peer-store poisoning.
- Address diversity is enforced: a node does not accept an unbounded number of records
  from a single source or for endpoints in a single network prefix (mitigates prefix-level
  eclipse).

### 6.7 Peer reachability verification

- Before advertising a peer's endpoint to others, a node confirms reachability by
  completing a handshake (or a lighter reachability probe) to that endpoint.
- Unreachable endpoints are demoted in the peer store and retried with backoff
  (existing `PeerLifecycle` circuit breaker pattern, `src/network/peer_lifecycle.cpp`).
- Reachability results are not signed claims; they are local observations used for
  selection, not for authentication.

### 6.8 NAT and firewall traversal

Nodes behind NAT/firewall may be unreachable by direct inbound connection. Options
(evaluated, not selected):

1. **Outbound-only connections** — the node initiates outbound connections and does not
   accept inbound. Simple, works behind symmetric NAT, but the node cannot serve inbound
   peers and depends on outbound connectivity.
2. **NAT traversal / hole punching** — direct UDP hole punching after NAT mapping
   discovery. Works for many NAT types; fails for symmetric NAT without relay.
3. **Circuit relay** — a relay node forwards bytes between two peers. End-to-end
   encryption is preserved; the relay cannot read traffic (reviewer-supplied libp2p
   finding: relay connections remain end-to-end encrypted, relays do not provide
   anonymity).
4. **AutoNAT** — a node determines its own reachability by being dialed through a set of
   validators (reviewer-supplied libp2p concept; not independently inspected).

FST should support outbound-only operation as the default and relay as an opt-in
mechanism for nodes that cannot accept inbound connections. Hole punching is a future
transport-adapter concern.

### 6.9 Relay discovery and relay resource limits

- Relays are discovered through signed node records advertising a "relay" service.
- A relay must not become a trusted financial intermediary: it forwards encrypted bytes
  only, cannot forge sessions (end-to-end authentication is between endpoints), and cannot
  learn transaction contents.
- Relay resource limits: per-peer bandwidth caps, connection limits, time limits, and
  optional payment/credits for relay usage (future research; not consensus).
- Abuse controls: rate-limited relay registration, reputation based on observed relay
  behavior, and the ability to blacklist misbehaving relays locally (no central
  blocklist).

### 6.10 Connection establishment and authentication

- Every connection performs mutual authentication: both sides prove possession of their
  node key during the handshake.
- The handshake establishes an ephemeral session key with forward secrecy.
- After authentication, frames are encrypted and per-message authenticated (existing
  FeinStatic pattern: per-message Ed25519 signatures over AES-256-GCM-encrypted frames,
  `src/network/node.cpp`).
- Connection establishment is rate-limited per source address to prevent connection
  exhaustion (existing token-bucket admission, `RaftNetworkConfig::inbound_admission`,
  `include/feinstat/network/node.hpp:91`).

### 6.11 Network upgrades and protocol-version compatibility

- Node records and handshakes carry a protocol version. Nodes advertise supported
  versions; peers select the highest mutually supported version.
- Version negotiation must be downgrade-resistant: an active attacker must not be able to
  force two nodes to use an older, weaker protocol version they both support.
- Forks (D-036): independent networks have distinct network identities; a node record from
  network A is not valid on network B. Genesis hash or network identifier is bound into
  handshake transcripts to prevent cross-network replay (D-036 replay protection).

---

## 7. Security and adversarial threat model

### 7.1 Threat model assumptions

- **Attacker capabilities:** controls arbitrary network links, can observe, delay, drop,
  reorder, and inject traffic; can run many nodes (Sybil); can compromise bootstrap
  sources and relays; can steal node keys from poorly protected hosts.
- **Attacker cannot:** break the underlying cryptography (Ed25519 signatures, X25519 key
  agreement, AES-256-GCM, SHA-256, HKDF) assuming these remain secure; cannot forge
  signatures without the private key; cannot passively decrypt end-to-end sessions.
- **Trust assumptions:** local key storage is protected by the OS/user; at least one
  bootstrap source is honest and reachable initially; entropy source is reliable.
- **Not addressed in this milestone:** consensus-layer Sybil resistance, validator
  economics, finality (D-016, D-017 OPEN RESEARCH); offline-payment double-spend under
  partition (D-018 PROVISIONAL).

### 7.2 Threat-to-mitigation matrix

| Threat | Preconditions | Impact | Existing mitigations (FeinStatic) | FST requirements | Residual risk | Verification |
|---|---|---|---|---|---|---|
| Sybil attack | Ability to generate unlimited key pairs | Adversary controls large fraction of observed peers; distributes discovery and routing | None in discovery layer; auth proves key possession only, not scarcity | Peer-selection diversity, independent bootstrap sources, connection limits; Sybil resistance is consensus-layer (OPEN RESEARCH) | Cannot be fully prevented at discovery layer | Adversarial simulation: measure fraction of attacker-controlled peers in a node's view under varying bootstrap-source mixes |
| Eclipse attack | Attacker monopolizes victim's inbound/outbound connections | Victim isolated from honest network; fed false chain/state | Peer lifecycle circuit breaker (backoff, jitter); bounded inbound pool; per-address admission | Inbound/outbound connection management, peer diversity, authenticated records, reachability verification | Cannot be fully prevented; reduces probability and detection time | Chaos tests: isolate a test node from all but attacker peers; measure detection and recovery time |
| Malicious/colluding bootstrap nodes | Attacker operates bootstrap source(s) | Returns only attacker-controlled peer records | None (FeinStatic has pre-provisioned roster, no bootstrap) | Multiple independent sources, record signature validation, source not trusted | One source can still isolate a node that only trusts it | Failover test: remove all but malicious source; confirm node falls back to cached peers/local discovery |
| DHT poisoning | Attacker runs DHT nodes near target IDs | False routing, false peer records | None | Record validation, source reputation, k-bucket hardening, close-group validation | Routing-level attacks remain possible | DHT simulation with adversarial nodes; measure lookup success rate and false-record rate |
| Peer-record forgery/replay | Attacker has old signed record or can intercept | Peer redirected to stale/malicious endpoint | FeinStatic endpoint registry checksummed (no signatures); no record expiry | Signed records, sequence numbers, expiry, monotonic revocation | Replay within validity window | Unit tests: replay old record, confirm rejection by sequence; tampered record, confirm signature failure |
| Address flooding / peer-store poisoning | Attacker sends many `addr`/record messages | Peer store filled with attacker addresses; legitimate peers crowded out | None | Rate limits per peer, address validation, diversity limits, store size caps | Poisoning within rate limits | Fuzz/rate test: flood node with records; measure store composition and latency |
| Connection exhaustion | Attacker opens many connections | Node's inbound capacity consumed; legitimate peers blocked | Token-bucket admission (global + per-address); bounded worker pool; bounded queue | Per-peer and per-address connection limits, handshake timeouts, SYN cookies or equivalent | Resource limits tuned too low/high | Load test: open many connections from many addresses; measure legitimate-handshake success rate |
| Network partitioning | Attacker controls network links or AS | Subset of nodes cannot reach the rest | None | Multi-transport (internet + mesh/radio/satellite as future adapters), multiple bootstrap sources, cached peers | Partitions cannot be prevented; only mitigated | Partition test: split test network; confirm each side continues and reconnects when link restored |
| Malicious peer selection/concentration | Node chooses peers non-diversely | Victim connects mostly to attacker nodes | None | Connection-selection policy enforcing AS/prefix/geographic diversity, randomized peer selection | Selection policy can be bypassed by attacker with diverse infrastructure | Simulation: assign attacker addresses across prefixes; measure selection diversity |
| Traffic correlation / metadata | Observer sees connection timing, endpoints, volumes | Node identities linked; usage patterns exposed | None (FeinStatic TCP frames expose IP/port) | Transport-independent identity, optional padding/timing obfuscation (research), metadata minimization in records | Metadata cannot be fully eliminated without mix networks | Traffic analysis on test network; measure linkability of sessions to node IDs |
| Malicious relays | Attacker operates relay | Traffic interception (metadata only), denial of service, traffic correlation | None | End-to-end encryption and authentication; relay cannot forge or read; per-peer relay limits; local blacklist | Relay learns metadata (timing, volume, endpoints); DoS possible | Relay test: malicious relay drops/delays traffic; confirm endpoints detect and switch relays |
| Key theft / rotation / hijacking | Attacker steals node key file | Attacker impersonates node; historical sessions not compromised (forward secrecy) | Mode-0600 files, optional encrypted vault, secret cleansing | Key rotation, revocation, encrypted-at-rest option, HSM/TPM support (future) | Stolen key can impersonate until revocation propagates | Key-rotation test: rotate node key; confirm old connections rejected, new record accepted |
| Discovery-layer DoS | Attacker floods bootstrap/DHT with requests | Nodes cannot bootstrap or find peers | None | Rate limits per source, caching, multiple sources, local peer cache | Availability during attack cannot be guaranteed | Load test on bootstrap source; measure response latency under attack |
| Malicious network operators | ISP/AS blocks or shapes FST traffic | Nodes on that network isolated or degraded | None | Multi-transport adapters, obfuscation (research), store-and-forward for delay-tolerant links | Network-layer censorship cannot be prevented at protocol level | Not testable in lab; deployment observation |

### 7.3 Sybil resistance vs identity authentication vs peer discovery

These are **separate architectural responsibilities**:

- **Identity authentication:** "Are you who you claim to be?" — answered by public-key
  signatures and signed node records. Existing FeinStatic pattern (D-025).
- **Peer discovery:** "How do I find you?" — answered by bootstrap, signed records, peer
  exchange. This document's subject.
- **Sybil resistance:** "How many identities can one attacker cheaply create?" —
  unresolved (D-016 OPEN RESEARCH); must be addressed at validator selection/consensus,
  not at the network layer.

FST must not claim that authenticated node IDs, peer discovery, or connection limits
provide Sybil resistance.

---

## 8. Secure session establishment

### 8.1 Requirements

- Mutual authentication: both peers prove possession of their node keys.
- Identity binding: session keys are bound to the authenticated node IDs and to the
  network/genesis identifier (cross-network fork protection, D-036).
- Forward secrecy: compromise of long-term node keys does not decrypt past sessions.
- Replay protection: handshake transcripts and frame sequences prevent replay.
- Downgrade resistance: version negotiation cannot be forced to a weaker mutually
  supported version.
- Identity privacy: session establishment should not expose long-term node IDs to
  passive observers more than necessary (research; D-020 PROVISIONAL).
- Handshake resource exhaustion: rate-limit handshakes per source address; bound
  handshake state.
- Cross-implementation interoperability: handshake format and crypto must be specified
  so independent clients can interoperate (D-005, P4).

### 8.2 Noise Protocol Framework evaluation

The Noise Protocol Framework (https://noiseprotocol.org/noise.html) provides handshake
patterns with explicit security properties. Relevant patterns for evaluation:

- **Noise_KK**: mutual static-key authentication; strong identity binding; no forward
  secrecy by itself (requires ephemeral mix).
- **Noise_IX / Noise_IXps**: one static, one ephemeral; partial forward secrecy; optional
  pre-messages for 0-RTP.
- **Noise_NK / Noise_NKps**: initiator anonymous, responder authenticated; forward secrecy
  with ephemeral-ephemeral.
- **Noise_XK / Noise_XKps**: responder authenticated, initiator anonymous; forward secrecy.
- **Noise_XX**: both ephemeral; mutual forward secrecy; no static authentication (needs
  separate identity binding).

**Key finding:** No single Noise pattern provides all guarantees. FST needs mutual
authentication + forward secrecy + identity binding, which suggests a pattern combining
static-static and ephemeral-ephemeral DH (e.g. XX with post-handshake static binding, or
IK/XX variants). The exact pattern and associated hash/cipher functions are **OPEN
RESEARCH** and must be selected with compatibility to FST's existing cryptographic
research (Ed25519, X25519, SHA-256, HKDF-SHA-256, AES-256-GCM — all already in
`src/crypto/crypto.cpp`) and the D-020 privacy direction.

Noise does not specify transport framing, retransmission, or DTN semantics; those are
transport-adapter concerns.

### 8.3 Relationship to peer discovery and node identity

Transport security is evaluated independently from peer discovery and node identity:
- A session authenticates a node ID, but session establishment does not discover peers.
- Discovery supplies candidate endpoints; the session layer authenticates the peer at that
  endpoint.
- Node keys (long-term) and session keys (ephemeral) are separate; session keys are
  derived from an ephemeral DH exchange bound to the handshake transcript (existing
  FeinStatic pattern: `derive_peer_session_keys`, `src/crypto/crypto.cpp:153`).

### 8.4 Handshake security properties to verify

Before selecting a pattern, the following must be confirmed by formal or informal
analysis:
- Mutual authentication resistance to impersonation and man-in-the-middle,
- Forward secrecy under long-term key compromise,
- Replay resistance under session resumption or reconnection,
- Downgrade resistance in version negotiation,
- Key-compromise impersonation (KCI) resistance,
- Identity hiding from passive observers (optional, D-020).

---

## 9. Modular transport architecture

### 9.1 Component boundaries

The network stack is layered so identity, discovery, and sessions are independent of
transport:

```
┌─────────────────────────────────────────────────────────────┐
│ Network messaging (application messages, framing)           │
├─────────────────────────────────────────────────────────────┤
│ Secure channel establishment (Noise handshake, session keys)│
├─────────────────────────────────────────────────────────────┤
│ Connection management (dialer, listener, reconnection,     │
│  peer lifecycle, admission control)                         │
├─────────────────────────────────────────────────────────────┤
│ Transport adapters (TCP, QUIC, mesh, radio, satellite, DTN) │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│ Peer discovery (bootstrap, records, peer exchange, DHT opt) │
├─────────────────────────────────────────────────────────────┤
│ Peer records and peer storage                               │
├─────────────────────────────────────────────────────────────┤
│ Identity and key management                                 │
└─────────────────────────────────────────────────────────────┘
```

### 9.2 Transport-independence invariant

- Node ID is a public-key hash; it contains no address, port, or transport hint.
- Node records carry transport-typed endpoints (transport enum + address + port), so a
  node can advertise TCP, QUIC, mesh bearer, radio frequency, or satellite endpoint
  simultaneously.
- The connection manager dials using the transport adapter matching the endpoint's
  transport type. Identity and session logic are unchanged across transports.

### 9.3 Planned transport adapters (future; not implemented now)

1. **Ordinary internet (TCP/TLS or TCP/raw + Noise)** — the baseline adapter, reusing
   FeinStatic's Boost.Asio TCP engineering patterns (D-025) where useful.
2. **QUIC** — for low-latency, connection-migration-friendly internet transport; NAT
   traversal friendliness.
3. **Local mesh** — link-local discovery and forwarding for LAN/mesh deployments without
   internet bootstrap.
4. **Radio-based connectivity** — low-bandwidth, high-latency links; store-and-forward
   friendly; fragmented frame support.
5. **Satellite** — asymmetric bandwidth, high latency, store-and-forward; broadcast
   capability.
6. **Store-and-forward / delay-tolerant (DTN)** — Bundle Protocol (RFC 9171) as a
   potential forwarding research direction; RFC 9171 status to be verified before adoption
   (reviewer-supplied; RFC not independently inspected). DTN provides message delivery,
   **not** transaction finality or double-spend prevention (offline payments are D-018,
   separate research).

No adapter is implemented in this milestone. Interface requirements (below) define what
an adapter must provide.

### 9.4 Interface requirements for transport adapters

Every adapter must expose:

- `bind(endpoint) -> listener`: accept inbound connections on an endpoint.
- `dial(endpoint, timeout) -> connection`: initiate a connection.
- `endpoint_address(connection) -> endpoint`: the peer's observed endpoint.
- `send(connection, bytes)` and `receive(connection) -> bytes`: framed byte stream.
- `close(connection)`.
- `reachable(endpoint, timeout) -> bool`: reachability probe.
- `mtu(connection) -> size`: maximum transmission unit for fragmentation.

Adapters must **not**:
- Learn or modify application message contents (end-to-end encryption is above the
  adapter),
- Be trusted as network authorities,
- Require long-term node keys (only ephemeral session material during handshake).

### 9.5 Connection management responsibilities

- Dial and listen across adapters,
- Per-peer lifecycle (existing `PeerLifecycle` circuit breaker: six states, capped
  exponential backoff with jitter, `src/network/peer_lifecycle.cpp`),
- Inbound admission (existing token-bucket pattern: global + per-address,
  `InboundAdmissionConfig`, `include/feinstat/network/node.hpp:91`),
- Outbound connection pooling and queuing,
- Reconnection with backoff,
- Connection health tracking (latency, failures, successes).

---

## 10. Future mesh, radio, satellite and DTN integration boundaries

### 10.1 What store-and-forward does and does not provide

- **Provides:** message delivery across intermittent or partitioned links; store-and-forward
  for nodes that are not simultaneously online; support for radio/satellite links with
  high latency and low bandwidth.
- **Does not provide:** global transaction finality, double-spend prevention, or
  settlement. Offline payment authorization and settlement are separate protocol concerns
  (D-018 PROVISIONAL; scope-v0.1.md offline-financial-operation limitation).

### 10.2 DTN / Bundle Protocol

- RFC 9171 (Bundle Protocol) is a candidate store-and-forward forwarding layer. Its
  current status and updates must be verified before any adoption recommendation.
- DTN is a transport/forwarding research direction only; it does not affect identity,
  discovery, or session authentication boundaries.

### 10.3 Integration boundaries

| Boundary | Identity/Discovery | Transport adapter |
|---|---|---|
| Mesh | Node ID unchanged; endpoints use mesh addresses | Mesh adapter handles link-local routing |
| Radio | Node ID unchanged; endpoints use radio bearer IDs | Radio adapter handles fragmentation, FEC |
| Satellite | Node ID unchanged; endpoints use satellite downlink/uplink | Satellite adapter handles asymmetry, store-and-forward |
| DTN | Node ID unchanged; endpoints are DTN node IDs | Bundle Protocol or equivalent (research) |

The identity and discovery layers require no changes to add a new transport adapter;
only a new endpoint type and adapter implementation.

---

## 11. Existing FeinStatic implementation reuse assessment

This assessment is based on **actual source inspection** of
`src/network/node.cpp`, `src/network/peer_identity.cpp`, `src/network/peer_lifecycle.cpp`,
`include/feinstat/network/*.hpp`, `src/crypto/crypto.cpp`, `include/feinstat/crypto/crypto.hpp`,
and `tests/network_tests.cpp`, `tests/peer_lifecycle_tests.cpp`, `tests/crypto_tests.cpp`.

### 11.1 Components worth preserving (as reference or components)

| Component | Location | Assessment | Reuse basis |
|---|---|---|---|
| Ed25519 key generation, signing, verification | `src/crypto/crypto.cpp:249`-`313` | Reusable after cryptographic review (D-023) | OpenSSL EVP_PKEY_ED25519; correct raw-key import/export; secret cleansing |
| X25519 key generation and DH | `src/crypto/crypto.cpp:109`-`151` | Reusable after review | Correct shared-secret derivation; all-zero shared-secret rejection |
| HKDF-SHA-256 session key derivation | `src/crypto/crypto.cpp:153`-`177` | Reusable | Directional keys, nonce prefixes, transcript-bound labels |
| AES-256-GCM frame encryption | `src/crypto/crypto.cpp:179`-`247` | Reusable | AAD support, tag handling, decryption failure cleanup |
| PBKDF2-HMAC-SHA256 passphrase derivation | `src/crypto/crypto.cpp:37`-`53` | Reusable | Iteration bounds 100k–5M; default 310k |
| SHA-256 | `src/crypto/crypto.cpp:57`-`65` | Reusable | Standard EVP_Digest |
| NodeIdentity (generate, load, save, vault, peer_id derivation) | `src/network/peer_identity.cpp:119`-`324` | Reusable as reference; mode-0600 files, secret cleansing, `fn1` + sha256(pubkey) ID | Node key independent from wallet; encrypted vault option; move/copy zeroization |
| PeerTrustStore (trust, revoke, rotate, signed policy, monotonic epochs) | `src/network/peer_identity.cpp:382`-`545` | Reusable as reference; monotonic revocation, operator-signed policies | Rotation + revocation pattern directly applicable to node-record revocation |
| PeerLifecycle (6-state circuit breaker, backoff with jitter) | `src/network/peer_lifecycle.cpp` | Reusable | EWMA latency, deterministic FNV-1a jitter, per-peer in-flight |
| Inbound admission (token bucket, bounded worker pool, bounded queue) | `src/network/node.cpp` (via `serve_connection`/`InboundAdmissionConfig`) | Reusable pattern | Global + per-address rate limits; bounded concurrency |
| Authenticated frame construction (magic, version, sequence, SHA-256 checksum, per-message Ed25519 signature, AES-256-GCM) | `src/network/node.cpp:318`-`501` | Reusable as reference; protocol v8 is TCP-specific | Session binding, per-frame sequence/replay, AAD over type+sequence |
| Synchronous one-request-per-connection client (PeerClient) | `src/network/node.cpp:788`-`862` | Reference only; too synchronous for P2P | Handshake flow is a useful template (challenge-response, ephemeral X25519) |

### 11.2 Components too tightly coupled to the old architecture

| Component | Location | Reason |
|---|---|---|
| Pre-provisioned peer roster | `RaftNetworkConfig::peers`, `include/feinstat/network/node.hpp:107` | No permissionless entry; conflicts with P4/P5 |
| Endpoint registry bound to exact roster and cluster ID | `src/network/node.cpp:123`-`253` | Cannot discover new peers; tied to Raft membership |
| PeerTrustStore operator roles (`PeerRole::Operator`) | `include/feinstat/network/peer_identity.hpp:41` | Permanent privileged authority; conflicts with P2 |
| Signed trust policy requiring active operator signer | `src/network/peer_identity.cpp:447`-`545` | Centralized operator control; not permissionless |
| RaftMessage / consensus message types embedded in network protocol | `src/network/node.cpp` (kRaftMessage types 15–28) | Network layer must be consensus-agnostic |
| Protocol v8 wire format and magic `FSN1` | `src/network/node.cpp:40`-`41` | Chain-parameter-bound; version 8 specific to FeinStatic |
| `RaftServerContext` coupling network to Raft | `src/network/node.cpp:864` | Network layer must not reference consensus |
| One-request-per-connection synchronous model | `PeerClient`, `src/network/node.cpp:788` | Not suitable for concurrent P2P gossip and multiple streams |

### 11.3 Security assumptions unsuitable for FST

- **Trust store as membership authority.** FeinStatic's `PeerTrustStore` is an allow-list
  with operator roles. FST requires permissionless participation (D-016 PROVISIONAL);
  no operator may control network membership.
- **Pre-provisioned peer rosters.** No peer discovery exists in FeinStatic; peers are
  configured in advance. FST requires discovery (D-019 PROVISIONAL).
- **TCP-only transport.** FeinStatic uses Boost.Asio TCP only. FST requires modular
  multi-transport (D-019 PROVISIONAL).
- **Authenticated encryption ≠ privacy.** FeinStatic frames are encrypted and signed but
  expose IP/port metadata and linkable node IDs. D-020 (strong cryptographic privacy) is
  PROVISIONAL and unresolved.
- **No independent cryptographic audit.** The FeinStatic authenticated-encryption
  construction has no audit (feinstat-baseline.md §Networking). FST requires independent
  verification (P9).
- **No fork protection.** FeinStatic has a single chain; no network identity or replay
  protection across forks. D-036 (APPROVED) requires unique network identity and replay
  protection.

---

## 12. Candidate FST architecture designs and trade-offs

### Candidate 1 — Minimal bootstrap + peer exchange (adapted from Bitcoin)

**Design.** DNS-style bootstrap from multiple independent sources; signed node records;
peer exchange; cached peers; no DHT. Outbound-first connectivity; relay opt-in.

| Pros | Cons |
|---|---|
| Simple to implement and operate | No distributed lookup beyond bootstrap/cache |
| Low operational complexity | Bootstrap-source concentration risk (mitigated by multiple sources) |
| Proven prior art (Bitcoin) | Unauthenticated peer exchange requires strict validation |
| Aligns with P1/P2 (no central authority) | Limited scalability for very large networks |

**Best for:** Initial FST mainnet; small-to-medium node count; quick deployment.

### Candidate 2 — Kademlia DHT over signed records (libp2p-inspired)

**Design.** Node IDs as DHT keys; Kademlia routing; signed node records stored in DHT;
AutoNAT + circuit relay + hole punching.

| Pros | Cons |
|---|---|
| Distributed peer lookup without bootstrap after bootstrapping | High complexity; DHT-specific attacks (poisoning, Sybil placement) |
| Scales to large networks | libp2p dependency imports external governance/versioning |
| Rich NAT traversal tooling (reviewer-supplied findings) | Attacker can position nodes near targets (eclipse risk) |

**Best for:** Large-scale deployment where bootstrap/cache is insufficient. Higher
implementation and audit cost.

### Candidate 3 — Hybrid bootstrap + signed records + optional DHT (recommended, PROVISIONAL)

**Design.** Multiple independent bootstrap sources supply signed records; peer exchange
gossips records; optional Kademlia DHT layered on top for large networks; relay for NAT
traversal; multi-transport adapters; Noise-based secure channel.

| Pros | Cons |
|---|---|
| Distributes trust across bootstrap sources | More components to specify and audit |
| Self-certifying records (no source authority) | Record format, expiry, and revocation must be designed |
| Optional DHT can be added later without redesign | Discovery-layer complexity higher than Candidate 1 |
| Multi-transport by design | NAT traversal across heterogeneous transports is complex |

**Best for:** FST's stated goals (permissionless, multi-transport, no central
authority). Recommended direction.

### Trade-off summary

| Decision | Candidate 1 | Candidate 2 | Candidate 3 (recommended) |
|---|---|---|---|
| Bootstrap trust | Multiple sources (not trusted for validity) | DHT (no bootstrap after start) | Multiple sources + records |
| Record authentication | Signed records | Signed records in DHT | Signed records |
| Routing | Bootstrap + peer exchange | Kademlia DHT | Bootstrap + exchange + optional DHT |
| NAT traversal | Relay opt-in | AutoNAT + relay + hole punch | Relay opt-in + adapter-specific |
| Complexity | Low | High | Moderate |
| Decentralization | High | High | High |

---

## 13. Recommended architectural direction (PROVISIONAL)

**Candidate 3 — Hybrid bootstrap + signed records + optional DHT** is the recommended
direction. Classification: **PROVISIONAL** (D-016 permissionless participation and
D-017 BFT consensus remain unresolved; this architecture must not depend on them).

**Core commitments:**
1. **Seven-domain identity model** with strict separation (account ≠ node ≠ validator ≠
   session ≠ service ≠ human ≠ endpoint).
2. **Signed, versioned, expiry-bearing node records** as the universal peer descriptor.
3. **Multiple independent bootstrap sources**; records are self-certifying, sources are
   not trust roots.
4. **Peer exchange** of authenticated records with rate limits and diversity checks.
5. **Noise-based mutual-authentication secure channel** with forward secrecy; exact
   pattern OPEN RESEARCH.
6. **Transport-independent core** with adapter interfaces for TCP, QUIC, mesh, radio,
   satellite, DTN.
7. **Relay as non-trusted intermediary** (end-to-end encryption preserved).
8. **Reuse of FeinStatic cryptographic primitives and peer-lifecycle patterns** as
   reference components (D-025, PROVISIONAL), not as-is.
9. **No Sybil-resistance claim at the network layer** (OPEN RESEARCH under D-016/D-017).
10. **Fork protection** (D-036, APPROVED): network identity bound into handshakes and
    records; replay protection across networks.

**Not decided (OPEN RESEARCH):**
- Exact Noise handshake pattern and crypto suite,
- Whether to implement a Kademlia DHT at all (and when),
- Node-record serialization format and field set,
- Bootstrap source discovery mechanism (static list vs dynamic),
- Post-quantum algorithm agility strategy,
- Privacy mechanisms (D-020),
- Relay payment/credit model (if any),
- DTN Bundle Protocol adoption (RFC 9171 status unverified).

---

## 14. Unresolved engineering questions and required experiments

| # | Question | Why unresolved | Required experiment |
|---|---|---|---|
| 1 | Exact Noise handshake pattern for FST | Multiple patterns provide different guarantees; D-020 privacy direction unresolved | Prototype candidate patterns; formal/informal analysis of mutual auth, forward secrecy, KCI, downgrade resistance |
| 2 | Is a Kademlia DHT needed? | Depends on expected network size and churn; unmeasured | Simulation: bootstrap-only vs DHT lookup success under churn |
| 3 | Node-record format and field set | No standard selected; must balance extensibility and simplicity | Draft format; interoperability test between two independent implementations |
| 4 | Bootstrap source list management | Static config vs dynamic discovery; source trust model | Deployment test with multiple independent sources, one malicious |
| 5 | Peer selection diversity policy | How to enforce AS/prefix diversity without central data | Simulation with synthetic topology; measure eclipse resistance |
| 6 | Relay abuse control and resource limits | Relay must not be trusted; DoS possible | Prototype relay with per-peer caps; measure abuse impact |
| 7 | Key rotation and revocation propagation | No central authority; gossip convergence unmeasured | Simulation: revocation spread time and stale-record acceptance |
| 8 | Post-quantum algorithm agility | No PQ algorithm selected; migration path undefined | Cryptographic review; hybrid classic+PQ prototype |
| 9 | Privacy / metadata minimization | D-020 PROVISIONAL; mechanisms unspecified | Traffic-analysis measurement on test network |
| 10 | DTN / Bundle Protocol (RFC 9171) suitability | RFC status unverified; not independently inspected | Review RFC 9171 current status; DTN prototype for store-and-forward |

All experiments must produce actual results; no security or performance claims may be made
without measured evidence (P9, AGENTS.md §7).

---

## 15. Source register with verification status

| Source | Type | Verification status | Claims supported |
|---|---|---|---|
| FeinStatic source code (`src/network/`, `src/crypto/`, `include/feinstat/network/`, `include/feinstat/crypto/`, `tests/network_tests.cpp`, `tests/peer_lifecycle_tests.cpp`, `tests/crypto_tests.cpp`) | Inspected material | VERIFIED by direct source inspection | Authenticated TCP transport, Ed25519/X25519/AES-256-GCM/HKDF-SHA-256/PBKDF2, PeerTrustStore, PeerLifecycle, endpoint registry, peer_id derivation, protocol v8 frame format, token-bucket admission, Raft coupling |
| AGENTS.md, scope-v0.1.md, engineering-principles.md, decision-register.md, feinstat-baseline.md, research-register.md, research README | Inspected material (repository docs) | VERIFIED by direct file read | Principles P1–P14, decisions D-001–D-036, scope, baseline transformation assessment |
| libp2p specs (https://github.com/libp2p/specs), protocols, DHT, circuit relay, AutoNAT, hole punching | Reviewer-supplied findings | NOT independently inspected; URLs not fetched | Peer IDs from public keys; Identify exchange; Kademlia routing; signed records; NAT traversal/relay; relay end-to-end encryption, no anonymity |
| Ethereum ENR (https://github.com/ethereum/devp2p/blob/master/enr.md), Discovery v5 (discv5.md) | Reviewer-supplied findings | NOT independently inspected | ENRs are signed node records; sequence numbers and endpoints; discv5 Kademlia-inspired; discv5 WIP |
| Bitcoin P2P devguide (https://developer.bitcoin.org/devguide/p2p_network.html) | Reviewer-supplied findings | NOT independently inspected | DNS seeds, cached peers, addr exchange; DNS responses unauthenticated; seed concentration risk |
| Heilman et al. eclipse attack (USENIX Security 2015) | Reviewer-supplied findings | NOT independently inspected | Eclipse attack class demonstrated; old Bitcoin measurements not current FST estimates |
| Noise Protocol Framework (https://noiseprotocol.org/noise.html) | Reviewer-supplied findings | NOT independently inspected | Handshake patterns and security properties; not all patterns provide identical guarantees |
| W3C DID Core v1.1 (March 2026 CR Snapshot) | Reviewer-supplied findings | NOT independently inspected | Optional DID interoperability framework; not required for base node identities |
| RFC 9171 Bundle Protocol | Reviewer-supplied findings | NOT independently inspected | DTN store-and-forward candidate; not payment settlement/consensus |

**Evidence classification note:** Reviewer-supplied findings are cited as such; they are
bibliographic/source identities, not claims verified by this agent. Claims verified against
the repository are marked VERIFIED by direct inspection. No external web sources were
independently accessed during this research.

---

## 16. Implementation dependencies and acceptance criteria for later milestones

### 16.1 Staged implementation dependency map

```
Stage 1 (foundations):         Identity model → Node ID → Key storage → Signed node-record format → Record validation
Stage 2 (bootstrap):           Bootstrap source interface → Multiple sources → Cached peer store → Peer exchange
Stage 3 (session):             Noise handshake prototype → Mutual auth → Forward secrecy → Session keys → Frame encryption
Stage 4 (transport):           TCP adapter → QUIC adapter → Connection manager → Admission control → Peer lifecycle
Stage 5 (relay/NAT):           Relay service → AutoNAT/reachability → Hole punching (optional)
Stage 6 (multi-transport):     Mesh adapter → Radio adapter → Satellite adapter → DTN (research)
Stage 7 (hardening):           Key rotation/revocation → Diversity selection → Abuse controls → Independent client interop test
```

Each stage produces a small, tested, documented increment (D-013, P14).

### 16.2 Interface requirements for independent client implementations (D-005, P4)

Any independent FST client must implement:

1. **Identity:** node-key generation, storage, loading; `node_id = fn1 || hex(sha256(pubkey))`; signature over handshake transcripts.
2. **Node records:** parse, validate (signature, sequence, expiry, endpoint syntax), produce, and gossip signed records.
3. **Bootstrap:** query ≥1 bootstrap source; validate records by signature; fall back to cached peers.
4. **Secure channel:** mutual-authentication handshake with forward secrecy; frame encryption and per-message authentication; replay protection.
5. **Connection management:** dial/listen, per-peer lifecycle with backoff, inbound admission rate limits, reconnection.
6. **Network identity:** bind genesis/network ID into handshake to prevent cross-network replay (D-036).
7. **No consensus dependency:** the network layer must not import or reference consensus code.

What may remain implementation-specific: key-storage backend, crypto library, concurrency
model, adapter implementations, peer-health scoring weights, admission constants, CLI.

### 16.3 Acceptance criteria (to be validated in later engineering milestones)

- A node can start with no pre-configured peers, bootstrap from multiple independent
  sources, and connect to authenticated peers.
- A node with all bootstrap sources unavailable can reconnect to cached peers.
- A revoked node key cannot establish new sessions; rotation to a new key succeeds.
- Two independent client implementations can complete a mutual-authentication handshake
  and exchange encrypted frames.
- A node behind NAT can connect outbound and, optionally, via relay, with end-to-end
  encryption preserved (relay cannot read traffic).
- Cross-network replay is rejected: a handshake/record from network A is invalid on
  network B.
- No single bootstrap source or relay can partition or impersonate the network.

---

## 17. Conclusion

FST's network foundation must be permissionless, transport-independent, and free of any
central discovery authority. The existing FeinStatic networking provides useful
cryptographic primitives, peer-lifecycle engineering, and authenticated-frame patterns,
but its trust model (operator trust store, pre-provisioned rosters), TCP-only transport,
and lack of discovery are incompatible with FST's goals and must be replaced, not patched.

The recommended direction (Candidate 3, PROVISIONAL) combines signed self-certifying node
records, multiple independent bootstrap sources, peer exchange, optional DHT, Noise-based
session authentication, and modular transport adapters. All designs in this document
require independent technical review, prototype validation, and measured results before
implementation. No new project decision is approved by this milestone.

---

## Cross-references

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope and D-036 fork protection.
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — P1–P14.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — D-016, D-017, D-018, D-019, D-020, D-025, D-035, D-036.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — FeinStatic baseline and transformation assessment.
- [`../research/research-register.md`](../research/research-register.md) — Research register (RS-014).
- [`../research/consensus-validator-research.md`](../research/consensus-validator-research.md) — RS-013 consensus/validator research (cross-reference for D-016/D-017).
