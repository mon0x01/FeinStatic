# ADR-0003 Permissionless native-FST Proof-of-Stake Validator Eligibility

## Metadata

| Field | Value |
|-------|-------|
| ID | ADR-0003 |
| Title | Permissionless native-FST Proof-of-Stake validator eligibility |
| Date | 2026-11-17 |
| Status | ACCEPTED |
| Classification | APPROVED |
| Author | FST project owner |
| Reviewed by | — |
| Supersedes | — |
| Superseded by | — |

## Problem statement

FST requires permissionless validator participation under publicly verifiable protocol rules, with native FST as the scarce resource establishing validator voting power. Validator identity must be separate from node identity and ordinary network participation. Voting power must derive from active bonded stake, not from the number of identities or keys. There must be no permanent validator whitelist, privileged organisation, or manually controlled validator registry. Deterministic staking, delegation assessment, validator registration, exit and unbonding rules, and evidence-based slashing for provable protocol violations are required. Validator concentration, pool centralisation, wealth concentration, long-range attacks, nothing-at-stake attacks and economic security must be analysed. No minimum stake, unbonding period, validator count, epoch length or economic parameter may be invented as production policy.

## Existing research and references

- S. King and S. Nadal, "PPCoin: Peer-to-Peer Crypto-Currency with Proof-of-Stake," 2012 — earliest PoS proposal, coin age. (Referenced via RS-002/RS-013.)
- I. Eyal and E. G. Sirer, "Majority is not Enough: Bitcoin Mining is Vulnerable," FC 2014 — selfish mining / concentration analysis (referenced via RS-002).
- V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437, 2017 — PoS finality gadget, slashing, nothing-at-stake. (Referenced via RS-013.)
- C. Buragohain et al. / PoS concentration literature and D. Irresberger and F. Yang, "The Economics of Proof of Stake," Economics Letters, 2023 — validator concentration and security budget (referenced via RS-013/RS-002).
- S. Micali, "Algorand," — verifiable random functions for committee selection (comparison baseline, RS-013).
- C. Cachin and M. Vukolic, "Blockchain Consensus Protocols in the Wild," arXiv:1707.01873, 2017 — Sybil resistance mechanisms comparison.
- FST research: docs/fst/research/sybil-resistance-research.md (RS-002), docs/fst/research/consensus-validator-research.md (RS-013), docs/fst/security/adversary-model-v0.1.md, docs/fst/scope/scope-v0.1.md.

Source verification status: sources recorded in RS-002/RS-013 as inspected; exact verification status per source follows docs/fst/research/research-register.md. No source count is inflated.

## Alternatives evaluated

| Alternative | Advantages | Disadvantages | Classification |
|-------------|------------|---------------|----------------|
| Permissionless native-FST PoS | Native FST as scarce resource; permissionless entry under public rules; voting power from bonded stake (not identity count); no whitelist or privileged registry; deterministic staking/unbonding/slashing; separates validator identity from node identity | Requires accurate stake economics; concentration/pool centralisation risk; nothing-at-stake/long-range attack surface requires slashing + unbonding design; economic parameters are testnet-labelled until simulated | APPROVED (D-016) |
| Proof of Work | Permissionless entry; well-understood security | Energy cost; mining-pool centralisation; does not use native FST as staking resource; voting power derived from hash rate not stake | OPEN RESEARCH (not selected) |
| Delegated PoS (fixed delegates) | High throughput | Fixed delegate set risks centralisation and privileged registry; conflicts with P1/P2 | OPEN RESEARCH (not selected) |
| Proof of Space / Proof of Elapsed Time | Alternative scarce resources | Immature production implementations; does not use native FST as staking resource | OPEN RESEARCH (not selected) |
| Permissioned validator set | Simplicity | Centralised custody/authority; conflicts with P1, P2, P4, P5 | Not viable |

## Reasoning

The owner approved D-016: permissionless native-FST Proof of Stake. Native FST as the scarce resource aligns voting power with economic stake in the network's own currency, giving validators a financial interest in correct behaviour (slashing risk) and aligning validator security with token value. Permissionless entry under publicly verifiable rules preserves P1, P4, P5. Separating validator identity from node identity and financial ownership limits blast radius of key compromise and supports key rotation/recovery.

Deterministic staking, delegation assessment, registration, exit, unbonding, and evidence-based slashing keep the validator set a function of deterministic FST application state, which consensus voting power must derive from (D-017). Parameters (minimum stake, unbonding period, validator count, epoch length, reward rates) are deliberately NOT set as production constants; they remain research/engineering tasks with clearly labelled testnet-only values. No minimum stake, unbonding period, validator count, epoch length or economic parameter is invented here.

Concentration analysis (validator concentration, pool centralisation, wealth concentration, long-range attacks, nothing-at-stake) is required before parameter selection and is recorded as open research questions feeding parameter simulation (Phase 10/RS-006).

## Selected design

**Staking as deterministic FST application state:** bond, unbond, delegate, redelegate, and slash operations are deterministic state transitions applied by the FST application (not by consensus directly).

**Validator identity:** distinct from node identity and financial ownership. A validator holds a consensus key (for voting) and an operator/staking key (for staking actions); these may be rotated independently. Key rotation and recovery are supported.

**Eligibility and registration:** any participant may register a validator by bonding native FST and publishing a consensus key. Registration is a deterministic transaction evaluated against published protocol rules; no whitelist or discretionary admission.

**Voting power:** deterministic conversion function from active bonded stake to voting power. Function and parameters are configurable/testnet-labelled until justified by simulation.

**Validator-set construction and updates:** validator set at each height derived deterministically from application state; updates follow CometBFT activation-height semantics (validators bonded in block N participate starting at height N+k per documented semantics).

**Entry/exit and unbonding:** validators may exit; bonded stake enters an unbonding period before becoming liquid (duration configurable/testnet-labelled). During unbonding, stake may still be slashable for violations committed while active.

**Slashing:** evidence-based only — provable protocol violations (equivocation: signing two conflicting votes at the same height/round) produce verifiable evidence that triggers deterministic slashing of bonded stake. No arbitrary or discretionary slashing; no privileged emergency slashing authority.

**Delegation:** delegation assessment supported (delegators bond to validators); delegation limits and concentration metrics are testnet-labelled until justified.

**Concentration metrics:** validator concentration, pool centralisation, wealth concentration tracked as observables; inform parameter tuning but are not consensus-critical enforcement unless slashing evidence dictates.

**Implementation locations:** new module `src/staking/` and `include/feinstat/staking/` (bonding/unbonding, validator registry, voting-power conversion, validator-set construction, evidence handling, slashing, reward accounting hooks). Consumes native FST balances via the UTXO ledger (ADR-0002) as application state.

## Security assumptions

- Bounded Byzantine validator fraction (f < 1/3 of voting power) for BFT safety (D-017).
- Slashing evidence is unforgeable and attributable (cryptographic signatures over consensus messages).
- Unbonding period provides a slashing window for post-exit violations.
- Nothing-at-stake mitigated by slashing equivocation and cost-of-stake (bonded FST at risk).

## Limitations and trade-offs

- Economic parameters are unset; incorrect values could centralise validators or weaken security. Simulation required before testnet deployment.
- Long-range attacks: old stake can be used to rewrite history; mitigated by checkpointing/trust assumptions (light-client trust period) — detailed design deferred.
- Delegation can concentrate voting power in pools; concentration metrics and pool limits are research items.
- Stake grinding (manipulating randomness for proposer selection) depends on consensus design; CometBFT proposer selection is weighted round-robin — evaluate for grinding resistance.

## Verification evidence

None yet — implementation pending. Unit, integration, property-based, and fault-injection tests required for: bonding/unbonding, validator registration, voting-power conversion, validator-set updates, activation-height semantics, equivocation evidence, slashing, exit/unbonding, restart and state-sync correctness, concentration metrics.

## Cross-references

- [`decision-register.md`](../decisions/decision-register.md) — D-016 APPROVED.
- [`research-register.md`](../../research/research-register.md) — RS-002, RS-013.
- [`security/adversary-model-v0.1.md`](../security/adversary-model-v0.1.md) — stake concentration, nothing-at-stake, long-range threat classes.
- [`architecture/permissionless-test-network-evaluation.md`](../architecture/permissionless-test-network-evaluation.md) — PoS evaluation input.
- [`adr/adr-0004-bft-consensus-cometbft-integration.md`](../adr/adr-0004-bft-consensus-cometbft-integration.md) — consensus voting-power derivation.
- [`adr/adr-0005-monetary-issuance-fee-architecture.md`](../adr/adr-0005-monetary-issuance-fee-architecture.md) — validator rewards issuance interaction.
