# FST Technology and Dependency Acceptance Policy

**Status:** Permanent project policy.
**Classification:** APPROVED — owner decision. Do not silently change.
**Owner:** fst-research (research and recommendations); fst-integration (enforcement and integration gates); owner (license and BLOCKED-ON-OWNER decisions).
**Enforced by:** `docs/fst/engineering-charter.md`, all five role-owned ledgers, all five agent definitions, and the `fst-team` integration controller.

This policy extends the FST Engineering Charter (`docs/fst/engineering-charter.md`) and the Decision Closure Roadmap (`docs/fst/decisions/decision-closure-roadmap.md`). It establishes FST's technology adoption, licensing, and global distribution doctrine. It does not alter accepted protocol decisions (D-001–D-037) or accepted ADRs (ADR-0002–ADR-0006-proposed). It does not select FST's root-project license; that remains BLOCKED-ON-OWNER (see Section 7).

The objective is a globally reusable, maintainable and interoperable software ecosystem without knowingly introducing proprietary restrictions, unclear licensing obligations, avoidable patent risks, or incompatible dependencies.

---

## 1. Technology acceptance principle

FST must not adopt technology merely because it is popular, recent, available on GitHub, or recommended by an AI agent.

Every significant technology requires four independent assessments, and every gate must pass independently:

1. **Legal and licensing compatibility** — see Section 2.
2. **Security, provenance and intellectual-property assessment** — see Sections 4 and 5.
3. **Engineering suitability and operational maturity** — see Sections 4 and 5.
4. **Compatibility with FST's approved architecture, decentralization and future phases** — see `docs/fst/engineering-charter.md` and `docs/fst/decisions/decision-closure-roadmap.md`.

No agent may claim that technology is absolutely free from all legal risk. Uncertainty must be explicitly recorded and escalated to the Integration Agent and, where classified BLOCKED-ON-OWNER, to the project owner.

## 2. Dependency license policy

FST establishes a preferred permissive-license policy.

### Preferred licenses (subject to normal review)

- Apache-2.0
- MIT
- BSD-2-Clause
- BSD-3-Clause
- ISC
- Zlib
- BSL-1.0

### Special-review licenses

- GPL-2.0 and GPL-3.0 variants
- LGPL variants
- AGPL variants
- MPL-2.0
- EPL variants
- CDDL
- Other reciprocal or potentially incompatible licensing models

These are not automatically unlawful or unusable. No such dependency may be incorporated into production without a documented compatibility assessment and explicit owner approval.

### Rejected by default

- Proprietary restrictions.
- Non-commercial terms.
- No-derivatives terms.
- Field-of-use restrictions.
- Source-available licenses that do not meet the project's approved open-source policy.
- SSPL and Commons Clause.
- Unclear or ambiguous custom licensing.
- Dependencies without verifiable licensing or provenance.

An exception requires a precise explanation, a distribution-impact analysis, and explicit owner approval.

OSI approval alone is not proof of compatibility with FST.

## 3. Transitive dependency compliance

Inspect complete dependency trees, not just direct dependencies. For every proposed dependency, record:

- Project and upstream repository.
- Exact release and immutable source reference (tag, commit SHA, digest).
- SPDX license identifier.
- Copyright and attribution obligations.
- Required NOTICE and third-party license files.
- Transitive dependencies and their licenses.
- Patent grants and known patent risks.
- Known security advisories.
- Provenance and release authenticity (signed releases, checksums).
- Current maintenance state (release cadence, issue/PR response, contributor count).
- Supported platforms and toolchains.
- Upgrade and replacement implications.

Generate and maintain a machine-readable software bill of materials (SBOM). Use SPDX identifiers and a reproducible dependency inventory. Do not introduce dependencies whose transitive license obligations cannot be established.

## 4. Algorithm and standard adoption

An algorithm or standard must be evaluated separately from the license of a particular software implementation. Before adoption, investigate:

- Published specification and authoritative standard.
- Patent disclosures and licensing conditions.
- Known intellectual-property disputes.
- Security assumptions and cryptographic properties.
- Cryptanalysis and known attacks.
- Production deployments and implementation maturity.
- Reference implementations and interoperability vectors.
- Implementation licenses.
- Export-control or jurisdiction-specific restrictions where relevant.

Do not independently assume that an algorithm is patent-free merely because its source code is open.

Do not reinvent cryptographic primitives. Prefer widely reviewed, standards-based, actively maintained cryptographic implementations.

Do not copy code from repositories, papers, documentation, or AI-generated output without establishing its origin and usage rights.

## 5. Security and software supply chain

Require appropriate controls for production dependencies:

- Pinned versions and immutable source references.
- Authenticated release verification where available (signatures, digests).
- Cryptographic checksums.
- Reproducible or independently verifiable builds where feasible.
- Vulnerability monitoring.
- Dependency update review.
- Signed release processes.
- SBOM generation.
- License and provenance verification.
- Security disclosure procedures.

Evaluate upstream maintainer responsiveness and project sustainability. A permissively licensed dependency may still be rejected for security, abandonment, or architectural reasons.

## 6. GitHub and public contribution policy

Treat GitHub as the project's public engineering and collaboration platform. Establish:

- Root-project license decision (owner decision; see Section 7).
- Contributor licensing requirements.
- Contribution guidelines.
- Code of conduct.
- Security policy.
- Third-party attribution.
- Dependency inventory.
- Reproducible release procedures.
- Review and approval requirements.

Do not assume that public repository visibility automatically grants downstream reuse rights.

The root-project license is an explicit owner decision. Do not create or replace the root LICENSE file without owner authorization.

## 7. Root-project license research (BLOCKED-ON-OWNER)

Prepare a dedicated comparative research report covering:

- Apache-2.0.
- GPL-3.0.
- MPL-2.0.
- Other justified candidates.

Evaluate:

- Global software reuse.
- Commercial deployment.
- Derivative works.
- Patent provisions.
- Distribution obligations.
- Compatibility with likely dependencies.
- Independent implementations.
- Community contributions.
- Compatibility with FST's Linux-inspired open-development philosophy.
- Long-term ecosystem sustainability.

Keep this decision BLOCKED-ON-OWNER until the owner explicitly selects the project license. No agent may silently choose FST's root license.

Research task: RT-LICENSE-001 (see `docs/fst/operations/research-status.md`).
Dependency automation task: RT-DEP-002 (see `docs/fst/operations/research-status.md`).

## 8. Research agent mandate (fst-research)

Every significant technology recommendation produced by fst-research must include:

1. Required FST capabilities.
2. Candidate alternatives.
3. Technical comparison.
4. Security and intellectual-property assessment.
5. License and dependency-tree assessment.
6. Patent analysis.
7. Performance and scalability evidence.
8. Maturity and maintenance evidence.
9. C++20 integration implications.
10. Deployment implications.
11. Future-phase compatibility.
12. Reproducible prototype plan where appropriate.
13. Recommendation and rejected alternatives.
14. Source links, versions, publication dates and research verification date.

The Research Agent must not independently approve its own proposals.

## 9. Implementation and integration gates

Implementation must not introduce an unreviewed external dependency or materially new algorithm/framework. Significant new technology requires a linked approved research recommendation and, where applicable, an owner-approved ADR.

The Integration Agent must reject handoffs containing:

- Missing source provenance.
- Unknown licensing.
- Unreviewed reciprocal licensing.
- Unresolved patent concerns.
- Missing transitive dependency inventory.
- Unpinned production dependencies.
- Incompatible architecture decisions.

Verification must include dependency and license checks where applicable.

Debugging must not introduce an unapproved dependency as an incidental fix.

## 10. Project documentation references

- `docs/fst/engineering-charter.md` — references this policy (Sections 7 and 10).
- `docs/fst/decisions/decision-closure-roadmap.md` — license selection and dependency-policy automation tracked as BLOCKED-ON-OWNER / ENGINEERING-DEFINED matters.
- `docs/fst/operations/research-status.md` — tracks RT-LICENSE-001 and RT-DEP-002.
- All five agent definitions — reference this policy and the research-agent mandate.

## 11. Automated enforcement (deferred; not yet implemented)

Investigate appropriate GitHub CI controls and implement only after this policy is documented. Desired controls:

- Dependency license scanning.
- SPDX SBOM generation.
- License allowlist and denylist enforcement.
- Dependency provenance checks.
- Vulnerability scanning.
- Reproducible dependency resolution.
- Automated policy-failure reporting.

An exception must not be hidden through scanner configuration. Status: DEFERRED-BY-DESIGN pending research task RT-DEP-002 and owner sign-off.

## 12. CometBFT review (revisit of PILOT-COMETBFT-2026-11-18)

Revisit the existing CometBFT v0.40.0 candidate report (`docs/fst/research/pilot-cometbft-consensus-candidate-2026-11-18.md`) and verify:

- Exact release tag and immutable commit.
- Release authenticity.
- Exact tagged Go toolchain requirement.
- Complete Go module dependency license inventory.
- Patent and licensing conditions.
- ABCI integration requirements.
- Security and compatibility evidence.

CometBFT's repository license (Apache-2.0) permits preliminary consideration but does not automatically approve the complete dependency tree or production adoption. Preserve ADR-0006 as a proposed decision until all necessary gates and owner authorization are obtained.

## 13. Strict change management

- Do not alter accepted protocol decisions.
- Do not silently choose FST's root license.
- Do not push to `main`.
- Do not force-push or rewrite history.
- Preserve existing worktrees and all agent ledger ownership.
- Stage exact reviewed files only.
- Keep `AGENTS.md` local-only.
- Do not touch protected session files (`session-ses_*.md`).
- Record exact SHAs and handoffs.
- Do not begin production feature work as part of this policy update.

## 14. Completion criteria

Report:

- Files created and updated.
- Exact license classifications.
- Root-project licensing decision status.
- Research tasks created.
- Dependency enforcement status.
- Agent instructions updated.
- CI controls implemented or deferred with reasons.
- Any unresolved legal or licensing questions.
- Exact commit SHA.

The final result must make it impossible for the agents to mistake a publicly available repository, an attractive algorithm, or an apparently permissive library for automatically approved production technology.
