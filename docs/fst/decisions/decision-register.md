# FST Decision Register

**Status:** INITIAL — records the first set of decisions from the FST transformation
assessment.
**Classifications:** APPROVED / PROVISIONAL / OPEN RESEARCH.

This register records all significant FST decisions with their classification. Decisions
recorded here as APPROVED are owner decisions and must not be silently changed. PROVISIONAL
decisions require technical validation before adoption. OPEN RESEARCH decisions require
investigation and discussion.

## Decision classifications

| Classification | Meaning | Can be implemented? |
|----------------|---------|---------------------|
| APPROVED | Explicit owner decision | Yes, per approved design |
| PROVISIONAL | Preferred direction requiring validation | Only as research/prototype; not as production design |
| OPEN RESEARCH | Unresolved decision requiring investigation | No — research only |

## Decision register

| ID | Date | Title | Classification | Summary | Location |
|----|------|-------|----------------|---------|----------|
| D-001 | 2026-11-15 | FST is an independent global financial network | APPROVED | FST is not a bank plugin or centrally controlled application. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-002 | 2026-11-15 | No unilateral organisational control | APPROVED | No single organisation possesses unilateral control over FST. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-003 | 2026-11-15 | No permanent privileged protocol authority | APPROVED | No organisation has permanent privileged authority over the protocol. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-004 | 2026-11-15 | Transparent development and governance | APPROVED | Protocol development and important discussions are transparent. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-005 | 2026-11-15 | Independent maintainability | APPROVED | Independent development teams must be able to maintain the software. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-006 | 2026-11-15 | Participants can reject upgrades | APPROVED | Participants retain the ability to reject proposed upgrades. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-007 | 2026-11-15 | Forks may be legitimate | APPROVED | Major disagreements may produce independent forks; neither has exclusive legitimacy. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-008 | 2026-11-15 | No central emergency administrator | APPROVED | No central emergency administrator or unilateral master key. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-009 | 2026-11-15 | Security from protocol design and verification | APPROVED | Security emerges from protocol design and independent verification. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-010 | 2026-11-15 | Founder independence | APPROVED | The network should survive the disappearance of its founding developers. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-011 | 2026-11-15 | Research-first engineering | APPROVED | Every module and implementation detail follows research-first engineering. | [`principles/engineering-principles.md`](../principles/engineering-principles.md) |
| D-012 | 2026-11-15 | Core network first, incremental services | APPROVED | Build the core financial network first; add services incrementally. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-013 | 2026-11-15 | Small, tested, documented releases | APPROVED | Releases after approximately two or three new features. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-014 | 2026-11-15 | Native currency scope: FST only | APPROVED | Initial on-network asset scope is FST only. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-015 | 2026-11-15 | Monetary policy open research | OPEN RESEARCH | Supply, issuance, monetary policy, and market-value model unresolved. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-016 | 2026-11-15 | Permissionless validator participation | PROVISIONAL | Preferred direction; exact mechanism unresolved. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-017 | 2026-11-15 | Byzantine fault tolerance research direction | OPEN RESEARCH | BFT is an important research direction, not a selected implementation. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-018 | 2026-11-15 | Offline financial operation | PROVISIONAL | Bounded offline spending, local operation, reconciliation; trade-offs unresolved. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-019 | 2026-11-15 | Modular multi-transport communication | PROVISIONAL | Internet, mesh, radio, satellite, delay-tolerant; incremental introduction. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-020 | 2026-11-15 | Strong cryptographic privacy | PROVISIONAL | Approved direction; specific mechanisms unresolved. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-021 | 2026-11-15 | Provisional fork and migration design | PROVISIONAL | Fork legitimacy, voluntary migration, fixed window, market conversion after closure. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |
| D-022 | 2026-11-15 | FeinStatic Ledger data structures reusable | PROVISIONAL | Potentially reusable after specification review. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-023 | 2026-11-15 | FeinStatic cryptographic primitives require review | PROVISIONAL | Reuse only after cryptographic and security review. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-024 | 2026-11-15 | FeinStatic Raft preserved as reference | PROVISIONAL | Research replacement consensus; Raft is crash-fault only. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-025 | 2026-11-15 | FeinStatic TCP networking patterns reusable | PROVISIONAL | Research new P2P networking architecture; preserve useful patterns. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-026 | 2026-11-15 | FeinStatic Match algorithms reference | PROVISIONAL | Preserve matching algorithms as reference. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-027 | 2026-11-15 | FeinStatic Flow arithmetic reusable | PROVISIONAL | Potentially reuse mathematical and arithmetic components. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-028 | 2026-11-15 | FeinStatic SettlementBook concepts reference | PROVISIONAL | Preserve useful accounting concepts as reference. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-029 | 2026-11-15 | FeinStatic FinancialPlatform requires redesign | PROVISIONAL | Single-writer coordinator conflicts with decentralisation. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-030 | 2026-11-15 | FeinStatic BridgeService conflicts with decentralisation | PROVISIONAL | Single-vault custody conflicts with P1, P2, P8. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-031 | 2026-11-15 | FeinStatic SQLite persistence reusable | PROVISIONAL | Reuse appropriate persistence engineering after scalability assessment. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-032 | 2026-11-15 | FeinStatic snapshot patterns reusable | PROVISIONAL | Preserve crash-recovery and streaming patterns. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-033 | 2026-11-15 | FeinStatic CLI is development tool | PROVISIONAL | Not the eventual network interface. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-034 | 2026-11-15 | FeinStatic tests preserved and extended | PROVISIONAL | Preserve and extend testing patterns. | [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) |
| D-035 | 2026-11-15 | Independent client implementation count | OPEN RESEARCH | Research one, two, or three independent client implementations before mainnet; no final number selected. | [`research/research-register.md`](../research/research-register.md) |
| D-036 | 2026-11-15 | Fork protection requirements | APPROVED | Unique network identity, replay protection, distinct currency identifiers for independent networks. Implementation mechanisms open research. | [`scope/scope-v0.1.md`](../scope/scope-v0.1.md) |

## How to add a new decision

1. Create an ADR in [`../adr/`](../adr/) using [`adr-template.md`](../adr/adr-template.md).
2. Add a row to the register table above with a new ID (D-035, D-036, ...).
3. Classify the decision as APPROVED, PROVISIONAL, or OPEN RESEARCH.
4. Cross-reference from the ADR and any related research entries.

## Cross-references

- [`../adr/adr-template.md`](../adr/adr-template.md) — ADR template.
- [`../adr/README.md`](../adr/README.md) — ADR overview.
- [`../research/research-register.md`](../research/research-register.md) — Research register.
- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope.
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — Approved principles.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — FeinStatic baseline.
