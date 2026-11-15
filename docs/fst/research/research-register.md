# FST Research Register

**Status:** INITIAL — no research entries recorded yet.
**Classification:** All entries below are placeholders pending owner-commissioned research.

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
