# FST Research-First Engineering

**Status:** APPROVED methodology — applies to every future FST module.
**Applies to:** All subsystems, features, protocols, algorithms, and implementation
details.

## Research-first methodology

Every feature, protocol, algorithm, subsystem, and implementation detail must be
investigated against existing research before design and implementation. The required
process is:

1. **Identify the engineering problem** — state the problem precisely, with scope and
   constraints.
2. **Review academic research and prior art** — survey relevant publications, including
   assumptions, limitations, and results.
3. **Study standards, RFCs, and existing implementations** — identify applicable
   technical standards and deployed systems.
4. **Identify assumptions and limitations** — document what each source assumes and
   where it falls short for FST.
5. **Define the threat model** — state the adversary model, attack surface, and
   security goals.
6. **Identify opportunities for measurable improvement** — define metrics and
   evaluation criteria.
7. **Design the FST solution** — produce a design document (ADR) with reasoning and
   trade-offs.
8. **Establish formal properties where appropriate** — specify safety/liveness
   properties, invariants, or formal models.
9. **Implement only after the design is approved** — no implementation before design
   review and owner approval.
10. **Create comprehensive tests and benchmarks** — unit, integration, adversarial,
    and performance tests with actual results.
11. **Obtain independent technical/security review where appropriate** — external review
    for security-critical components.
12. **Document evidence and remaining risks** — record sources, results, and open risks.

## Research entry requirements

Every research entry must provide:

- Source title.
- Author(s).
- Publication date.
- Publisher or venue.
- DOI or canonical URL (when available).
- Research relevance to FST.
- Limitations of the source.
- FST design implications.

Entries must distinguish:

- **Established knowledge** — widely accepted results from reviewed sources.
- **Proposed improvements** — novel ideas requiring validation.
- **Experimental work** — unvalidated prototypes or simulations.

Do not fabricate sources. Do not claim novelty without evidence.

## Research register

All research entries are tracked in
[`research-register.md`](research-register.md). Each entry receives a stable identifier
(e.g., `RS-001`) and is cross-referenced from related ADRs and decisions.

## Research domains for FST

The following domains require research before implementation (classification: OPEN
RESEARCH unless otherwise noted):

| Domain | Status | Key questions |
|--------|--------|---------------|
| Consensus algorithm | OPEN RESEARCH | BFT family selection, Sybil resistance, validator economics, finality model |
| Transaction model | OPEN RESEARCH | Account vs UTXO, fee market, confidentiality, replay protection |
| Cryptographic identity | OPEN RESEARCH | Permissionless registration, key rotation, threshold signatures, DKG |
| Network discovery | OPEN RESEARCH | DHT/bootstrap/peer exchange, NAT traversal, multi-transport |
| Monetary policy | OPEN RESEARCH | Supply schedule, issuance, governance |
| Privacy | OPEN RESEARCH | Transaction confidentiality, selective disclosure, identity |
| Offline operation | OPEN RESEARCH | Bounded spending authority, reconciliation under partitions |
| Bridge / national currency exchange | OPEN RESEARCH | Decentralised FST<->fiat exchange without custodial vault |
| Light-client proofs | OPEN RESEARCH | State commitments, Merkle proofs, sync protocols |
| Governance and upgrades | OPEN RESEARCH | On-chain signaling, participant veto, fork legitimacy |

## Cross-references

- [`research-register.md`](research-register.md) — Register of research entries.
- [`research-entry-template.md`](research-entry-template.md) — Template for new entries.
- [`../adr/adr-template.md`](../adr/adr-template.md) — ADR template for recording designs.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications.
- [`../../scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — FST project scope.
- [`../../principles/engineering-principles.md`](../../principles/engineering-principles.md) — Approved principles (P12: research-first engineering).
