# FST Research Register

**Status:** ACTIVE — RS-002, RS-013, RS-014 and RS-015 have dedicated research documents and remain OPEN.
**Classification:** RS-001 through RS-015 are tracked OPEN research entries. RS-001 and RS-002 are interdependent but not interchangeable (RS-001: consensus algorithms and architecture; RS-002: Sybil-resistance mechanisms).

This register tracks all research that informs FST design decisions. Each entry has a
stable identifier, status, and cross-references to related ADRs and decisions.

## Entry format

Each research entry follows this structure:

| Field | Description |
|-------|-------------|
| ID | Stable identifier (e.g., `RS-001`) |
| Status | OPEN / IN PROGRESS / COMPLETE / SUPERSEDED |
| Title | Research topic title |
| Source | Title, author, date, publisher, DOI/URL |
| Relevance | Why this research matters to FST |
| Limitations | Known limitations of the source |
| Design implications | How this research affects FST design |
| Related ADRs | ADR identifiers that cite this entry |
| Related decisions | Decision identifiers that cite this entry |

## Register entries

| ID | Status | Title | Source | Design implications |
|----|--------|-------|--------|---------------------|
| RS-001 | OPEN | Consensus algorithms for permissionless networks | docs/fst/research/consensus-validator-research.md | Consensus algorithm selection (classical BFT, leader-based BFT/HotStuff, Tendermint/CometBFT, Nakamoto PoW, PoS longest-chain/Ouroboros Praos, PoS checkpoint finality/Gasper, DAG-based/Narwhal-Tusk, asynchronous/randomized BFT). Nine-component consensus taxonomy; composition analysis; network and financial requirements; monetary and economic dependencies; transaction-model dependencies. 30 sources inspected (19 VERIFIED, 11 PARTIALLY VERIFIED; 0 UNRESOLVED at metadata level). PBFT, FLP, DLS, Byzantine Generals, Sybil, CAP, Nakamoto, HotStuff, CometBFT, Narwhal/Tusk, Ouroboros, Praos, Casper FFG, Gasper verified against primary sources. AlephBFT, Avalanche-family, Chia resolved to authoritative sources. No consensus algorithm, threshold, or finality model selected. |
| RS-002 | OPEN | Sybil-resistance mechanisms | docs/fst/research/sybil-resistance-research.md | Proof of Work, Proof of Stake, Delegated Proof of Stake, Proof of Space/Proof of Space-Time, Proof of Authority (comparison baseline only), reputation/social-trust/Proof-of-Personhood, resource-based/hybrid approaches. Six concept distinctions (node identity, peer admission, validator eligibility, validator selection, voting power, quorum formation). Concrete attack analysis (identity multiplication, cheap-identity, stake concentration, mining-pool concentration, delegation centralisation, nothing-at-stake, long-range, stake grinding, adaptive corruption, bribery/collusion, censorship, wealth concentration, infrastructure/geographic concentration, governance capture). Seven candidate consensus–Sybil compositions (C1–C7). Network, financial, monetary, and transaction-model dependencies. 60 sources inspected (44 VERIFIED, 16 PARTIALLY VERIFIED). No Sybil-resistance mechanism selected. |
| RS-003 | OPEN | Transaction confidentiality and privacy | TBD | Privacy layer design for FST transactions |
| RS-004 | OPEN | Permissionless cryptographic identity | TBD | Identity registration, key rotation, threshold signatures |
| RS-005 | OPEN | P2P network discovery and multi-transport | TBD | DHT, bootstrap, NAT traversal, mesh/radio/satellite |
| RS-006 | OPEN | Monetary policy and supply mechanics | TBD | FST supply, issuance, governance |
| RS-007 | OPEN | Offline operation and bounded spending | TBD | Local spending authority, reconciliation |
| RS-008 | OPEN | Decentralised bridge to national currencies | TBD | Non-custodial FST<->fiat exchange |
| RS-009 | OPEN | Light-client state proofs | TBD | Merkle commitments, sync protocols, pruning |
| RS-010 | OPEN | Governance and on-chain upgrades | TBD | Upgrade signaling, participant veto, fork legitimacy |
| RS-011 | OPEN | Compliance | TBD | Regulatory boundaries, jurisdictional differences, protocol vs service-provider responsibilities, fiat on/off-ramp requirements, custodian/exchange/liquidity-provider compliance, lawful services without unilateral protocol control |
| RS-012 | OPEN | Independent client implementation count | TBD | Research one, two, or three independent client implementations before mainnet |
| RS-013 | OPEN | Permissionless consensus and validator architecture | docs/fst/research/consensus-validator-research.md | Consensus family comparison, validator membership, Sybil resistance, validator economics, network resilience, security assumptions, formal verification, existing implementations. Nine-component consensus taxonomy added (Sybil resistance, validator eligibility, validator selection, proposer selection, transaction dissemination, agreement and ordering, fork choice, finality, state-machine execution). Comparison matrix reorganised: separate validator-set assumptions from permissionless admission, separate Sybil resistance from agreement/finality, Ouroboros Praos and Gasper listed separately, Narwhal dissemination separated from Tusk consensus, unsupported O(n)-per-transaction bandwidth and fixed latency ranges removed. Validator economics strengthened with Irresberger & Yang 2023 (Economics Letters) and Daian et al. 2020 (IEEE S&P). Deepened in Milestone 2G with consensus–Sybil composition analysis (Section 18), network and financial requirements (Section 19), monetary and economic dependencies (Section 20), transaction-model dependencies (Section 21), and verified source records (Section 22). 22 original references PARTIALLY VERIFIED (metadata recorded; full papers not inspected in earlier rounds) now supplemented by primary-source inspection: 19 VERIFIED, 11 PARTIALLY VERIFIED, 0 UNRESOLVED at metadata level. Verification date: 2026-11-15. |
| RS-014 | OPEN | Identity, peer discovery and P2P network architecture | docs/fst/research/identity-peer-discovery-research.md | Seven-domain identity model (account, node, validator, session, service, human/organisation, endpoint) with strict separation; hybrid bootstrap + signed node-record + peer-exchange discovery architecture (Candidate 3, PROVISIONAL); Noise-based secure-channel evaluation; modular transport adapters (internet, TCP/QUIC, mesh, radio, satellite, DTN); threat-to-mitigation matrix (Sybil, eclipse, DHT poisoning, record forgery, connection exhaustion, malicious relays, key theft); FeinStatic source reuse assessment based on actual inspection of src/network/, src/crypto/, include/feinstat/network/, include/feinstat/crypto/, tests/. **Implementation begun in Milestone 2C:** NodeIdentity key-provider abstraction, versioned NodeId, SignedNodeRecord canonical serialization and validation, SQLite-backed PeerStore, BootstrapDiscovery, PeerExchange with PeerAuthenticator interface. See docs/fst/architecture/node-identity-peer-discovery-implementation.md. **Milestone 2D implementation:** TLS 1.3 mutual-authenticated secure sessions via OpenSSL/Boost.Asio SSL with self-signed certificates and Ed25519 identity binding over TLS certificate hash (no central CA); dynamic peer admission via AdmissionController with connection limits, duplicate resolution, and bounded-backoff reconnection; versioned bounded MessageDispatcher; NetworkRuntime lifecycle integration (startup, identity loading, TLS credential generation, peer store, bootstrap discovery, peer exchange, admission, message dispatch, graceful shutdown, restart recovery); feinstatic-demo multi-process executable. PeerStore, BootstrapDiscovery, and PeerExchange from M2C are integrated and extended. No implementation code was produced in RS-014 itself; it remains a research document. Sybil resistance, BFT consensus, exact Noise pattern, DHT adoption, node-record format, post-quantum agility, and DTN/RFC 9171 status all remain OPEN RESEARCH. Reviewer-supplied findings (libp2p, ENR/discv5, Bitcoin devguide, eclipse-attack research, Noise, DID, RFC 9171) NOT independently inspected — cited as reviewer-supplied findings only. Verification date: 2026-11-15. |

| RS-015 | OPEN | Transaction Model and State Representation | docs/fst/research/transaction-model-research.md | Comparison of UTXO, account-based, eUTXO and hybrid/object-cell state representations; transaction lifecycle; security and privacy; fee-market alternatives; financial-use-case suitability; storage and scalability; implementation complexity. Two candidate architectures for FST with an evidence-backed provisional recommendation. Feeds transaction serialization (spec Section 8), mempool, dissemination, replay protection (D-036), fee market (D-015), offline operation (D-018), light-client proofs (RS-009), monetary policy (D-015). Cross-cutting dependency for later subsystems. **RS-015 is complete as a research deliverable but remains OPEN RESEARCH pending owner selection of the transaction model; no model is approved.** |

**RS-015 status note:** RS-015 is complete as a research deliverable (document exists, survey and provisional recommendation recorded) but remains OPEN RESEARCH pending owner selection of the transaction model. It is used as an input (comparison case) to RS-001/RS-002 consensus and Sybil-resistance research; no transaction model is approved or silently selected.

## How to add a new entry

1. Copy [`research-entry-template.md`](research-entry-template.md) to a new file
   named `rs-<NNNN>-<short-title>.md` in this directory.
2. Complete all fields using the research-first methodology
   ([`README.md`](README.md)).
3. Add a row to the register table above.
4. Cross-reference from any related ADR or decision.

## Cross-references

- [`README.md`](README.md) — Research-first methodology.
- [`research-entry-template.md`](research-entry-template.md) — Entry template.
- [`research-dependency-assessment.md`](research-dependency-assessment.md) — Research
  dependency and priority assessment (Milestone 2E).
- [`../adr/adr-template.md`](../adr/adr-template.md) — ADR template.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications.
- [`../../scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — FST project scope.
- [`../security/adversary-model-v0.1.md`](../security/adversary-model-v0.1.md) — FST adversary model (Milestone 2H; OPEN RESEARCH; cross-references RS-001/RS-002/RS-013).
- [`../architecture/permissionless-test-network-evaluation.md`](../architecture/permissionless-test-network-evaluation.md) — Deep consensus–Sybil evaluation (Milestone 2H; OPEN RESEARCH).
- [`../architecture/permissionless-test-network-decision-brief.md`](../architecture/permissionless-test-network-decision-brief.md) — Owner decision briefs (Milestone 2H; no decision approved).
