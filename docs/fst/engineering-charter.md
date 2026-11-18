# FST Engineering Charter

**Status:** Authoritative technical and community-engineering doctrine for the FST project.
**Scope:** Applies to all five permanent FST engineering roles (fst-research, fst-implementation, fst-verification, fst-debugging, fst-integration) and to every contribution to the FST codebase.
**Classification:** APPROVED — owner decision. Do not silently change.

This charter is the common authoritative document referenced by all five role-owned operational ledgers. Each ledger links to this charter rather than duplicating it. It is reviewed and updated only through the same research-first, owner-approved change process it describes.

---

## 1. FST identity and global mission

FST is an independent, globally accessible, decentralised financial network. It is not a bank plugin, a centrally controlled financial application, or a system controlled by an existing financial institution. FST's native asset is FST (D-014). The network is designed to survive the disappearance of its founding developers (D-010) and to be maintainable by independent teams (D-005, P4). No single organisation possesses unilateral control over FST (D-002, P1), no organisation has permanent privileged protocol authority (D-003, P2), and there is no central emergency administrator or unilateral master key (D-008, P8).

## 2. Open-development philosophy

FST development is open and transparent (D-004, P3). Protocol development and important discussions are conducted in the open. Participants can reject proposed upgrades (D-006, P5). Forks may be legitimate; neither fork has exclusive legitimacy (D-006, P6). Development philosophy draws inspiration from the Linux and open-source communities — research before implementation, modular architecture, independent maintainability, reproducible conformance — while preserving FST's own governance principles that prohibit unilateral permanent protocol authority.

## 3. Modularity and subsystem boundaries

FST is built as a set of modules with well-defined interfaces:
- Consensus is separated from transaction execution and financial economics.
- The UTXO ledger is the canonical source of financial ownership.
- Networking transports integrate through controlled interfaces.
- Wallet and account abstractions do not replace the canonical UTXO ledger.

Each module owns its state and exposes an explicit contract. Modules communicate only across declared interfaces, never through shared internal state.

## 4. Stable interfaces and explicit contracts

Interfaces are versioned, documented, and tested. Contract changes require research, an ADR, and owner approval where classified. Backward compatibility is preserved within a major protocol version; breaking changes are announced, versioned, and opt-in for participants (D-006). Extension points are defined early but speculative features are not implemented prematurely ("Build for extension, not premature complexity").

## 5. Canonical protocol specifications and conformance vectors

The canonical protocol is specified in `docs/fst/spec/`. Every implemented behaviour has a corresponding specification section and conformance vector. Independent implementations must be capable of producing identical deterministic results from identical inputs. Conformance vectors are versioned alongside the protocol.

## 6. Independent implementations and interoperability

FST targets multiple independent client implementations (D-035, research phase). Interoperability and reproducible conformance are requirements, not afterthoughts. Protocol versions and compatibility rules are documented so independent teams can build compatible nodes without relying on the founding team (P4, P10).

## 7. Research-first technology selection

No significant technology or architecture selection is made without research (D-011, ADR process). Before adopting a significant technology, the Research Agent evaluates:
- Security properties and known vulnerabilities
- Correctness and formal properties
- Peer-reviewed research and specifications
- Production maturity and real deployment evidence
- Maintenance activity and security-response quality
- Performance on FST-specific workloads
- C++20 compatibility and integration complexity
- Dependency footprint and operational requirements
- Licensing and patent considerations
- Portability and independent implementation feasibility
- Supply-chain risks
- Long-term upgrade, compatibility and replacement costs

Evaluation records publication dates, release versions, access dates and source links. No research result automatically becomes an approved protocol decision. Research is a mandatory gate for significant technology or architecture selections. The full acceptance criteria are in `docs/fst/policies/technology-and-dependency-acceptance.md` (license policy, transitive-dependency compliance, supply-chain controls, root-project license research BLOCKED-ON-OWNER).

## 8. Cryptographic and security engineering

Security emerges from protocol design and independent verification (D-009, P9). Cryptographic primitives, key lifecycle, and privacy mechanisms are selected through research and reviewed by independent security auditors before production use. Secrets, private keys and credentials are never logged or committed. Strong cryptographic privacy (D-020) is PROVISIONAL; specific mechanisms require owner decision.

## 9. Reproducible builds and software supply-chain security

Builds are reproducible. Dependencies are pinned, audited, and their lifecycle managed. Release signing and supply-chain security are required before mainnet. Vulnerability response follows a documented process. No prototype or benchmark is treated as proof of production security.

## 10. Dependency lifecycle and vulnerability response

Every dependency is recorded with version, license, upstream maintenance status, and known vulnerabilities. Vulnerabilities are assessed, patched, and released on a documented schedule. Dependency upgrades require the same research and gate process as new technology adoption.

## 11. Performance and capacity engineering

Scalability is demonstrated through reproducible evidence (not claims). Performance targets are defined per workload and validated with benchmarks and multi-node tests. Capacity planning covers node classes (full, validator, light) and storage/synchronization requirements.

## 12. Portability, global usability and low-bandwidth considerations

FST is portable across toolchains and architectures. Global usability and low-bandwidth operation are design constraints. Networking transports support internet, mesh, radio, satellite and delay-tolerant links incrementally (D-019, PROVISIONAL).

## 13. Backward compatibility and versioning

Protocol versions and compatibility rules are documented. Backward-conscious protocol evolution (principle 8) governs changes. Participants can reject upgrades (D-006). Testnet parameters are never production parameters.

## 14. Experimental versus stable development

Experimental work (PROVISIONAL / OPEN RESEARCH) lives in research branches and prototypes, never in the production core. Stable development requires an APPROVED ADR, research evidence, and verification-gate passage. Deferred features (Phase 5–10) are not implemented until their dependencies and research gates are met.

## 15. Community contributions and independent maintenance

Contributions are reviewed against this charter and the approved scope. Independent teams must be able to maintain the software without the founding team (P4). Maintainership and succession are documented governance matters (decision-closure-roadmap).

## 16. Governance boundaries

No agent may silently change an owner-approved decision. Owner decisions (APPROVED / BLOCKED-ON-OWNER) require owner agreement to change. Delegated engineering decisions (ENGINEERING-DEFINED / TESTNET-ONLY) are made by the responsible role within its charter. Governance must not introduce unilateral authority (D-003, P2, P8).

## 17. External security review and production-readiness requirements

Production readiness requires external security audits, reproducible builds, verified determinism, and documented mainnet-readiness criteria. Mainnet parameters (D-015 numerical monetary parameters, D-016 validator economics) are selected through research, modeling, and owner decision — never estimated.

## 18. Release engineering and long-term sustainability

Releases are small, tested and documented (D-013), issued after approximately two or three new features. Long-term sustainability beyond the founding development team is a design goal (P10). Release engineering includes build, test, sign and publish automation governed by the Integration Agent.

## 19. Technology and dependency acceptance policy

FST adopts technology only after four independent assessments pass: legal/licensing compatibility, security/provenance/intellectual-property assessment, engineering suitability/operational maturity, and compatibility with FST's approved architecture, decentralization and future phases. A preferred permissive-license policy applies (Apache-2.0, MIT, BSD-2-Clause, BSD-3-Clause, ISC, Zlib, BSL-1.0); reciprocal and GPL-family licenses require documented compatibility assessment and explicit owner approval; proprietary, non-commercial, no-derivatives, field-of-use, SSPL, Commons Clause, and unclear/custom licenses are rejected by default. Transitive dependencies must be fully inventoried (SPDX SBOM), and no dependency whose transitive license obligations cannot be established may be introduced. Cryptographic primitives must not be reinvented; widely reviewed, standards-based, actively maintained implementations are preferred. The full policy is in `docs/fst/policies/technology-and-dependency-acceptance.md`. Root-project license selection remains BLOCKED-ON-OWNER; no agent may silently choose FST's root license.

---

**"Build for extension, not premature complexity."** Future phases integrate through explicit, documented and tested interfaces. Do not build speculative features prematurely or create unnecessary abstraction layers.

---

## Relationship to approved decisions

This charter does not amend D-001 through D-037 or ADR-0002 through ADR-0005. Where this charter and an approved decision conflict, the approved decision governs. The decision-closure roadmap (`docs/fst/decisions/decision-closure-roadmap.md`) classifies each outstanding matter and its closure conditions.

## Roles

- `fst-research` — discovers and evaluates the best technical foundations; owns `docs/fst/operations/research-status.md`.
- `fst-implementation` — implements owner-approved, research-backed engineering requirements; owns `docs/fst/operations/implementation-status.md`.
- `fst-verification` — independently verifies exact commits, functionality, security and compatibility; owns `docs/fst/operations/verification-status.md`.
- `fst-debugging` — reproduces, diagnoses and fixes authorized defects; owns `docs/fst/operations/defect-register.md`.
- `fst-integration` — centrally coordinates work, resolves ownership, manages handoffs and integrates accepted changes; owns `docs/fst/operations/integration-status.md`.

Integration is the coordinator, not the owner of the other agents' technical decisions. Verification is independent and does not approve its own implementation. Research is a mandatory gate and does not approve its own recommendations.
