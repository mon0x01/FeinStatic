# FST Security Research Register

**Status:** INITIAL — no security research entries recorded yet.
**Classification:** All entries are OPEN RESEARCH until validated.

This register tracks security-relevant research for FST, including threat models,
adversary assumptions, cryptographic review status, and known limitations of the
existing FeinStatic implementation.

## Security research domains

| Domain | Status | Key questions |
|--------|--------|---------------|
| Consensus security model | OPEN RESEARCH | Byzantine fault model, adversary assumptions, finality guarantees |
| Network security | OPEN RESEARCH | Transport encryption, peer authentication, DoS resistance, metadata privacy |
| Transaction privacy | OPEN RESEARCH | Confidential transactions, identity privacy, selective disclosure |
| Key management | OPEN RESEARCH | Permissionless identity, key rotation, threshold signatures, recovery |
| Node and key custody | OPEN RESEARCH | Non-custodial key management, no master key (principle P8) |

## Known limitations of FeinStatic (for reference only)

The following limitations were identified in the FeinStatic v2.1.0 codebase and are
recorded here so they are not carried forward unexamined into FST:

| Limitation | Location | FST implication |
|------------|----------|-----------------|
| Crash-fault Raft, not Byzantine | `src/consensus/raft.cpp` | Consensus must be replaced for permissionless Byzantine security |
| Pre-provisioned peer rosters | `src/consensus/raft.cpp`, `src/network/node.cpp` | No permissionless validator entry |
| Operator roles and trust store | `include/feinstat/network/peer_identity.hpp` | Permanent privileged authority conflicts with P2, P8 |
| Single Bridge vault wallet | `src/bridge/bridge_service.cpp` | Centralised custody conflicts with P1, P2, P8 |
| Application-layer encryption, not TLS; no crypto audit | `src/network/node.cpp`, `src/crypto/crypto.cpp` | External cryptographic review required |
| Unkeyed SHA-256 checksums on state | `src/storage/sqlite_store.cpp` | Corruption detection only, not authentication |
| No independent cryptographic audit | Documentation | Security research must include independent review |

## How to add a security research entry

1. Copy the research-entry-template
   (`../research/research-entry-template.md`) to a new file in this directory.
2. Complete all fields, with emphasis on threat model, adversary assumptions, and
   security assumptions.
3. Add a row to the register table above.
4. Cross-reference from related ADRs and decisions.

## Cross-references

- [`../research/research-register.md`](../research/research-register.md) — General research register.
- [`../research/research-entry-template.md`](../research/research-entry-template.md) — Entry template.
- [`../adr/adr-template.md`](../adr/adr-template.md) — ADR template.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision register.
- [`../../architecture/feinstat-baseline.md`](../../architecture/feinstat-baseline.md) — FeinStatic baseline.
