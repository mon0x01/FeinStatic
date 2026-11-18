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
| Current research phase | Pilot — CometBFT consensus candidate investigation (PILOT-COMETBFT-2026-11-18); research-gate portion complete, executable prototype evidence blocked by missing Go 1.25.0 toolchain |
| Research branch | `team/research` |
| Research worktree | `/home/monil/Desktop/CXX/FeinStatic-agents/research` |
| Active research tasks | PILOT-COMETBFT-2026-11-18 (report delivered); RQ-02 executable evidence pending Go toolchain |
| Technology candidates under evaluation | CometBFT v0.40.0 (see technology candidates table below) |
| Last research deliverable | `docs/fst/research/pilot-cometbft-consensus-candidate-2026-11-18.md` (commit SHA recorded in deliverables table below) |
| Last verified commit SHA | None (no executable evidence produced; prototype blocked by Go toolchain) |
| Prototype/benchmark evidence | None (Go 1.25.0 not installed; prerequisite, not an architectural blocker) |
| Proposed ADRs awaiting owner decision | ADR-0006 (outline) — CometBFT v0.40.0 as committed consensus engine dependency |
| Accepted recommendations | None (recommendation proposed, not owner-approved) |
| Rejected recommendations | Nakamoto PoW/C3; FeinStatic Raft/D-024; CometBFT `main` branch (see pilot report Section 13) |
| Blocked research | RQ-02 executable prototype/multi-validator evidence — blocked by missing Go 1.25.0 toolchain |
| Future-phase research roadmap | See `docs/fst/decisions/decision-closure-roadmap.md` (M-03, M-04, M-05, M-06, M-07, M-08, M-10, M-14) |

---

## Research task queue

| Queue ID | Title | Type | Status | Dependencies | Target |
|---|---|---|---|---|---|
| RQ-01 | Pilot: validate research deliverable format and sourced reporting | Pilot | COMPLETE | Research agent definition + ledger established | Phase 0 (migration) |
| RQ-02 | CometBFT feasibility, security and real multi-validator acceptance evidence (D-017, M-03) | Feasibility study | RESEARCH-GATE-COMPLETE; executable evidence BLOCKED-on-Go-toolchain | CometBFT feasibility spike (`docs/fst/architecture/cometbft-integration-feasibility-spike.md`); Go 1.25.0 toolchain | Phase 3–4 |
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
| CometBFT v0.40.0 | Consensus engine (BFT, deterministic finality) | Under evaluation — APPROVED candidate (D-017/ADR-0004), feasibility-gated; not a committed production dependency | v0.40.0, release 2026-07-27, commit 0880b4d; local spike accessed 2026-11-18 | Leader-based stake-weighted BFT; ABCI++ over local Unix-domain socket; Apache-2.0; Go 1.25.0; all listed advisories patched in v0.40.0 (inference basis for CSA-2026-001); executable prototype blocked by missing Go toolchain |
| HotStuff / C1 composition | Consensus alternative | OPEN RESEARCH fallback | RS-013 (verification date 2026-11-15) | Leader-based BFT, O(n) per view; less mature deployment |
| Nakamoto PoW / C3 | Consensus alternative | Not viable (D-017 conflict — probabilistic finality) | RS-013 (verification date 2026-11-15) | Probabilistic finality; high energy; slow settlement |
| Gasper / C5 | Consensus alternative | OPEN RESEARCH fallback | RS-013 / candidate-c5-evaluation (accessed 2026-11-18) | Weak subjectivity (P9 tension); stake concentration; long-range attacks |
| Narwhal/Tusk / C6 | Consensus alternative | OPEN RESEARCH fallback | RS-013 / candidate-c6-evaluation (accessed 2026-11-18) | No intrinsic Sybil resistance; no unified composition proof |
| FeinStatic Raft / D-024 | Consensus reference | Reference only — crash-fault, not BFT | RS-013 / decision-register (D-024 PROVISIONAL) | Not Byzantine; not viable for D-017 |

---

## Source verification and research freshness

| Source | Title | URL | Access date | Version/edition | Verified by | Freshness check due |
|---|---|---|---|---|---|---|
| `docs/fst/architecture/cometbft-integration-feasibility-spike.md` | CometBFT Integration Feasibility Spike (FST-SPIKE-COMETBFT-001) | internal (local doc) | 2026-11-18 | COMPLETE, 2026-11-17 | fst-research | 2026-12-18 (re-verify CSA-2026-001 inclusion in v0.40.0 tree when Go toolchain available) |
| `docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md` | ADR-0004 BFT consensus, finality and CometBFT integration | internal (local doc) | 2026-11-18 | ACCEPTED, 2026-11-17 | fst-research | 2026-12-18 |
| `docs/fst/decisions/decision-register.md` | FST Decision Register | internal (local doc) | 2026-11-18 | D-001–D-037 (D-017/D-016/D-037/D-015 APPROVED) | fst-research | 2026-12-18 |
| `docs/fst/scope/scope-v0.1.md` | FST Project Scope v0.1 | internal (local doc) | 2026-11-18 | APPROVED; Current-Scope Amendment 2026-11-17 | fst-research | 2026-12-18 |
| `docs/fst/engineering-charter.md` | FST Engineering Charter | internal (local doc) | 2026-11-18 | APPROVED | fst-research | 2026-12-18 |
| `docs/fst/decisions/decision-closure-roadmap.md` | FST Decision Closure Roadmap | internal (local doc) | 2026-11-18 | M-03 DEFERRED-BY-DESIGN | fst-research | 2026-12-18 |
| `docs/fst/operations/project-plan.md` | FST Project Plan | internal (local doc) | 2026-11-18 | v1.0, 2026-11-16 | fst-research | 2026-12-18 |
| `docs/fst/research/consensus-validator-research.md` | RS-013 Consensus and Validator Research | internal (local doc) | 2026-11-18 | OPEN RESEARCH; verification date 2026-11-15 | fst-research | 2026-12-18 |
| `docs/fst/research/candidate-c5-evaluation.md` | Candidate C5 Evaluation (Gasper-style) | internal (local doc) | 2026-11-18 | OPEN RESEARCH | fst-research | 2026-12-18 |
| `docs/fst/research/candidate-c6-evaluation.md` | Candidate C6 Evaluation (Narwhal/Tusk-style) | internal (local doc) | 2026-11-18 | OPEN RESEARCH | fst-research | 2026-12-18 |
| `docs/fst/research/candidate-c5-c6-comparative-assessment.md` | Comparative Assessment — C5 vs C6 | internal (local doc) | 2026-11-18 | OPEN RESEARCH | fst-research | 2026-12-18 |
| `docs/fst/security/adversary-model-v0.1.md` | FST Adversary Model v0.1 | internal (local doc) | 2026-11-18 | DRAFT, OPEN RESEARCH | fst-research | 2026-12-18 |
| `docs/fst/security/threat-model-v0.1.md` | FST Network Threat Model v0.1 | internal (local doc) | 2026-11-18 | DRAFT, PROVISIONAL | fst-research | 2026-12-18 |
| Root `README.md` | FST repository README | internal (local doc) | 2026-11-18 | current tree | fst-research | 2026-12-18 |
| `CMakePresets.json` | CMake build presets | internal (local file) | 2026-11-18 | version 6 | fst-research | 2026-12-18 |
| External URLs cited within local sources | github.com/cometbft/cometbft/releases/latest; docs.cometbft.com; pkg.go.dev; arxiv.org papers; GHSA advisories | not fetched (local-source constraint) | 2026-11-18 | as cited in local docs | fst-research | 2026-12-18 |

---

## Research deliverables and exact commit SHAs

| Deliverable ID | Title | Type | Commit SHA | Date | Owner decision required? |
|---|---|---|---|---|---|
| PILOT-COMETBFT-2026-11-18 | CometBFT as the FST consensus candidate (D-017 / M-03) — pilot research report | Research report | <commit SHA to be filled at commit> | 2026-11-18 | Yes (proposed ADR-0006) |

---

## Prototype / benchmark evidence

| Evidence ID | Title | Commit SHA | Result | Log location |
|---|---|---|---|---|
| PILOT-COMETBFT-2026-11-18-EV-01 | Executable prototype (1 CometBFT process + 1 C++ FST ABCI app over Unix-domain socket) | — | BLOCKED — Go 1.25.0 not installed (`go: command not found`); prerequisite, not an architectural blocker | Feasibility spike §10 (local doc) |
| PILOT-COMETBFT-2026-11-18-EV-02 | ≥4-validator multi-validator acceptance evidence | — | BLOCKED — same Go-toolchain prerequisite | Feasibility spike §10 (local doc) |
| PILOT-COMETBFT-2026-11-18-EV-03 | Repository-recorded CometBFT integration claims (3 tests: socket+Echo; single-node genesis+commit; 4-node genesis agreement; 343/343 C++ tests) | — | Recorded claim only — NOT re-verified in this session (no Go toolchain) | Root `README.md` (project status table, test results) |

---

## Proposed ADRs awaiting owner decision

| ADR ID | Title | Proposed by | Date | Status | Owner decision due |
|---|---|---|---|---|---|
| ADR-0006 (proposed number) | CometBFT v0.40.0 as committed consensus engine dependency | fst-research | 2026-11-18 | PROPOSED (outline in pilot report Section 16) | Owner decision required before CometBFT becomes a production dependency |

---

## Accepted and rejected recommendations

| Recommendation | Title | Outcome | Rationale | Date |
|---|---|---|---|---|
| PILOT-COMETBFT-2026-11-18-REC-01 | Adopt CometBFT v0.40.0 as FST consensus engine candidate (feasibility-gated) | Proposed (pending owner decision via ADR-0006) | Most mature, most completely evidenced BFT candidate locally; D-017/ADR-0004 preferred candidate; separate-process ABCI model preserves C++20 codebase; conditions: executable prototype, ≥4-validator evidence, verification gate, owner decision | 2026-11-18 |
| PILOT-COMETBFT-2026-11-18-REC-02 | Reject Nakamoto PoW/C3 for D-017 | Rejected | Probabilistic finality conflicts with D-017 deterministic-finality direction; high energy; slow settlement | 2026-11-18 |
| PILOT-COMETBFT-2026-11-18-REC-03 | Reject FeinStatic Raft/D-024 as production consensus | Rejected | Crash-fault only, not BFT; conflicts with P9/D-017; reference only | 2026-11-18 |
| PILOT-COMETBFT-2026-11-18-REC-04 | Exclude CometBFT `main` branch | Rejected | Unreleased (43 commits ahead); not a production dependency per CometBFT policy | 2026-11-18 |
| PILOT-COMETBFT-2026-11-18-REC-05 | Retain HotStuff/C1, Gasper/C5, Narwhal/Tusk/C6 as OPEN RESEARCH fallbacks | Retained (not selected) | Documented in ADR-0004 alternatives; OPEN RESEARCH; less local evidence than CometBFT | 2026-11-18 |

---

## Dependencies and blocked research

| Blocked item | Blocked by | Required action | Unblock condition |
|---|---|---|---|
| RQ-02 executable prototype + ≥4-validator acceptance evidence | Go 1.25.0 toolchain not installed (`go: command not found`) | Install Go 1.25.0; run feasibility spike prototype steps (genesis init, single-process ABCI exercise, persistence/restart, ≥4 validators, Byzantine fault injection, determinism, crash recovery, activation-height tests) | Go 1.25.0 available in environment; fst-verification gate passed |
| CSA-2026-001 inclusion in v0.40.0 tree | Not re-verified by direct source inspection (inference basis only) | Direct inspection of v0.40.0 source tree once Go toolchain available | Fix confirmed in v0.40.0 tree |

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
