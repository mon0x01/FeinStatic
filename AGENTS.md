# AGENTS.md — FST Project Instructions

This file instructs AI agents and future contributors working in the FST repository.
Read this file before modifying any code or documentation.

## Project identity

FST is an independent, globally decentralised financial network being developed from
the existing FeinStatic codebase (commit `a8eac41`, release v2.1.0). FST is **not** a
bank plugin, a centrally controlled financial application, or a system controlled by
an existing financial institution.

## Foundational documents (read before modifying code)

Before making any change, read the relevant documents under `docs/fst/`:

1. [`docs/fst/scope/scope-v0.1.md`](docs/fst/scope/scope-v0.1.md) — Project scope and
   approved boundaries.
2. [`docs/fst/principles/engineering-principles.md`](docs/fst/principles/engineering-principles.md) —
   Approved engineering principles (P1–P14).
3. [`docs/fst/decisions/decision-register.md`](docs/fst/decisions/decision-register.md) —
   Decision classifications (APPROVED / PROVISIONAL / OPEN RESEARCH).
4. [`docs/fst/architecture/feinstat-baseline.md`](docs/fst/architecture/feinstat-baseline.md) —
   Existing FeinStatic baseline and transformation findings.
5. [`docs/fst/research/README.md`](docs/fst/research/README.md) — Research-first
   methodology.

## Approved principles (P1–P14)

Respect these principles in every change. Never silently violate them:

- P1: No single organisation with unilateral control over FST.
- P2: No permanent privileged protocol authority.
- P3: Transparent development and discussions.
- P4: Independent teams must be able to maintain the software.
- P5: Participants can reject proposed upgrades.
- P6: Forks may be legitimate; neither has exclusive legitimacy.
- P8: No central emergency administrator or unilateral master key.
- P9: Security from protocol design and independent verification.
- P10: Network survives disappearance of founding developers.
- P12: Research-first engineering for every module.

## Decision classifications

Every significant decision is classified:

- **APPROVED** — Owner decision. Do not change without owner agreement.
- **PROVISIONAL** — Preferred direction requiring validation. Do not implement as
  production design.
- **OPEN RESEARCH** — Unresolved. Research only; do not implement.

See `docs/fst/decisions/decision-register.md` for the full register.

## Required workflow for every change

1. **Read the FST scope** before modifying code.
2. **Read relevant architecture and research documents** for the area being changed.
3. **Respect APPROVED / PROVISIONAL / OPEN RESEARCH classifications.** Never silently
   change a foundational decision.
4. **Investigate existing research before designing features.** Cite sources with
   exact titles, authors, dates, and DOI/URL where available.
5. **Document research references and engineering rationale** alongside implementation.
6. **Maintain module-specific instructions** where appropriate (under `docs/fst/`).
7. **Run relevant tests and report actual results.** Never fabricate test results,
   security guarantees, or performance claims.
8. **Update documentation alongside implementation.** Every implementation change
   must be accompanied by documentation updates.
9. **Avoid exposing secrets or private keys.** Do not log or commit credentials.
10. **Avoid destructive Git operations** without explicit approval. Do not force-push,
    rewrite history, or use `git add .` / `git add -A`. Stage only intended files.
11. **Keep changes incremental and independently reviewable.** Small, tested,
    documented releases after approximately two or three new features.
12. **Preserve historical FeinStatic implementation and release provenance.** Do not
    delete, replace, or modify existing FeinStatic source, tests, release notes, or
    Git history.

## Prohibited actions

- Do not implement consensus, blockchain networking, wallets, monetary policy, or
  other application features without an approved ADR and design review.
- Do not convert PROVISIONAL or OPEN RESEARCH decisions into APPROVED designs
  without owner agreement.
- Do not fabricate sources, test results, security guarantees, or performance claims.
- Do not expose secrets, private keys, API keys, or credentials in code or logs.
- Do not use `git add .`, `git add -A`, force-push, or history rewriting.
- Do not modify existing FeinStatic source files during the documentation-only
  Milestone 1 phase.

## Preservation rules

- The untracked file `session-ses_e2b1.md` must not be read, modified, deleted, or
  staged.
- Existing C++ source, headers, CMake configuration, tests, runtime databases, and
  historical release notes must not be modified during Milestone 1.
- The current branch is `main`; do not change branches or rewrite history without
  explicit approval.

## Relationship to FeinStatic

FeinStatic is a permissioned, crash-fault-tolerant prototype. Several of its
architectural assumptions conflict with FST principles:

- Single Bridge vault wallet (centralised custody — conflicts with P1, P2, P8).
- Operator roles and trust store (permanent privileged authority — conflicts with P2).
- Pre-provisioned peer rosters (no permissionless entry — conflicts with P4, P5).
- Raft consensus (crash-fault only — conflicts with P9).
- No upgrade/governance mechanism (conflicts with P5).

These are documented in `docs/fst/architecture/feinstat-baseline.md` and must be
reconsidered in the FST redesign, not fixed in place.

## Escalation

When uncertain about a decision classification, whether a change is in scope, or how
to apply research-first methodology, stop and request owner review. Do not guess.
