# FST Decision Closure Roadmap

**Status:** Authoritative classification of every outstanding FST matter.
**Classification scheme:** APPROVED / ENGINEERING-DEFINED / TESTNET-ONLY / DEFERRED-BY-DESIGN / BLOCKED-ON-OWNER.
**Owner:** Integration Agent maintains this document; decisions are classified here and tracked to closure.
**Source of truth:** `docs/fst/scope/scope-v0.1.md`, `docs/fst/decisions/decision-register.md`, `docs/fst/engineering-charter.md`, `docs/fst/operations/project-plan.md`.

This roadmap classifies each outstanding matter and assigns a decision owner, dependencies, closure condition and target phase. It explicitly defers Phase 5–10 decisions until their dependencies and research gates are met. No matter here is forced into an immediate owner decision merely because it is listed.

---

## Classification definitions

| Classification | Meaning | Who decides | Can proceed to implementation? |
|---|---|---|---|
| APPROVED | Explicit owner decision (recorded in decision register / ADR) | Owner | Yes, per approved design |
| ENGINEERING-DEFINED | Delegated engineering decision within approved bounds | Responsible role (research-backed) | Yes, after research gate and specification |
| TESTNET-ONLY | Approved for testnet parameterisation, not production | Responsible role + owner sign-off for testnet | Testnet only |
| DEFERRED-BY-DESIGN | Deliberately postponed until a later phase/gate | Project plan (phase gates) | No — blocked until unblocked phase |
| BLOCKED-ON-OWNER | Requires explicit owner decision before any work | Owner | No — awaiting owner decision |

---

## Decision closure matrix

| # | Decision matter | Classification | Decision owner | Dependencies | Closure condition | Target phase |
|---|---|---|---|---|---|---|
| M-01 | D-015 numerical monetary parameters (issuance schedule, tail emission, base fee, priority reward bounds) and economic simulation | BLOCKED-ON-OWNER | Owner | D-015 APPROVED (ADR-0005); monetary-policy research (RS-015); simulation model | Owner selects numerical parameters; simulation results documented; ADR updated with values | Phase 4 (pre-mainnet) |
| M-02 | D-016 validator economics and lifecycle parameters (bonding, delegation, unbonding, slashing thresholds, rewards) | BLOCKED-ON-OWNER | Owner | D-016 APPROVED (ADR-0003); staking research | Owner selects lifecycle/economics parameters; ADR updated | Phase 4 |
| M-03 | D-017 CometBFT feasibility, security and real multi-validator acceptance | DEFERRED-BY-DESIGN | fst-research (+ fst-verification gate) | D-017 APPROVED (ADR-0004 preferred candidate); CometBFT feasibility spike; multi-validator test | CometBFT feasibility + security evidence accepted; deterministic finality verified on real multi-validator network | Phase 3–4 |
| M-04 | D-018 bounded offline spending and reconciliation | DEFERRED-BY-DESIGN | fst-research | D-018 PROVISIONAL; offline-operation research; double-spend/finality limitation analysis | Offline spending bounds and reconciliation rules specified; double-spend/finality limitations documented | Phase 5 |
| M-05 | D-019 multi-transport architecture (internet, mesh, radio, satellite, DTN) | DEFERRED-BY-DESIGN | fst-research | D-019 PROVISIONAL; transport-interface specification | Transport interface contract defined; first transport (internet) implemented and tested | Phase 3 (interface) / Phase 5 (additional transports) |
| M-06 | D-020 financial privacy mechanisms | DEFERRED-BY-DESIGN | fst-research | D-020 PROVISIONAL; privacy research; transaction/consensus assumption analysis | Privacy mechanism specified; proof it does not silently invalidate core transaction/consensus assumptions | Phase 5 |
| M-07 | D-021 migration and fork conversion mechanisms | DEFERRED-BY-DESIGN | fst-research | D-021 PROVISIONAL; fork-protection requirements (D-036 APPROVED); migration-window design | Migration/fork-conversion mechanism specified; fixed window and market-conversion rules defined | Phase 6 |
| M-08 | D-035 independent client implementation count | OPEN RESEARCH (no number selected) | fst-research | D-035; client-implementation research | Research complete; owner selects count | Phase 5+ (pre-mainnet) |
| M-09 | D-036 implementation mechanisms (unique network identity, replay protection, distinct identifiers) | ENGINEERING-DEFINED | fst-implementation (+ fst-research) | D-036 APPROVED (requirements); network-identity research | Network identity, replay protection, distinct currency identifiers implemented and tested | Phase 2–3 |
| M-10 | Cryptographic primitives and key lifecycle | DEFERRED-BY-DESIGN | fst-research | Cryptographic review; primitive research | Primitives selected; key lifecycle specified; security review passed | Phase 4 |
| M-11 | Genesis allocation and issuance parameters | BLOCKED-ON-OWNER | Owner | D-014 (FST only), D-015 issuance; genesis specification | Genesis allocation and issuance parameters specified and approved | Phase 4 (pre-mainnet) |
| M-12 | Transaction encoding, spending conditions and resource limits | ENGINEERING-DEFINED | fst-implementation | D-037 APPROVED (ADR-0002 UTXO); transaction-model research | UTXO transaction encoding, spending conditions, resource limits specified and implemented | Phase 3 |
| M-13 | Mempool replacement, persistence and fee policies | ENGINEERING-DEFINED | fst-implementation | D-037; transaction model; fee market research (D-015 base fee) | Mempool policy specified; fee policy consistent with burned base fee | Phase 3–4 |
| M-14 | State synchronization and light-client proofs | DEFERRED-BY-DESIGN | fst-research | Node-roles research; light-client proof research | Sync protocol and light-client proof specification | Phase 5 |
| M-15 | Open-source license and contribution policy | ENGINEERING-DEFINED | fst-integration (+ owner) | License review; contribution-process design | License selected; contribution policy published | Phase 2 |
| M-16 | Project governance, maintainership and succession | BLOCKED-ON-OWNER | Owner | Governance research; succession plan | Governance model documented; maintainers and succession named | Phase 5+ |
| M-17 | Supply-chain security and release signing | ENGINEERING-DEFINED | fst-integration | Build/release tooling; key management | Release signing and supply-chain process implemented and tested | Phase 4 |
| M-18 | Production security audits and mainnet readiness | BLOCKED-ON-OWNER | Owner | All M-01..M-17 closed or deferred; audit schedule | Independent security audits passed; mainnet-readiness criteria documented and met | Mainnet (post-Phase 5) |
| M-19 | FST root-project license selection (Apache-2.0, GPL-3.0, MPL-2.0, others) | BLOCKED-ON-OWNER | Owner | License comparison research (RT-LICENSE-001); contributor licensing requirements | Owner explicitly selects root-project license; LICENSE file added only with owner authorization | Phase 2–4 (pre-mainnet) |
| M-20 | Dependency policy automation (license scanning, SPDX SBOM, allowlist/denylist, provenance, vulnerability scanning) | DEFERRED-BY-DESIGN | fst-integration (+ fst-research research) | Policy documented (`docs/fst/policies/technology-and-dependency-acceptance.md`); CI controls researched | CI gates enforce policy; exceptions not hidden through scanner configuration | Phase 4+ |

---

## Phase gating

- **Phases 0–2:** COMPLETE. Foundation, specification, foundational research, core networking foundation. Root-project license selection (M-19) is BLOCKED-ON-OWNER and must be resolved before public contribution policy is finalized.
- **Phase 3:** Consensus & validator architecture, transaction & state layer, UTXO ledger, mempool, network identity (M-09, M-12, M-13, M-10 partially). D-017 CometBFT feasibility (M-03) must reach prototype + determinism evidence before CometBFT becomes a production dependency.
- **Phase 4:** Monetary parameters (M-01), validator economics (M-02), genesis (M-11), cryptographic primitives (M-10), supply-chain/release signing (M-17), testnet deployment.
- **Phase 5–10:** Offline operation (M-04), additional transports (M-05), privacy (M-06), migration/fork conversion (M-07), independent client count (M-08), state sync/light clients (M-14), governance/succession (M-16), mainnet readiness (M-18).

Phase 5–10 matters are DEFERRED-BY-DESIGN until their dependencies and research gates are met. They are not subject to immediate owner decision.

---

## Reconciliation of historical decision classifications

The decision register (`docs/fst/decisions/decision-register.md`) is the authoritative classification record and is already consistent with the approved direction:
- APPROVED: D-001..D-010, D-014, D-015, D-016, D-017, D-036, D-037.
- PROVISIONAL: D-018, D-019, D-020, D-021, D-022..D-034.
- OPEN RESEARCH: D-035.

Research documents that describe an APPROVED decision as still undecided are preserved for their research and history, but their status is superseded by the decision register and this roadmap. Current decision status is unambiguous: consult the decision register first, then this roadmap for closure conditions. No research result reclassifies an APPROVED decision.

## Decision owner notes

- The Research Agent (fst-research) drives research for M-03, M-04, M-05, M-06, M-07, M-08, M-10, M-14 and proposes ADRs where an owner decision is required. It does NOT approve its own recommendations.
- The Implementation Agent (fst-implementation) makes ENGINEERING-DEFINED decisions (M-09, M-12, M-13) within approved bounds and after research gate.
- The Integration Agent (fst-integration) owns M-15, M-17 process and cross-role coordination.
- The Verification Agent (fst-verification) independently gates all implementation work; it does not approve its own work.
- BLOCKED-ON-OWNER matters (M-01, M-02, M-11, M-16, M-18, M-19) require explicit owner decision; no agent proceeds on implementation that depends on them without owner sign-off.
- M-20 (dependency policy automation) is DEFERRED-BY-DESIGN pending policy documentation and owner sign-off; CI controls are not implemented until the policy is accepted.

---

## Change control

Changes to this roadmap (reclassification, new matters, closure) require:
1. Research evidence (for research-driven items).
2. Agreement of the responsible decision owner.
3. Owner approval for any BLOCKED-ON-OWNER item or any change to an APPROVED decision.
4. Update recorded here with date, owner, and rationale. No silent reclassification.
