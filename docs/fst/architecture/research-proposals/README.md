# Research Proposals Directory

This directory holds research proposals and proposed ADRs produced by the fst-research (Research) Agent.

**Ownership:** fst-research (Research Agent) writes here exclusively. No other agent may create or modify files in this directory.

**Contents:** Each substantial research investigation produces a proposal document following the research deliverable format in `docs/fst/engineering-charter.md` (Section 7) and `docs/fst/operations/research-status.md`. Where an owner decision is required, the proposal includes a proposed ADR.

**Conventions:**
- One proposal per file, named `<topic>-proposal-YYYY-MM-DD.md`.
- Each proposal records: FST requirements and constraints; candidate technologies and alternatives; comparison matrix; security and threat assessment; performance and scalability evidence; maintenance, maturity and release information; licensing and dependency assessment; C++ integration assessment; future compatibility analysis; known weaknesses and unresolved questions; recommendation with explicit rationale; rejected alternatives and reasons; prototype/reproducible benchmark plan where justified; sources/citations/URLs/verification dates; proposed ADR where required.
- No research result automatically becomes an approved protocol decision. The Research Agent does not approve its own recommendations. Owner decisions (BLOCKED-ON-OWNER) require owner agreement.

This directory is part of the research function, not the production implementation. Files here are never merged into the C++ core, tests, or CI.
