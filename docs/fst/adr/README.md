# FST Architecture Decision Records (ADRs)

**Status:** INITIAL — no ADRs recorded yet.
**Applies to:** All significant architectural decisions in FST.

## Purpose

ADRs record significant architectural decisions, their context, alternatives considered,
and consequences. They preserve the reasoning behind decisions so that future
contributors (including independent development teams) can understand *why* a design was
chosen, not just *what* was chosen.

## ADR format

Each ADR follows [`adr-template.md`](adr-template.md) and records:

- Decision identifier (e.g., `ADR-001`).
- Date.
- Status (PROPOSED / ACCEPTED / SUPERSEDED / DEPRECATED).
- Problem statement.
- Existing research and exact references.
- Alternatives evaluated.
- Reasoning and trade-offs.
- Selected design (if approved).
- Security assumptions.
- Limitations and trade-offs.
- Implementation locations.
- Verification evidence.
- Superseding decisions, if applicable.

## Decision classifications

Every ADR and the decisions it records must be classified as:

- **APPROVED** — Explicit owner decision.
- **PROVISIONAL** — Preferred direction requiring validation.
- **OPEN RESEARCH** — Unresolved decision requiring investigation.

Never convert a discussion or suggestion into an approved design without owner agreement.

## ADR register

| ID | Status | Title | Date | Classification | Supersedes |
|----|--------|-------|------|----------------|------------|
| ADR-0001 | PROPOSED | Candidate-neutral multi-node network integration and fault-injection test harness | 2026-11-15 | OPEN RESEARCH pending owner review | — |
| ADR-0002 | ACCEPTED | UTXO ledger and transaction/state architecture | 2026-11-17 | APPROVED (D-037) | — |
| ADR-0003 | ACCEPTED | Permissionless native-FST Proof-of-Stake validator eligibility | 2026-11-17 | APPROVED (D-016) | — |
| ADR-0004 | ACCEPTED | BFT consensus, finality and CometBFT integration | 2026-11-17 | APPROVED (D-017) | — |
| ADR-0005 | ACCEPTED | Monetary issuance and transaction-fee architecture | 2026-11-17 | APPROVED (D-015) | — |

## How to create a new ADR

1. Copy [`adr-template.md`](adr-template.md) to a new file named
   `adr-<NNNN>-<short-title>.md` in this directory.
2. Complete all fields using the research-first methodology
   ([`../../research/README.md`](../../research/README.md)).
3. Add a row to the register table above.
4. Cross-reference from the
   [`../decisions/decision-register.md`](../decisions/decision-register.md).

## Cross-references

- [`adr-template.md`](adr-template.md) — ADR template.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision register.
- [`../../research/research-register.md`](../../research/research-register.md) — Research register.
- [`../../scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — FST project scope.
- [`../../principles/engineering-principles.md`](../../principles/engineering-principles.md) — Approved principles.
