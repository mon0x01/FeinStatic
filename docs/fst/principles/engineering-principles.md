# FST Engineering Principles

**Status:** APPROVED — recorded by the project owner.
**Applies to:** All FST modules, subsystems, and implementation decisions.

These principles are the approved foundational requirements for FST. They govern every
engineering decision and must be referenced when proposing, designing, or reviewing any
change to the FST codebase or documentation.

## Principle list

| ID | Principle | Classification |
|----|-----------|----------------|
| P1 | No single organisation should possess unilateral control over FST. | APPROVED |
| P2 | No organisation should have permanent privileged authority over the protocol. | APPROVED |
| P3 | Protocol development and important discussions should be transparent. | APPROVED |
| P4 | Independent development teams must be able to maintain the software. | APPROVED |
| P5 | Participants must retain the ability to reject proposed upgrades. | APPROVED |
| P6 | Major disagreements may result in independent network forks. | APPROVED |
| P7 | Neither fork automatically receives exclusive legitimacy. | APPROVED |
| P8 | No central emergency administrator or unilateral master key. | APPROVED |
| P9 | Security should primarily emerge from protocol design and independent verification. | APPROVED |
| P10 | The network should survive the disappearance of its founding developers. | APPROVED |
| P11 | Documentation must preserve the reasoning behind every important engineering decision. | APPROVED |
| P12 | Every module and implementation detail must follow research-first engineering. | APPROVED |
| P13 | Build the core financial network first; add services incrementally. | APPROVED |
| P14 | Development proceeds through small, thoroughly tested and documented releases. | APPROVED |

## How to apply these principles

When designing or reviewing any FST component, answer the following:

- **Control:** Does this component introduce a single point of unilateral control? (P1, P2, P8)
- **Upgradeability:** Can participants reject proposed upgrades without losing access? (P5)
- **Fork safety:** If the network forks, does this component treat both chains as potentially legitimate? (P6, P7)
- **Founder independence:** Would the network continue to operate if the founding developers disappeared? (P10)
- **Transparency:** Is the design documented with reasoning, references, and limitations? (P3, P11)
- **Research basis:** Is the design grounded in reviewed academic research, standards, or prior art? (P12)
- **Incremental release:** Can this component be delivered in a small, tested, documented release? (P13, P14)

## Relationship to FeinStatic

The existing FeinStatic implementation (v2.1.0, commit `a8eac41`) was designed as a
permissioned, crash-fault-tolerant prototype for private test infrastructure. Several of
its architectural assumptions conflict with the FST principles above:

- **BridgeService** uses a single local vault wallet, creating centralised custody (conflicts with P1, P2, P8).
- **Operator roles** (`PeerRole::Operator`) and the trust store confer permanent privileged authority (conflicts with P2).
- **Pre-provisioned peer rosters** prevent permissionless validator participation (conflicts with P4, P5).
- **Raft consensus** is crash-fault tolerant only; it does not address the separate BFT research direction under D-017. P9 states that security should primarily emerge from protocol design and independent verification, but does **not** mandate Byzantine fault-tolerant consensus.
- **No upgrade/governance mechanism** exists for participants to reject changes (conflicts with P5).

These conflicts are documented in [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) and the
[decision register](../decisions/decision-register.md). They are not bugs to fix in place;
they are architectural assumptions that must be reconsidered in the FST redesign.

## Cross-references

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope.
- [`../architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — Existing FeinStatic baseline.
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications.
