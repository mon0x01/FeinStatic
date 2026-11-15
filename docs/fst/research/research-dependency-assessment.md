# FST Research Dependency and Priority Assessment — v0.1

**Status:** DRAFT — initial research dependency and priority assessment.
**Classification:** PROVISIONAL (assessment direction requiring validation; not an
approved research plan).
**Milestone:** FST Milestone 2E — Core Protocol Specification and Node Architecture.
**Related decisions:** D-015, D-016, D-017, D-018, D-019, D-020, D-021, D-035, D-036.
**Related research:** RS-001 through RS-015.

This document reviews the existing research register (`research/research-register.md`),
identifies the remaining research items that directly block later protocol and network
development, and prioritizes them by engineering impact. It records dependencies and
explains which future engineering work each unresolved topic blocks.

No OPEN RESEARCH or PROVISIONAL decision is converted to APPROVED. No account or UTXO
model, consensus algorithm, validator mechanism, monetary policy, or privacy mechanism
is selected.

## 1. Research Register Review

The research register tracks 15 entries (RS-001 through RS-015), all classified OPEN.
Three entries (RS-013, RS-014, RS-015) have dedicated research documents. RS-014 records that
implementation was begun in Milestones 2C–2D (node identity, signed records, peer store,
bootstrap discovery, peer exchange, TLS 1.3 secure sessions, message dispatch, network
runtime), but the research document itself remains OPEN — no Noise handshake pattern,
DHT adoption, or post-quantum agility is selected.

All other entries (RS-001 through RS-012) have "TBD" sources and no dedicated research
documents. They are recorded as research topics with design implications but no
substantive survey.

## 2. Research Items That Directly Block Later Development

The following research items are prerequisites for Milestone 3 (minimal permissionless
test network) and subsequent milestones. They are listed in priority order by engineering
impact.

### Priority 1 — Transaction model (blocks: transaction processing, mempool, dissemination)

**Research entry:** RS-015 (Transaction Model and State Representation) is the dedicated
entry for the transaction model (account vs UTXO, fee market, replay protection). RS-002
(Sybil-resistance mechanisms) and RS-003 (transaction confidentiality and privacy) are
related but remain separate, intact OPEN research entries; they are not the transaction-
model entry.

**Classification:** OPEN RESEARCH (RS-015).

**What it blocks:**
- Transaction serialization format (core protocol specification Section 8, unresolved).
- Transaction validation and mempool logic (not implemented; milestone 3+).
- Transaction dissemination message types (core protocol specification Section 7,
  unresolved).
- Transaction-level replay protection across forks (threat model T-06, unresolved).
- Fee market and prioritization (unresolved).

**Dependencies:** None on other research items; can proceed independently.

**Why priority 1:** The transaction model is the foundational application-layer concern.
Without it, no transaction processing, mempool, or dissemination can be specified or
implemented. The core protocol specification deliberately leaves transaction boundaries
open (Section 8) pending this research.

### Priority 2 — Permissionless consensus and Sybil resistance (blocks: Milestone 3 test network)

**Research entries:** RS-001 (consensus algorithms), RS-002 (Sybil-resistance mechanisms).

**Classification:** OPEN RESEARCH (D-016 PROVISIONAL, D-017 OPEN RESEARCH).

**What it blocks:**
- Milestone 3 (minimal permissionless test network with Byzantine fault-tolerant
  finality).
- Validator-capable node role (node-roles.md Section 6, future architectural role).
- Consensus message types and agreement/ordering (core protocol specification Section 7,
  unresolved).
- Block format and finality rules (core protocol specification Section 9, unresolved).
- Fork choice and state-machine execution (RS-013 taxonomy components 2.7, 2.9).

**Dependencies:**
- Depends on transaction model (Priority 1): consensus orders transactions, so the
  transaction format must be at least provisionally specified before consensus can
  reference it.
- Depends on Sybil resistance (RS-002): without Sybil resistance, quorum thresholds and
  Byzantine fault bounds are meaningless (consensus-validator-research.md Section 3.3).

**Why priority 2:** Consensus is the gating item for Milestone 3. Sybil resistance must be
resolved in parallel because it determines the validator admission model, which shapes
consensus design.

### Priority 3 — Privacy architecture (blocks: privacy layer, transaction confidentiality)

**Research entry:** RS-003 (transaction confidentiality and privacy).

**Classification:** PROVISIONAL (D-020 — strong cryptographic privacy is an approved
direction; specific mechanisms unresolved).

**What it blocks:**
- Transaction-confidentiality mechanisms (scope-v0.1.md, open research).
- Identity mechanisms and selective disclosure (scope-v0.1.md, open research).
- Traffic-analysis resistance (threat model T-09, not mitigated).
- Metadata minimization in protocol messages (core protocol specification, unresolved).

**Dependencies:**
- Depends on transaction model (Priority 1): privacy mechanisms are applied to
  transactions, so the transaction model constrains what privacy is feasible.

**Why priority 3:** Privacy is an approved direction (D-020 PROVISIONAL) but not required
for Milestone 3. It blocks the privacy layer milestone, not the initial test network.

### Priority 4 — Monetary policy (blocks: issuance, rewards, economic security)

**Research entry:** RS-006 (monetary policy and supply mechanics).

**Classification:** OPEN RESEARCH (D-015 OPEN RESEARCH).

**What it blocks:**
- Supply rules and issuance schedule (scope-v0.1.md).
- Validator rewards and economic security (consensus-validator-research.md Section 7).
- Staking and bonding economics if stake-based Sybil resistance is selected (RS-013
  Section 6.4).
- Monetary-consensus interdependence (RS-013 Section 7.14: consensus determines what
  resource secures the network; monetary policy determines the value and issuance of
  that resource).

**Dependencies:**
- Depends on consensus and Sybil resistance (Priority 2): economic security models vary
  by consensus family (PoW vs PoS vs other).
- Depends on transaction model (Priority 1): fees and rewards are transaction-level
  concepts.

**Why priority 4:** Monetary policy blocks validator economics and reward design but does
not block the initial protocol specification or test network deployment. It must be
resolved before mainnet.

### Priority 5 — Offline financial operation (blocks: local spending, reconciliation)

**Research entry:** RS-007 (offline operation and bounded spending).

**Classification:** PROVISIONAL (D-018 PROVISIONAL).

**What it blocks:**
- Bounded offline spending authority (scope-v0.1.md).
- Local financial operation during disconnection (scope-v0.1.md).
- Reconciliation when communication returns (scope-v0.1.md).
- Partition-tolerant operation modes (consensus-validator-research.md Section 3.13,
  four operation modes).

**Dependencies:**
- Depends on transaction model (Priority 1): offline spending requires a transaction
  model with bounded authority.
- Depends on consensus (Priority 2): reconciliation after reconnection requires global
  consensus recovery.

**Why priority 5:** Offline operation is a post-mainnet feature per the incremental
release philosophy (P13/P14). It blocks regional autonomy and offline spending, not the
initial network.

### Priority 6 — Independent fiat connectivity (blocks: FST/fiat exchange)

**Research entry:** RS-008 (decentralised bridge to national currencies).

**Classification:** OPEN RESEARCH.

**What it blocks:**
- FST exchange with national currencies (scope-v0.1.md financial functionality).
- Bank deposits and withdrawals through independent infrastructure (scope-v0.1.md).
- Non-custodial FST<->fiat exchange (research register).

**Dependencies:**
- Depends on monetary policy (Priority 4): fiat exchange mechanics depend on the token's
  monetary properties.
- Depends on transaction model (Priority 1): exchange transactions are financial
  transactions.

**Why priority 6:** Fiat connectivity is a long-term vision feature, not required for
the core network or initial test network.

### Priority 7 — Network discovery and multi-transport (partially implemented)

**Research entry:** RS-005 (P2P network discovery and multi-transport).

**Classification:** OPEN RESEARCH (DHT adoption, NAT traversal, DTN unresolved).
Implementation begun in Milestones 2C–2D (bootstrap discovery, peer exchange, TLS 1.3
sessions); DHT, NAT traversal, and transport adapters beyond TCP unimplemented.

**What it blocks:**
- DHT-based distributed discovery (node-identity-peer-discovery-implementation.md
  Limitation 3).
- NAT traversal (relay, hole punching, AutoNAT; Limitation 4).
- Transport adapters beyond TCP/TLS (QUIC, mesh, radio, satellite, DTN; D-019 PROVISIONAL).
- Store-and-forward / delay-tolerant networking (RS-013 Section 8.10).

**Dependencies:**
- Depends on consensus partition tolerance (Priority 2): DTN operation during partitions
  requires consensus that tolerates store-and-forward links.

**Why priority 7:** Basic discovery (bootstrap + peer exchange) is implemented and
sufficient for initial networks. DHT, multi-transport, and DTN are enhancements for
later milestones.

### Priority 8 — Light-client state proofs (blocks: light client sync)

**Research entry:** RS-009 (light-client state proofs).

**Classification:** OPEN RESEARCH.

**What it blocks:**
- Light-client sync protocols (node-roles.md Section 4).
- Merkle commitments and state proofs (node-roles.md Section 9).
- Pruning and archival state proofs (node-roles.md Section 5).

**Dependencies:**
- Depends on transaction model and block format (Priority 1, 2): state commitments are
  over transaction/block data.

**Why priority 8:** Light clients are a secondary node role; full nodes and archival
nodes are prioritized for the initial network.

### Priority 9 — Governance and on-chain upgrades (blocks: upgrade signaling)

**Research entry:** RS-010 (governance and on-chain upgrades).

**Classification:** OPEN RESEARCH.

**What it blocks:**
- Upgrade signaling mechanism (core protocol specification Section 12, unresolved).
- Participant veto and fork legitimacy (P5, P6, P7).
- Protocol upgrade coordination without central authority (P2).

**Dependencies:**
- Depends on consensus (Priority 2): upgrade signaling is consensus-adjacent.

**Why priority 9:** Upgrade signaling is needed before mainnet upgrades but not for the
initial test network.

### Priority 10 — Independent client implementation count (blocks: mainnet readiness)

**Research entry:** RS-012 (independent client implementation count).

**Classification:** OPEN RESEARCH (D-035 OPEN RESEARCH).

**What it blocks:**
- Mainnet readiness decision (one, two, or three independent implementations before
  mainnet; no final number selected).
- Interoperability testing and conformance (threat model T-013, mitigations planned but
  not implemented).

**Dependencies:**
- Depends on protocol specification stability (this milestone): independent
  implementations require a stable specification.

**Why priority 10:** Mainnet gating, not development gating.

### Priority 11 — Compliance (blocks: fiat on/off-ramp, jurisdictional operation)

**Research entry:** RS-011 (compliance).

**Classification:** OPEN RESEARCH.

**What it blocks:**
- Regulatory boundaries and jurisdictional differences.
- Fiat on-ramp and off-ramp requirements.
- Custodian/exchange/liquidity-provider compliance.

**Dependencies:**
- Depends on monetary policy (Priority 4) and fiat connectivity (Priority 6).

**Why priority 11:** Operational/legal gating for fiat connectivity, not core protocol
gating.

## 3. Dependency Graph

```
Transaction model (P1)
  ├── Privacy architecture (P3)
  ├── Monetary policy (P4)
  ├── Offline operation (P5)
  └── Independent fiat connectivity (P6)

Consensus + Sybil resistance (P2)
  ├── Monetary policy (P4) — economic security coupling
  ├── Offline operation (P5) — reconciliation
  ├── Network discovery/multi-transport (P7) — DTN during partitions
  ├── Light-client proofs (P8) — state commitments over blocks
  └── Governance/upgrades (P9) — upgrade signaling

Protocol specification (Milestone 2E, complete)
  └── Independent client implementations (P10)
```

## 4. Items Not Addressed in This Milestone

The following are explicitly NOT completed in this milestone:

- Consensus algorithm selection (D-017 OPEN RESEARCH).
- Sybil-resistance mechanism (RS-002, D-016 PROVISIONAL).
- Transaction model / account vs UTXO (OPEN RESEARCH, RS-015).
- Privacy mechanism (D-020 PROVISIONAL, RS-003 OPEN).
- Monetary policy (D-015 OPEN RESEARCH).
- Offline operation mechanism (D-018 PROVISIONAL, RS-007 OPEN).
- Fiat connectivity (RS-008 OPEN).
- DHT adoption, NAT traversal, multi-transport adapters (RS-005 OPEN, D-019 PROVISIONAL).
- Light-client proofs (RS-009 OPEN).
- Governance/upgrade signaling (RS-010 OPEN).
- Independent client implementation count (RS-012, D-035 OPEN).
- Compliance (RS-011 OPEN).

## 5. Classification Preservation

All existing decision classifications are preserved:
- D-015 OPEN RESEARCH — monetary policy (unchanged).
- D-016 PROVISIONAL — permissionless validator participation (unchanged).
- D-017 OPEN RESEARCH — BFT research direction (unchanged).
- D-018 PROVISIONAL — offline financial operation (unchanged).
- D-024 Raft as reference only (unchanged).
- D-035 OPEN RESEARCH — independent client implementation count (unchanged).
- D-036 APPROVED — fork protection requirements (unchanged).

No OPEN RESEARCH or PROVISIONAL decision is converted to APPROVED.

## 6. Recommendations for the Next Milestone

1. **Complete the transaction model research** (Priority 1) before specifying transaction
   message types or mempool behavior.
2. **Complete consensus and Sybil-resistance research** (Priority 2) before Milestone 3
   test network design.
3. **Do not implement** any consensus, validator, monetary policy, or transaction
   processing functionality in the next milestone unless an approved ADR and design
   review exists (prohibited actions, AGENTS.md).
4. **Use this specification as the contract** for independent implementation research
   (RS-012, D-035): the core protocol specification defines the boundaries within which
   independent clients must interoperate.

## 7. Cross-References

- [`../research/research-register.md`](../research/research-register.md) — Research
  register (RS-001 through RS-014).
- [`../research/README.md`](../research/README.md) — Research-first methodology.
- [`../research/research-entry-template.md`](../research/research-entry-template.md) —
  Entry template.
- [`../spec/core-protocol-specification-v0.1.md`](../spec/core-protocol-specification-v0.1.md)
  — Core protocol specification (this milestone).
- [`../architecture/node-roles.md`](../architecture/node-roles.md) — Node role
  architecture (this milestone).
- [`../security/threat-model-v0.1.md`](../security/threat-model-v0.1.md) — Network
  threat model (this milestone).
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision
  register (D-015, D-016, D-017, D-018, D-019, D-020, D-035, D-036).
- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) —
  FeinStatic baseline and transformation findings.
- [`consensus-validator-research.md`](consensus-validator-research.md) — RS-013 consensus and validator research.
- [`identity-peer-discovery-research.md`](identity-peer-discovery-research.md) — RS-014 identity and peer discovery research.
- [`transaction-model-research.md`](transaction-model-research.md) — RS-015 transaction model and state representation research.
- [`../glossary.md`](../glossary.md) — FST glossary of terms.
