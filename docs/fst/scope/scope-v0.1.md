# FST Project Scope — v0.1

**Status:** DRAFT — first recorded version of the owner-approved scope.
**Classification:** APPROVED (principles and scope as recorded by the project owner).

This document records the canonical scope of the FST project as decided by the project
owner. It is versioned so that future scope changes can supersede it without erasing
history. This document does not implement functionality; it records intent and
boundaries only.

## Project identity

FST is an independent, globally decentralised financial network. It is the network's
native currency, operating token, and settlement asset.

FST is **not**:
- a bank plugin,
- a banking application,
- a system controlled by an existing financial institution,
- a centrally controlled financial application.

## Approved principles

The following principles are approved by the project owner and govern all FST
development:

1. No single organisation should possess unilateral control over FST.
2. No organisation should have permanent privileged authority over the protocol.
3. Protocol development and important discussions should be transparent.
4. Independent development teams must be able to maintain the software.
5. Participants must retain the ability to reject proposed upgrades.
6. Major disagreements may result in independent network forks.
7. Neither fork automatically receives exclusive legitimacy.
8. No central emergency administrator or unilateral master key.
9. Security should primarily emerge from protocol design and independent verification.
10. The network should survive the disappearance of its founding developers.
11. Documentation must preserve the reasoning behind every important engineering decision.
12. Every module and implementation detail must follow research-first engineering.
13. Build the core financial network first. Add further services incrementally according
    to research, feasibility, security, and genuine need.
14. Development should proceed through small, thoroughly tested and documented releases,
    generally after approximately two or three new features.

## Native currency

FST is intended to be the network's:

- Native currency.
- Operating token.
- Settlement asset.

The initial on-network asset scope is **FST only**.

The following remain **open research**:
- Supply rules and issuance schedule.
- Monetary policy.
- Market-value model.

## Financial functionality

The long-term vision includes:

- Financial accounts.
- Payments and transfers.
- Merchant transactions.
- Institutional and wholesale transactions.
- Settlement.
- International transfers.
- FST exchange with national currencies.
- Bank deposits and withdrawals through appropriate independent infrastructure.

The core network must not depend on a single bank, exchange, custodian, or company.

These additional financial services are **not** required in the initial release. They
will be introduced incrementally according to research, feasibility, security, and
genuine need.

## Decentralisation

Permissionless validator participation is an **approved direction**.

The following remain **unresolved** (open research):
- Exact consensus algorithm.
- Sybil-resistance mechanism.
- Validator economics.
- Security assumptions.
- Finality model.

Byzantine fault tolerance is an important research direction, not an already-selected
consensus implementation.

## Offline financial operation

The network is intended to support:

- Bounded offline spending authority.
- Local financial operation during disconnection.
- Different finality rules according to transaction amount and risk.
- Regional autonomy under defined conditions.
- Reconciliation when communication returns.

Offline operation must be designed using existing research as a baseline.

**Limitation:** Unrestricted local spending and simultaneous global double-spend
prevention under arbitrary network partitions cannot both be guaranteed. This trade-off
must be addressed in the offline-operation design.

## Communication

The intended communication architecture is **modular**. The following should be
considered and introduced incrementally:

- Internet.
- Mesh networking.
- Radio links.
- Satellite links.
- Delay- and disruption-tolerant networking.

## Privacy

Strong cryptographic privacy is an **approved direction**.

The following remain **open research**:
- Specific transaction-confidentiality mechanisms.
- Identity mechanisms.
- Selective-disclosure mechanisms.

Authenticated encryption (as used in FeinStatic's TCP frames) is **not** complete
financial privacy.

## Currency migration and forks

The following migration design is **provisional**:

1. A network fork may create two independent networks and currencies.
2. Neither continuation automatically receives exclusive legitimacy.
3. Users can voluntarily migrate a chosen portion of their FST-A balance into FST-B.
4. The destination network must be fully operational, stable, independently tested, and
   appropriately reviewed before migration opens.
5. Migration receives a sufficiently long, publicly announced fixed window.
6. The deadline closes permanently, without outage-based extensions.
7. After closure, conversion occurs through ordinary markets at prevailing exchange
   rates.
8. FST-A and FST-B may develop independent monetary policies and market values.

**Unresolved:** Conversion ratios, migration mechanisms, and migration duration.

**Do not implement migration functionality** in this milestone.

## Fork protection requirements (APPROVED)

The following requirements are **approved** for any fork creating independent networks:

1. Every independent network must have a unique network identity.
2. Transactions must be protected against replay across networks.
3. Independent currencies must have distinct identifiers so wallets, exchanges and
   users cannot confuse them.

Cross-reference: [`D-036`](../decisions/decision-register.md).

The implementation mechanisms for these requirements (chain-ID format, signing scheme,
token symbols, etc.) remain **OPEN RESEARCH**. No specific mechanism is selected.

## Regional outage extensions (SUPERSEDED)

The following earlier design ideas are **superseded** and must not be implemented:

- Regional outage-based migration extensions.
- Anonymous regional credentials.
- Decentralised outage verification for migration windows.

The migration rules in this section control: migration opens only after the destination
network is fully operational and stable; the migration window is a fixed, publicly
announced period; the closing deadline is permanent; there are no outage-based
extensions; there are no regional exceptions; there are no additional special migration
periods; after closure, users may use ordinary market-based exchange between the
currencies.

Cross-reference: [`D-021`](../decisions/decision-register.md).

This supersession does not affect FST's broader requirement to support offline financial
operation, which remains part of the project vision (see Offline financial operation).

## Out of scope for this milestone

The following are explicitly **not** part of this documentation-only milestone:

- Consensus implementation.
- Blockchain networking implementation.
- Wallet implementation.
- Monetary policy implementation.
- Any application-architecture changes to FeinStatic source code.
- Modification of existing C++ source, header, CMake, test, or runtime files.

## Cross-references

- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — Approved engineering principles.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — Existing FeinStatic baseline.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications.
- [`../research/research-register.md`](../research/research-register.md) — Research register.
- [`../roadmap/development-roadmap.md`](../roadmap/development-roadmap.md) — Development roadmap.

---

## Current-Scope Amendment (2026-11-17)

This amendment records the current status of the scope document and reconciles it with later approved decisions. The original document text above is preserved unchanged.

### Document status

- **Recorded status:** DRAFT (first recorded version)
- **Classification:** APPROVED (principles and scope as recorded by the project owner)
- **Reconciliation:** The DRAFT recording status reflects the document's versioning history, not its approval status. The scope principles and boundaries are APPROVED. Subsequent decisions recorded in `docs/fst/decisions/decision-register.md` may refine or supersede specific scope items; the decision register is authoritative for classification.

### Decision classifications (verified against decision-register.md)

| Decision | ID | Classification | Source |
|----------|-----|----------------|--------|
| Monetary policy | D-015 | APPROVED | `decision-register.md`, ADR-0005 |
| Permissionless validator participation | D-016 | APPROVED | `decision-register.md`, ADR-0003 |
| BFT consensus and finality | D-017 | APPROVED | `decision-register.md`, ADR-0004 |
| Offline financial operation | D-018 | PROVISIONAL | `decision-register.md` |
| Modular multi-transport communication | D-019 | PROVISIONAL | `decision-register.md` |
| Strong cryptographic privacy | D-020 | PROVISIONAL | `decision-register.md` |
| Provisional fork and migration design | D-021 | PROVISIONAL | `decision-register.md` |
| Independent client implementation count | D-035 | OPEN RESEARCH | `decision-register.md` |
| Fork protection requirements | D-036 | APPROVED (reqs), mechanisms OPEN RESEARCH | `decision-register.md` |
| Transaction model and state representation | D-037 | APPROVED | `decision-register.md`, ADR-0002 |

### Current implementation scope

The following items, previously listed as "out of scope for this milestone" in the original document, are now part of the approved implementation scope based on APPROVED decisions D-015, D-016, D-017, and D-037:

- Consensus implementation (stake-weighted BFT with CometBFT candidate, per D-017/ADR-0004)
- Blockchain networking implementation (P2P networking foundation, per D-016/D-019)
- Monetary policy implementation (predetermined declining issuance, congestion-adjusted fees, per D-015/ADR-0005)
- Transaction model implementation (UTXO canonical ledger, per D-037/ADR-0002)

The following remain outside the current implementation scope:

- Wallet implementation (not part of approved core network scope)
- Offline operation implementation (D-018 PROVISIONAL, research only)
- Multi-transport beyond internet/TCP (D-019 PROVISIONAL, research only)
- Strong cryptographic privacy mechanisms (D-020 PROVISIONAL, research only)
- Migration functionality (D-021 PROVISIONAL, explicitly "Do not implement migration functionality in this milestone")
- Financial accounts, payments, settlement (Phase 5, DEFERRED)
- Privacy layer (Phase 6, DEFERRED)
- Mesh/radio/satellite/DTN transports (Phase 7, DEFERRED)
- Governance, upgrades, fork safety implementation (Phase 8, DEFERRED)
- Independent implementations and conformance (Phase 9, DEFERRED)
- Production hardening and mainnet readiness (Phase 10, DEFERRED)

### Distinctions

- **Long-term project vision:** Decentralised financial network with FST as native currency, operating token, and settlement asset (original scope, Section "Native currency" and "Financial functionality").
- **Approved current architecture:** UTXO ledger (D-037), permissionless PoS (D-016), stake-weighted BFT consensus with CometBFT candidate (D-017), deterministic monetary issuance and fees (D-015).
- **Current implementation scope:** Phases 0–2 complete; Phases 3–4 unblocked and in progress per `docs/fst/operations/project-plan.md`.
- **Deferred functionality:** Phases 5–10 per `docs/fst/operations/project-plan.md`.
- **Provisional directions:** D-018, D-019, D-020, D-021 (require validation before implementation).
- **Open research:** D-035, D-036 mechanisms, RS-001 through RS-015 entries per `docs/fst/research/research-register.md`.
- **Explicitly prohibited or superseded designs:** Regional outage extensions (superseded, original scope Section "Regional outage extensions"); FeinStatic BridgeService single-vault custody (conflicts with P1, P2, P8); FeinStatic Raft as production consensus (crash-fault only, not BFT); FeinStatic operator roles and trust store (permanent privileged authority).
