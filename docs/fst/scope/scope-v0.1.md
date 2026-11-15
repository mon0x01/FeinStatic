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
