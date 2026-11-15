# FST Development Roadmap

**Status:** INITIAL — records the intended release cadence and milestone themes.
**Classification:** PROVISIONAL — release timing and feature order require owner
validation.

## Release philosophy

Development proceeds through small, thoroughly tested and documented releases,
generally after approximately two or three new features (principle P13/P14). Each
release must be accompanied by:

- Updated documentation (scope, principles, architecture, ADRs, decisions).
- Research entries for any new design decisions.
- Tests with actual results (not fabricated).
- Independent review where security-critical.

## Milestone 1 — Project foundation and research documentation (COMPLETE)

**Status:** COMPLETE (this documentation set).
**Objective:** Establish the FST project's documented foundation and agent-development
infrastructure.

Deliverables:
- `docs/fst/` modular documentation hierarchy.
- `AGENTS.md` at repository root for AI agent and contributor instructions.
- Decision register with APPROVED / PROVISIONAL / OPEN RESEARCH classifications.
- Research register and methodology.
- FeinStatic technical baseline and transformation findings.

No blockchain functionality was implemented in this milestone. No existing FeinStatic
source files were modified.

## Milestone 2 — Specification and research (PROVISIONAL)

**Objective:** Produce the FST protocol specification and complete foundational research.

Planned deliverables (classification: PROVISIONAL):
- Versioned protocol specification (message types, state transitions, fork rules,
  upgrade signaling, versioning).
- Research entries for consensus, identity, discovery, privacy, monetary policy,
  offline operation, and bridge domains.
- Threat model and security assumptions document.
- Node architecture definition (light/full/archival roles).

Open research questions to resolve:
- Consensus algorithm and validator economics.
- Transaction model (account vs UTXO, fee market, confidentiality).
- Permissionless identity registration mechanism.
- Network discovery and multi-transport stack.

## Milestone 3 — Minimal permissionless test network (PROVISIONAL)

**Objective:** Deploy a minimal test network where permissionless validators reach
Byzantine fault-tolerant finality on a test chain.

Planned deliverables (classification: PROVISIONAL):
- Reference implementation of the selected consensus algorithm.
- P2P networking with peer discovery.
- Permissionless validator entry and exit.
- Test network with multiple independent validators.
- Test results and benchmark evidence.

No privileged operator, no master key, and no founding-organisation control.

## Future milestones (PROVISIONAL)

The following themes are planned but not scheduled or approved in detail:

- Financial accounts, payments, and transfers on the core network.
- Settlement layer separate from consensus.
- Privacy layer (transaction confidentiality, selective disclosure).
- Offline operation with bounded spending authority.
- FST exchange with national currencies through independent infrastructure.
- Merchant, institutional, and wholesale transaction support.
- Additional financial services introduced incrementally.

## Cross-references

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope.
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — Approved principles.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision register.
- [`../research/research-register.md`](../research/research-register.md) — Research register.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — FeinStatic baseline.
