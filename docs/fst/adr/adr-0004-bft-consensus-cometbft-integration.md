# ADR-0004 BFT Consensus, Finality and CometBFT Integration

## Metadata

| Field | Value |
|-------|-------|
| ID | ADR-0004 |
| Title | BFT consensus, finality and CometBFT integration |
| Date | 2026-11-17 |
| Status | ACCEPTED |
| Classification | APPROVED (architecture); CometBFT integration subject to mandatory feasibility evaluation |
| Author | FST project owner |
| Reviewed by | — |
| Supersedes | — |
| Superseded by | — |

## Problem statement

FST requires stake-weighted Byzantine fault-tolerant consensus with deterministic finality. Finality must have explicit safety, liveness and network-synchrony assumptions. The implementation should prefer an established, maintained, extensively tested consensus implementation over writing a new BFT algorithm. CometBFT is the leading implementation candidate and is subject to a mandatory implementation and security feasibility evaluation. Consensus voting power must be derived from deterministic FST application state. Permissionless validator entry is preserved through FST staking and validator-set application logic (D-016). CometBFT must not implement FST monetary policy, staking, transaction validation or validator economics — those belong to the deterministic FST application. If material incompatibilities are discovered, document evidence and a proposed alternative rather than silently changing the approved architecture.

## Existing research and references

- M. Castro and B. Liskov, "Practical Byzantine Fault Tolerance," OSDI 1999 — classical BFT (C1 composition reference).
- L. Lamport, R. Shostak and M. Pease, "The Byzantine Generals Problem," ACM TOPLAS 1982 — Byzantine agreement problem.
- M. J. Fischer, N. A. Lynch and M. S. Paterson, "Impossibility of Distributed Consensus with One Faulty Process," JACM 1985 (FLP) — asynchronous consensus impossibility.
- C. Dwork, N. Lynch and L. Stockmeyer, "Consensus in the Presence of Partial Synchrony," JACM 1988 (DLS) — partial synchrony, safety/liveness assumptions.
- J. Kwon and E. Buchman, "Cosmos Network / Tendermint consensus," 2016–2019 — leader-based BFT with deterministic finality under partial synchrony (C2 composition).
- E. Buchman, "Tendermint: Byzantine Fault Tolerance in the Age of Blockchains," 2016 — Tendermint consensus design, ABCI interface.
- CometBFT documentation, https://docs.cometbft.com/ — ABCI/ABCI++ application interface, validator-set updates, state sync, snapshots, light-client verification. (To be re-verified against the selected stable release during the feasibility spike.)
- M. Yin et al., "HotStuff: BFT Consensus in the Lens of Blockchain," ACM CCS 2019 / arXiv:1803.05069 — leader-based BFT, view changes (C1 composition).
- V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437 — checkpoint finality (C5 composition).
- G. Danezis et al., "Narwhal and Tusk: a DAG-based mempool and efficient BFT consensus," arXiv:2105.11827 / SOSP 2021 — mempool/consensus separation (C6 alternative).
- FST research: docs/fst/research/consensus-validator-research.md (RS-013), docs/fst/security/adversary-model-v0.1.md, docs/fst/architecture/permissionless-test-network-evaluation.md.

Source verification status: sources recorded in RS-013 as inspected (19 VERIFIED, 11 PARTIALLY VERIFIED); exact verification status per source follows docs/fst/research/research-register.md. CometBFT release/advisory status must be re-verified against the actual selected release during the feasibility spike (see Verification evidence).

## Alternatives evaluated

| Alternative | Advantages | Disadvantages | Classification |
|-------------|------------|---------------|----------------|
| Tendermint/CometBFT (stake-weighted BFT, deterministic finality) | Established, maintained, extensively tested; deterministic finality with explicit partial-synchrony assumptions; ABCI application interface decouples consensus from application state; validator-set updates with activation-height semantics; state sync and snapshots; light-client verification | Go dependency in C++ project (run as separate process via Unix-domain socket ABCI); operational overhead; must verify selected stable release includes CSA-2026-001/Tachyon fix; application must be fully deterministic | APPROVED candidate (D-017), subject to feasibility evaluation |
| HotStuff / C1 composition | Optimistic responsiveness, linear view changes | Less mature production deployment; deterministic finality model differs; more integration effort | OPEN RESEARCH (alternative) |
| Nakamoto PoW / C3 | Permissionless, no validator set | Probabilistic finality, high energy, no deterministic finality; conflicts with D-017 | Not viable |
| Casper FFG / Gasper / C5 | Checkpoint finality, PoS-native | Complex fork-choice + finality gadget; Ethereum-specific economics; high integration complexity | OPEN RESEARCH (alternative) |
| Narwhal/Tusk / C6 | High throughput via mempool/consensus separation | Less mature; more components; integration risk | OPEN RESEARCH (alternative) |
| FeinStatic Raft / D-024 | Existing codebase | Crash-fault only, not BFT; conflicts with P9/D-017 | Reference only |

## Reasoning

The owner approved D-017: stake-weighted Byzantine fault-tolerant consensus with deterministic finality, preferring an established implementation (CometBFT family). Tendermint/CometBFT provides deterministic finality under explicit partial-synchrony assumptions (safety holds as long as fewer than 1/3 of voting power is Byzantine; liveness under synchrony after GST). This matches FST's financial-infrastructure safety requirements better than probabilistic finality.

Running CometBFT as a separate consensus process with the C++ FST application connected via a local Unix-domain socket using the supported ABCI protocol keeps the application interface off the public network and preserves the C++ codebase. ABCI application execution must be deterministic: identical committed transactions and prior state must yield identical application state and application hash on every correct node.

Validator updates follow CometBFT's documented activation-height semantics; voting power is derived from deterministic FST application state (staking module, ADR-0003), not from CometBFT internals.

The feasibility spike (below) must verify the selected stable CometBFT release, its maintenance status, security advisories (including CSA-2026-001/Tachyon), ABCI/ABCI++ compatibility, validator-set update behaviour, state synchronization, snapshot requirements, light-client verification, upgrade/migration, and performance/operational overhead. If material incompatibilities are found, evidence and a proposed alternative must be documented rather than silently switching consensus families.

## Selected design

**Consensus engine:** CometBFT (stable release, NOT the moving main branch), run as a separate process per validator node.

**Application interface:** local Unix-domain socket, ABCI protocol. Supported ABCI lifecycle methods implemented by the C++ FST ABCI application:
- `InitChain` — genesis initialization, validator set, initial application state.
- `PrepareProposal` / `ProcessProposal` — transaction filtering/validation hints (application-deterministic).
- `FinalizeBlock` — apply transactions, update UTXO/staking state, compute rewards/fees, return validator updates.
- `Commit` — persist application state, compute and return application hash (state commitment).
- Validator updates — returned from `FinalizeBlock`; CometBFT applies per activation-height semantics.
- State synchronization — snapshot/chunk serving and restoration for state sync.

**Determinism:** the FST ABCI application is a pure function of (prior state, committed transactions). No non-deterministic external API call, wall-clock dependency, unordered iteration, floating-point calculation or local filesystem input may influence consensus state transitions. Floating-point is prohibited for all consensus-critical financial calculations (exact bounded integer/fixed-point only).

**Application state:** UTXO set (ADR-0002) + staking/validator state (ADR-0003) + monetary/fee state (ADR-0005), all deterministic.

**Application hash:** reproducible state root over the committed application state, returned in `Commit`, included in block header; every correct node computes an identical hash.

**Persistence and restart recovery:** committed application state persisted before `Commit` acknowledges; crash recovery must not allow partially committed financial state to become authoritative (atomic commit semantics).

**Integration locations:** new module `src/consensus/abci/` and `include/feinstat/consensus/abci/` (C++ ABCI application, Unix-domain socket client, determinism guards). CometBFT process managed externally (test network orchestrator); not vendored into the C++ build.

## Security assumptions

- Fewer than 1/3 of voting power Byzantine (BFT safety bound).
- Partial synchrony for liveness; safety is synchronous-independent (holds even during asynchrony/partitions, at cost of liveness).
- Validator voting power derived from deterministic application state; CometBFT does not adjudicate FST economics.
- Deterministic application execution prevents state divergence (forking due to non-determinism).

## Limitations and trade-offs

- Go runtime and deployment requirements for CometBFT process; C++ project remains C++20.
- Operational overhead of running and upgrading a separate consensus process.
- ABCI socket is a local interface; must be protected by filesystem permissions (not exposed publicly).
- Feasibility evaluation may reveal material incompatibilities; fallback is a documented alternative (HotStuff/Casper/Narwhal), not a silent switch.
- CometBFT does not implement FST monetary policy/staking/fee economics; those must be correctly implemented in the application layer.

## Verification evidence

**Feasibility spike required before production dependency:**
- Current stable CometBFT release (version number, release date, maintenance status).
- Security advisories and patch status, including CSA-2026-001/Tachyon — chosen release must include its fix.
- Dependency and licensing obligations; Go runtime requirements.
- ABCI/ABCI++ compatibility; validator-set update behaviour and activation-height semantics.
- State synchronization, snapshot requirements, light-client verification.
- Upgrade and migration process; performance and operational overhead.
- Minimal prototype: one CometBFT process + one C++ FST ABCI application over Unix-domain socket; genesis init, deterministic application state, transaction submission, block finalization, state hashing, persistence/restart recovery; then isolated multi-node test network (>=4 validators).
- Determinism tests, crash-recovery tests (no partially committed state), activation-height validator update tests.

Record results in verification-status.md. Until the spike completes and results are recorded, CometBFT remains an APPROVED candidate but not a committed production dependency.

## Cross-references

- [`decision-register.md`](../decisions/decision-register.md) — D-017 APPROVED.
- [`research-register.md`](../../research/research-register.md) — RS-001, RS-013.
- [`security/adversary-model-v0.1.md`](../security/adversary-model-v0.1.md) — BFT safety/liveness assumptions, f < 1/3.
- [`architecture/permissionless-test-network-evaluation.md`](../architecture/permissionless-test-network-evaluation.md) — consensus evaluation.
- [`adr/adr-0003-permissionless-pos-validator-eligibility.md`](../adr/adr-0003-permissionless-pos-validator-eligibility.md) — validator voting power derivation.
- [`adr/adr-0002-utxo-ledger-transaction-architecture.md`](../adr/adr-0002-utxo-ledger-transaction-architecture.md) — deterministic application state.
- [`adr/adr-0005-monetary-issuance-fee-architecture.md`](../adr/adr-0005-monetary-issuance-fee-architecture.md) — monetary/fees in application state.
