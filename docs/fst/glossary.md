# FST Glossary

**Status:** INITIAL — records core terminology used across FST documentation.
**Principle:** Terminology must be consistent across all documents. When a term's
meaning changes, update this glossary and cross-reference all affected documents.

## Terms

| Term | Definition | Notes |
|------|-----------|-------|
| FST | The native currency, operating token, and settlement asset of the FST network. | Initial on-network asset scope is FST only. |
| FeinStatic | The existing C++20 financial-infrastructure prototype at commit `a8eac41` (release v2.1.0). | Preserved as historical reference; not modified by FST documentation. |
| APPROVED | A decision classification indicating an explicit owner decision. | Must not be silently changed. |
| PROVISIONAL | A decision classification indicating a preferred direction requiring validation. | Not yet production-ready. |
| OPEN RESEARCH | A decision classification indicating an unresolved decision requiring investigation. | Not ready for implementation. |
| Permissionless validator | A validator that can join the network without pre-provisioning or approval. | Open research; not yet implemented. |
| Byzantine fault tolerance | Consensus safety under the assumption that some participants may act maliciously. | Research direction; FeinStatic uses crash-fault Raft only. |
| Crash-fault tolerance | Consensus safety under the assumption that participants may crash but not act maliciously. | FeinStatic's current model. |
| Raft | The crash-fault consensus algorithm used in FeinStatic v2.1.0. | Preserved as reference; not suitable for permissionless Byzantine security. |
| ADR | Architecture Decision Record — documents a significant architectural decision. | Stored in `docs/fst/adr/`. |
| Research entry | A documented review of academic research, standards, or prior art relevant to FST. | Stored in `docs/fst/research/`. |
| Decision register | The register of all APPROVED / PROVISIONAL / OPEN RESEARCH decisions. | Stored in `docs/fst/decisions/decision-register.md`. |
| Fork | A divergence creating two independent networks and currencies. | Neither fork has exclusive legitimacy (principle P7). |
| Migration | Voluntary movement of a balance from FST-A to FST-B after a fork. | Provisional design; not implemented. |
| Offline operation | Local financial operation during network disconnection. | Bounded spending authority; reconciliation on reconnect. |
| Multi-transport communication | Communication over internet, mesh, radio, satellite, and delay-tolerant links. | Modular, incremental introduction. |
| Privacy | Strong cryptographic privacy for transactions, identity, and selective disclosure. | Approved direction; specific mechanisms open research. |
| Finality | The point at which a transaction or block is considered irreversible. | Model open research; FeinStatic has no BFT finality. |
| Sybil resistance | Mechanism preventing a single entity from creating many identities. | Open research for permissionless validator entry. |

## Cross-references

- [`scope/scope-v0.1.md`](scope/scope-v0.1.md) — FST project scope.
- [`principles/engineering-principles.md`](principles/engineering-principles.md) — Approved principles.
- [`decisions/decision-register.md`](decisions/decision-register.md) — Decision register.
- [`architecture/feinstat-baseline.md`](architecture/feinstat-baseline.md) — FeinStatic baseline.
