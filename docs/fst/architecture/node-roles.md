# FST Node Roles and Architecture — v0.1

**Status:** DRAFT — initial node architecture definition.
**Classification:** PROVISIONAL (preferred architectural direction requiring validation;
not an approved production design).
**Milestone:** FST Milestone 2E — Core Protocol Specification and Node Architecture.
**Related decisions:** D-001, D-003, D-005, D-006, D-008, D-010, D-016, D-017, D-018,
D-019, D-020, D-036.
**Related research:** RS-001 through RS-014.

This document defines the responsibilities and data requirements of FST node categories:
full nodes, light clients, archival nodes, validator-capable nodes (future architectural
role), and bootstrap/discovery services. No node category is assigned permanent privileged
authority. Validator membership and selection are not implemented or specified here; both
remain OPEN RESEARCH (decision D-016 PROVISIONAL, D-017 OPEN RESEARCH).

## 1. Design Principles

- **No permanent privileged authority:** No node category has unilateral control over the
  protocol, consensus, or financial state (principles P1, P2, P8).
- **Fork safety:** Node roles treat all forks as potentially legitimate until participants
  choose otherwise (principles P6, P7).
- **Upgrade rejection:** Participants can run software that rejects proposed upgrades
  without losing network access (principle P5).
- **Independent maintainability:** Each role is independently implementable by separate
  teams (principle P4).
- **Research-first:** Role boundaries are grounded in reviewed research; unresolved
  mechanism questions are explicitly marked (principle P12).

## 2. Role Overview

| Role | Synchronizes | Verifies state | Stores full history | Participates in consensus | Privileged authority |
|------|-------------|----------------|---------------------|---------------------------|----------------------|
| Full node | All blocks and state | Yes (independently) | Recent state + bounded history | No (future: possible) | No |
| Light client | Block headers + proofs | No (relies on full nodes) | Minimal (headers only) | No | No |
| Archival node | All blocks and state | Yes (independently) | Complete history | No (future: possible) | No |
| Validator-capable node | All blocks and state | Yes | Recent state + bounded history | Yes (future role) | No |
| Bootstrap/discovery service | Node records only | No | Node records + metadata | No | No (connectivity only) |

## 3. Full Node

### 3.1 Responsibilities

A full node independently validates the entire state of the network from genesis (or from
a trusted snapshot) and maintains current state.

- **Synchronization:** Downloads and validates blocks and state data from peers; catches
  up from genesis or a snapshot; remains current with the chain tip.
- **State verification:** Independently re-executes state transitions and verifies all
  state against local rules. Does not trust peer-provided state.
- **Peer interactions:** Maintains authenticated peer connections; relays valid messages
  within its advertised capabilities; participates in peer discovery and exchange.
- **Transaction handling:** Receives, validates (syntactically and against local state),
  and relays transactions according to local policy. Does not execute application-layer
  financial logic beyond validation (transaction model OPEN RESEARCH).
- **Availability:** Expected to remain online with reasonable uptime; handles temporary
  disconnection and resynchronization on reconnect.

### 3.2 Required local data

- Node identity key pair and signed node record (established implementation).
- Peer store of validated signed node records (established implementation).
- Current network state (balances, accounts, or UTXO set — model unresolved).
- Recent block headers and blocks sufficient for reorg/safety within the local fork-choice
  horizon (finality model unresolved).
- Local configuration: network identifier, protocol version, capabilities, endpoints.

### 3.3 Storage expectations

- Persistent storage for state and recent history (SQLite persistence patterns reusable
  from FeinStatic, D-031 PROVISIONAL).
- Bounded history retention: full nodes prune old blocks according to local policy
  (pruning policy unresolved).
- Mode-0600 file permissions for key material (established implementation pattern).

### 3.4 Resource and connectivity considerations

- Moderate CPU, memory, and disk; suitable for operator-run hardware or cloud instances.
- Persistent internet or mesh connectivity preferred; tolerates intermittent
  disconnection with resynchronization on return (offline operation PROVISIONAL, D-018).
- Inbound and outbound connection limits per peer admission (established implementation:
  max 64 total, 32 inbound, 32 outbound, 2 per peer).

### 3.5 Trust assumptions

- Trusts its own local verification, not peers.
- Trusts bootstrap services only for connectivity (not membership or consensus authority)
  (D-036 APPROVED; bootstrap source is not a trust root, established implementation).
- Assumes at least some honest peers are available for synchronization.

## 4. Light Client

### 4.1 Responsibilities

A light client does not store full state or independently validate state transitions. It
relies on full nodes or archival nodes for state proofs.

- **Synchronization:** Downloads block headers (not full blocks) and state proofs;
  verifies proofs against header commitments (state commitment scheme OPEN RESEARCH,
  RS-009).
- **State verification:** Delegates execution to full nodes; verifies Merkle/state proofs
  for specific transactions or balances it cares about. Cannot detect state transitions
  it does not request proofs for.
- **Peer interactions:** Connects to full nodes as peers; requests proofs; may use
  multiple full nodes to cross-check responses (reduces trust in any single node).
- **Transaction handling:** Submits transactions to full nodes for inclusion; does not
  validate transactions locally beyond syntactic checks.
- **Availability:** Intermittent connectivity acceptable; syncs on connect.

### 4.2 Required local data

- Node identity key pair and signed node record (optional for clients that only initiate
  outbound connections).
- Block headers for the headers chain it follows.
- State proofs for transactions/balances it tracks.
- Local configuration: network identifier, trusted full-node peers (optional).

### 4.3 Storage expectations

- Minimal: block headers only, plus transient proof data.
- No full state or block storage.

### 4.4 Resource and connectivity considerations

- Low CPU, memory, and disk; suitable for mobile devices, embedded hardware, or browsers.
- Intermittent connectivity acceptable.
- Dependent on full-node availability for proof retrieval.

### 4.5 Trust assumptions

- Trusts full nodes to provide valid proofs; mitigates by querying multiple independent
  full nodes.
- Does not trust any single full node for state correctness (proofs are cryptographically
  verifiable).
- Light-client proof schemes and sync protocols are OPEN RESEARCH (RS-009).

## 5. Archival Node

### 5.1 Responsibilities

An archival node stores the complete historical state of the network, including all
blocks and state transitions from genesis.

- **Synchronization:** Downloads and validates the complete history from genesis; retains
  all data permanently (subject to local storage capacity).
- **State verification:** Independently verifies all state transitions, as with a full
  node.
- **Peer interactions:** Serves historical blocks and state to full nodes, light clients,
  and other archival nodes on request.
- **State verification:** Full independent verification (same as full node).
- **Availability:** Expected to be highly available for historical queries; handles
  catch-up after extended disconnection.

### 5.2 Required local data

- All data required by a full node.
- Complete block history from genesis.
- Complete state history (all historical state snapshots, if retained).

### 5.3 Storage expectations

- Large persistent storage (complete network history); storage growth is unbounded as
  the network grows.
- Storage pruning is NOT performed (by definition of archival).
- Mode-0600 file permissions for key material.

### 5.4 Resource and connectivity considerations

- High disk storage requirements; grows with network history.
- High bandwidth for serving historical data to peers.
- Persistent connectivity preferred.

### 5.5 Trust assumptions

- Same as full node: trusts local verification, not peers.
- Trusts bootstrap services only for connectivity.

## 6. Validator-Capable Node (Future Architectural Role)

### 6.1 Status

Validator-capable node is a **future architectural role**. No validator membership,
selection, slashing, or consensus participation mechanism is specified or implemented in
this milestone. Consensus algorithm, Sybil resistance, and validator economics remain
OPEN RESEARCH (D-016 PROVISIONAL, D-017 OPEN RESEARCH, RS-013).

### 6.2 Anticipated responsibilities (not implemented)

A validator-capable node would, in addition to full-node responsibilities:

- Participate in consensus (propose blocks, vote on ordering, finalize) according to the
  unselected consensus algorithm.
- Maintain validator identity and any staking/bonding credentials (mechanism unresolved).
- Participate in validator entry and exit (mechanism unresolved).

### 6.3 Distinctions from other roles

- Running a full node does NOT imply participating in consensus (RS-013 Section 6.18).
- Validator-capable nodes are a subset of full nodes with additional consensus duties.
- No node category has privileged authority over the protocol (principle P2).

### 6.4 Unresolved design

- Validator eligibility, selection, and rotation mechanisms (RS-013 Sections 2.2, 2.3,
  6.9–6.10).
- Sybil-resistance mechanism (RS-002, D-016).
- Staking, slashing, and unbonding (RS-013 Sections 6.4, 6.8–6.9).
- Consensus algorithm and finality model (RS-001, D-017).

## 7. Bootstrap / Discovery Services

### 7.1 Role definition

Bootstrap/discovery services provide **connectivity assistance only** — they help new or
rejoining nodes find initial peers. They do NOT authorize membership, determine consensus
participation, validate transactions, or control the protocol.

### 7.2 Responsibilities

- Serve signed node records to requesting nodes (established implementation:
  `BootstrapDiscovery`, `BootstrapSource` interface).
- Provide multiple independent bootstrap sources so no single source is a central
  authority (established implementation; P1, P2).
- Record how each peer was learned (`Bootstrap`, `PeerExchange`, `Cached`, `Configured`,
  `Unknown`) (established implementation).

### 7.3 Required local data

- A list of bootstrap entry addresses (configured or dynamically discovered).
- Signed node records learned from bootstrap sources.
- Local configuration: network identifier (for wrong-network rejection).

### 7.4 Storage expectations

- Minimal: bootstrap entry list and cached peer records.
- Peer store patterns from `PeerStore` (established implementation).

### 7.5 Trust assumptions (critical)

- **Bootstrap sources are NOT trust roots.** Records from bootstrap sources are
  self-certifying (signed by the node's own key) and validated independently
  (established implementation: signature, sequence, expiry, network ID checks).
- A malicious bootstrap source can withhold records, provide stale records, or provide
  records for malicious peers — but cannot forge records for other nodes or authorize
  membership (established implementation; threat model Section 7).
- Multiple independent bootstrap sources are REQUIRED to avoid a single point of failure
  or control (proposed specification; P1, P2).

### 7.6 What bootstrap services MUST NOT do

- Approve or reject validator membership.
- Determine consensus participation.
- Sign transactions or blocks on behalf of users.
- Act as a central certificate authority (self-signed certs with node-record binding,
  established implementation).
- Serve as a single point of network control.

## 8. Role Comparisons and Transitions

### 8.1 Capability-based role advertisement

Nodes advertise capabilities in signed node records (established implementation) and
during handshake (established implementation). Capabilities describe what a node is
willing to do (e.g., relay transactions, serve blocks, serve historical data), NOT what it
is authorized to do (proposed specification; consistent with signed node record security
invariant).

### 8.2 Role transitions

- A light client may become a full node by downloading and validating history.
- A full node may become an archival node by retaining full history.
- A full node may become validator-capable when the consensus and Sybil-resistance
  mechanisms are approved and implemented (future work).
- No role transition requires approval from any network authority (P4, P5).

### 8.3 Mixed deployments

A single node process may combine roles (e.g., full node + archival storage) through
configuration. Role boundaries are logical, not enforced by protocol.

## 9. Synchronization Model

### 9.1 Synchronization modes

| Mode | Description | Status |
|------|-------------|--------|
| Genesis sync | Download and validate from genesis | Proposed specification |
| Snapshot sync | Download a state snapshot, then sync from snapshot height | Reference: FeinStatic snapshot patterns (D-032 PROVISIONAL) |
| Header-only sync | Light client downloads block headers only | Unresolved (RS-009) |
| Catch-up sync | Sync from last known state to chain tip after disconnection | Proposed specification |

### 9.2 State transfer

State synchronization mechanics use snapshot and streaming patterns preserved from
FeinStatic as reference (D-032 PROVISIONAL). Exact state commitment scheme, snapshot
format, and streaming protocol are OPEN RESEARCH (RS-009).

## 10. Storage Expectations Summary

| Role | State | History | Keys/records | Growth |
|------|-------|---------|--------------|--------|
| Full node | Current state | Recent (pruned) | Node records + peer store | Bounded |
| Light client | None | Headers only | Minimal | Very low |
| Archival node | Current + historical state | Complete (genesis) | Node records + peer store | Unbounded |
| Validator-capable | Current state | Recent (pruned) | Node records + validator creds | Bounded |
| Bootstrap service | None | None | Bootstrap entries + peer records | Low |

## 11. Availability and Trust Assumptions

### 11.1 Availability assumptions

- Full nodes and archival nodes are expected to be online with reasonable uptime for
  network health.
- Light clients may be intermittently connected.
- Bootstrap services should be available for initial peer discovery but are not required
  for ongoing operation once peers are known (peer exchange provides ongoing discovery,
  established implementation).

### 11.2 Trust assumptions

- **No trusted third party:** No node category or service is trusted for financial
  correctness (P9 — security from protocol design and independent verification).
- **No master key:** No node holds a master key or emergency administrator privilege
  (P8).
- **No unilateral control:** No organization controls validator membership or protocol
  upgrades (P1, P2).
- **Bootstrap connectivity only:** Bootstrap services are trusted only for connectivity,
  never for authorization (established implementation).

## 12. Cross-References

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope; fork
  protection requirements (D-036); offline operation (D-018).
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) —
  Approved engineering principles (P1–P14).
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision
  classifications (D-016, D-017, D-018, D-019, D-020, D-036).
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
- [`../security/threat-model-v0.1.md`](../security/threat-model-v0.1.md) — Network
  threat model (this milestone).
- [`../research/research-register.md`](../research/research-register.md) — Research
  register (RS-001 through RS-014).
- [`../glossary.md`](../glossary.md) — FST glossary of terms.

## 13. Open Questions

1. What is the pruning policy for full nodes? (Unresolved)
2. What is the state commitment scheme for light-client proofs? (OPEN RESEARCH, RS-009)
3. What is the snapshot format and state-transfer protocol? (Reference: D-032 PROVISIONAL;
   format unresolved)
4. What is the validator eligibility and selection mechanism? (OPEN RESEARCH, D-016,
   D-017, RS-013)
5. What is the Sybil-resistance mechanism? (OPEN RESEARCH, RS-002)
6. What are the storage growth bounds for archival nodes? (Unresolved)
7. What are the minimum uptime expectations for full nodes? (Unresolved)
8. What capability strings are standardized for role advertisement? (Unresolved)
