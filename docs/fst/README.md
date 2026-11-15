# FST Project Documentation

This directory contains the research-first documentation for **FST**, an independent,
globally decentralised financial network being developed from the existing FeinStatic
codebase (release v2.1.0, commit `a8eac41`).

## Purpose

FST is not a bank plugin or a centrally controlled financial application. It is an
independent financial network with FST as its native currency, operating token, and
settlement asset. The network is intended to support permissionless validator
participation, community-driven governance, offline operation, and modular
communication over internet, mesh, radio, satellite, and delay-tolerant links.

## Documentation hierarchy

```
docs/fst/
├── README.md                      # This file: navigation and overview
├── scope/
│   └── scope-v0.1.md              # Versioned project scope (owner decisions)
├── principles/
│   └── engineering-principles.md  # Approved engineering principles
├── architecture/
│   ├── feinstat-baseline.md       # Existing FeinStatic baseline and transformation findings
│   ├── node-identity-peer-discovery-implementation.md  # Node identity, signed records, peer discovery (M2C)
│   ├── secure-sessions.md         # TLS 1.3 secure session design (M2D)
│   ├── message-dispatch.md        # Message dispatch protocol and frame format (M2D)
│   ├── peer-admission.md          # Dynamic peer admission (M2D)
│   └── node-roles.md              # Node role architecture (M2E)
├── research/
│   ├── README.md                  # Research-first methodology overview
│   ├── research-register.md       # Register of research entries
│   └── research-entry-template.md # Template for new research entries
├── adr/
│   ├── README.md                  # Architecture decision record overview
│   └── adr-template.md            # Template for new ADRs
├── decisions/
│   └── decision-register.md       # Register of APPROVED / PROVISIONAL / OPEN RESEARCH decisions
├── security/
│   ├── security-research-register.md  # Security research register
│   ├── threat-model-v0.1.md          # Network threat model (M2E)
│   └── adversary-model-v0.1.md       # FST adversary model (Milestone 2H; OPEN RESEARCH)
├── spec/
│   └── core-protocol-specification-v0.1.md  # Versioned core protocol specification (M2E)
├── roadmap/
│   └── development-roadmap.md     # Development roadmap
├── architecture/
│   ├── permissionless-test-network-evaluation.md  # Deep consensus–Sybil evaluation (Milestone 2H; OPEN RESEARCH)
│   └── permissionless-test-network-decision-brief.md  # Owner decision briefs (Milestone 2H; no decision approved)
└── glossary.md                    # Glossary of terms
```

## Decision classifications

Every significant decision in this documentation is classified as one of:

- **APPROVED** — Explicit owner decision. Must not be silently changed.
- **PROVISIONAL** — Preferred direction requiring technical validation.
- **OPEN RESEARCH** — Unresolved decision requiring investigation and discussion.

Never convert a discussion or suggestion into an approved design without owner agreement.

## Relationship to FeinStatic

The existing FeinStatic implementation (commit `a8eac41`, release v2.1.0) is preserved
as historical engineering knowledge. Its source code, tests, release notes, and Git
history are **not** modified by this documentation. The FST documentation analyses
FeinStatic's architecture to determine what to preserve, redesign, or replace, but does
not implement blockchain functionality or change the existing application architecture.

## Agent instructions

See [`AGENTS.md`](../AGENTS.md) at the repository root for instructions that apply to
AI agents and future contributors working in this repository.
