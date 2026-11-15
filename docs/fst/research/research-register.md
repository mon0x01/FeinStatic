# FST Research Register

**Status:** ACTIVE — RS-013 and RS-014 have dedicated research documents and remain OPEN.
**Classification:** RS-001 through RS-014 are tracked OPEN research entries.

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
| RS-001 | OPEN | Consensus algorithms for permissionless networks | TBD | Consensus algorithm selection (BFT vs Nakamoto vs hybrid) |
| RS-002 | OPEN | Sybil-resistance mechanisms | TBD | Validator entry, staking, or alternative Sybil resistance |
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
| RS-013 | OPEN | Permissionless consensus and validator architecture | docs/fst/research/consensus-validator-research.md | Consensus family comparison, validator membership, Sybil resistance, validator economics, network resilience, security assumptions, formal verification, existing implementations. Nine-component consensus taxonomy added (Sybil resistance, validator eligibility, validator selection, proposer selection, transaction dissemination, agreement and ordering, fork choice, finality, state-machine execution). Comparison matrix reorganised: separate validator-set assumptions from permissionless admission, separate Sybil resistance from agreement/finality, Ouroboros Praos and Gasper listed separately, Narwhal dissemination separated from Tusk consensus, unsupported O(n)-per-transaction bandwidth and fixed latency ranges removed. Validator economics strengthened with Irresberger & Yang 2023 (Economics Letters) and Daian et al. 2020 (IEEE S&P). 22 references PARTIALLY VERIFIED (metadata recorded from task description; full papers not inspected — no external web access). 3 sources removed as UNRESOLVED (AlephBFT, Avalanche-family, Chia). Verification date: 2026-11-15. |
| RS-014 | OPEN | Identity, peer discovery and P2P network architecture | docs/fst/research/identity-peer-discovery-research.md | Seven-domain identity model (account, node, validator, session, service, human/organisation, endpoint) with strict separation; hybrid bootstrap + signed node-record + peer-exchange discovery architecture (Candidate 3, PROVISIONAL); Noise-based secure-channel evaluation; modular transport adapters (internet, TCP/QUIC, mesh, radio, satellite, DTN); threat-to-mitigation matrix (Sybil, eclipse, DHT poisoning, record forgery, connection exhaustion, malicious relays, key theft); FeinStatic source reuse assessment based on actual inspection of src/network/, src/crypto/, include/feinstat/network/, include/feinstat/crypto/, tests/. **Implementation begun in Milestone 2C:** NodeIdentity key-provider abstraction, versioned NodeId, SignedNodeRecord canonical serialization and validation, SQLite-backed PeerStore, BootstrapDiscovery, PeerExchange with PeerAuthenticator interface. See docs/fst/architecture/node-identity-peer-discovery-implementation.md. No implementation code was produced in RS-014 itself; it remains a research document. Sybil resistance, BFT consensus, exact Noise pattern, DHT adoption, node-record format, post-quantum agility, and DTN/RFC 9171 status all remain OPEN RESEARCH. Reviewer-supplied findings (libp2p, ENR/discv5, Bitcoin devguide, eclipse-attack research, Noise, DID, RFC 9171) NOT independently inspected — cited as reviewer-supplied findings only. Verification date: 2026-11-15. |

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
- [`../adr/adr-template.md`](../adr/adr-template.md) — ADR template.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision register.
- [`../../scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — FST project scope.
