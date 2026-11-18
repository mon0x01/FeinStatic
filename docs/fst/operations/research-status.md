# FST Research Status — Research Progress Ledger

**Status:** ESTABLISHED — fifth role-owned operational ledger for the fst-research agent.
**Version:** 1.0
**Date:** 2026-11-18
**Classification:** Live document — updated by the Research Agent (fst-research) at every durable research checkpoint.
**Exclusive writer:** fst-research (Research Agent). No other agent may edit this ledger.

This ledger tracks the research function of the permanent five-agent FST engineering model. Research is a mandatory gate for significant technology or architecture selections. The Research Agent discovers, evaluates, and recommends technical foundations; it does not implement production code, modify production tests/CI, change accepted ADRs or approved scope, select tokenomics/consensus parameters, merge code, publish releases, or approve its own recommendations.

Authoritative references: `docs/fst/engineering-charter.md` (common doctrine), `docs/fst/decisions/decision-closure-roadmap.md` (classification and closure conditions), `docs/fst/scope/scope-v0.1.md` (approved scope), `docs/fst/decisions/decision-register.md` (APPROVED / PROVISIONAL / OPEN RESEARCH classifications).

---

## Current research checkpoint

| Field | Value |
|---|---|
| Current research phase | Foundation — research agent and ledger established (this migration) |
| Research branch | `team/research` |
| Research worktree | `/home/monil/Desktop/CXX/FeinStatic-agents/research` |
| Active research tasks | None (pilot pending) |
| Technology candidates under evaluation | None |
| Last research deliverable | None (pilot report pending) |
| Last verified commit SHA | None |
| Prototype/benchmark evidence | None |
| Proposed ADRs awaiting owner decision | None |
| Accepted recommendations | None |
| Rejected recommendations | None |
| Blocked research | None |
| Future-phase research roadmap | See `docs/fst/decisions/decision-closure-roadmap.md` (M-03, M-04, M-05, M-06, M-07, M-08, M-10, M-14) |

---

## Research task queue

| Queue ID | Title | Type | Status | Dependencies | Target |
|---|---|---|---|---|---|
| RQ-01 | Pilot: validate research deliverable format and sourced reporting | Pilot | PENDING | Research agent definition + ledger established | Phase 0 (migration) |
| RQ-02 | CometBFT feasibility, security and real multi-validator acceptance evidence (D-017, M-03) | Feasibility study | BLOCKED-on-research-gate | CometBFT feasibility spike (`docs/fst/architecture/cometbft-integration-feasibility-spike.md`) | Phase 3–4 |
| RQ-03 | Offline spending bounds and reconciliation (D-018, M-04) | Research | DEFERRED-BY-DESIGN | D-018 PROVISIONAL; double-spend/finality analysis | Phase 5 |
| RQ-04 | Multi-transport architecture interface (D-019, M-05) | Research | DEFERRED-BY-DESIGN | D-019 PROVISIONAL | Phase 3 (interface) |
| RQ-05 | Financial privacy mechanisms (D-020, M-06) | Research | DEFERRED-BY-DESIGN | D-020 PROVISIONAL | Phase 5 |
| RQ-06 | Migration and fork conversion mechanisms (D-021, M-07) | Research | DEFERRED-BY-DESIGN | D-021 PROVISIONAL; D-036 APPROVED | Phase 6 |
| RQ-07 | Independent client implementation count (D-035, M-08) | Research | OPEN | D-035 OPEN RESEARCH | Phase 5+ |
| RQ-08 | Cryptographic primitives and key lifecycle (M-10) | Research | DEFERRED-BY-DESIGN | Cryptographic review | Phase 4 |
| RQ-09 | State synchronization and light-client proofs (M-14) | Research | DEFERRED-BY-DESIGN | Node-roles research | Phase 5 |

---

## Technology candidates under evaluation

| Candidate | Domain | Status | Source version / access date | Summary |
|---|---|---|---|---|
| None | — | — | — | — |

---

## Source verification and research freshness

| Source | Title | URL | Access date | Version/edition | Verified by | Freshness check due |
|---|---|---|---|---|---|---|
| None | — | — | — | — | — | — |

---

## Research deliverables and exact commit SHAs

| Deliverable ID | Title | Type | Commit SHA | Date | Owner decision required? |
|---|---|---|---|---|---|
| None | — | — | — | — | — |

---

## Prototype / benchmark evidence

| Evidence ID | Title | Commit SHA | Result | Log location |
|---|---|---|---|---|
| None | — | — | — | — |

---

## Proposed ADRs awaiting owner decision

| ADR ID | Title | Proposed by | Date | Status | Owner decision due |
|---|---|---|---|---|---|
| None | — | — | — | — | — |

---

## Accepted and rejected recommendations

| Recommendation | Title | Outcome | Rationale | Date |
|---|---|---|---|---|
| None | — | — | — | — |

---

## Dependencies and blocked research

| Blocked item | Blocked by | Required action | Unblock condition |
|---|---|---|---|
| None | — | — | — |

---

## Future-phase research roadmap

Research for Phase 5–10 matters (M-04 offline operation, M-05 multi-transport, M-06 privacy, M-07 migration/fork conversion, M-08 independent client count, M-10 cryptographic primitives, M-14 state sync/light clients) is scoped but not started. It begins only after the Phase 3–4 research gates (M-03 CometBFT feasibility, M-10 primitives) and the corresponding owner decisions are met.

---

## Research-before-implementation obligations (from `docs/fst/engineering-charter.md`)

1. No significant technology or architecture selection without a research investigation.
2. Every substantial research investigation contains: FST requirements and constraints; candidate technologies and alternatives; comparison matrix; security and threat assessment; performance and scalability evidence; maintenance, maturity and release information; licensing and dependency assessment; C++ integration assessment; future compatibility analysis; known weaknesses and unresolved questions; recommendation with explicit rationale; rejected alternatives and reasons; prototype and reproducible benchmark plan where justified; sources/citations/URLs/verification dates; proposed ADR where an owner decision is required.
3. Research is a mandatory gate; implementation proceeds only for approved and sufficiently specified work.
4. No research result automatically becomes an approved protocol decision. The Research Agent does not approve its own recommendations.
5. All research is independently reviewable; sources, citations, URLs and verification dates are recorded.
