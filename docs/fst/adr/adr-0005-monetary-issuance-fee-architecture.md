# ADR-0005 Monetary Issuance and Transaction-Fee Architecture

## Metadata

| Field | Value |
|-------|-------|
| ID | ADR-0005 |
| Title | Monetary issuance and transaction-fee architecture |
| Date | 2026-11-17 |
| Status | ACCEPTED |
| Classification | APPROVED (direction); numerical parameters are testnet-labelled research values |
| Author | FST project owner |
| Reviewed by | — |
| Supersedes | — |
| Superseded by | — |

## Problem statement

FST requires a deterministic monetary policy: predetermined declining issuance approaching a small permanent tail emission, combined with congestion-adjusted transaction fees. Issuance must not depend on unilateral administrative minting or discretionary organisational control. Transaction pricing uses an algorithmically congestion-adjusted base fee and a bounded priority-fee mechanism. The base fee is burned; applicable priority rewards go to block-producing validators. The interaction between issuance, fee burning, validator rewards, economic security and long-term dilution must be evaluated. No initial supply, genesis allocation percentages, emission percentages, terminal issuance, validator reward rates or fee parameters may be invented as production policy. No organisation-controlled treasury, unilateral minting key or arbitrary emission override may be introduced.

All consensus-critical financial arithmetic must use exact bounded integer/fixed-point methods; no floating-point. The implementation must make it possible to independently verify the complete supply history.

## Existing research and references

- S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," 2008 — fixed supply, halving issuance, fee-only long-run security (baseline comparison).
- V. Buterin et al., EIP-1559 "Fee market change for Ethereum," 2019 — base fee burning + priority fee, congestion adjustment (congestion-adjusted base fee mechanism reference).
- C. Cachin and M. Vukolic, "Blockchain Consensus Protocols in the Wild," arXiv:1707.01873, 2017 — validator rewards and security budget.
- D. Irresberger and F. Yang, "The Economics of Proof of Stake," Economics Letters, 2023 — validator participation incentives, security budget, concentration.
- Vitalik Buterin, "Economic abstraction and issuance" / Ethereum issuance discussions — declining issuance to tail emission concept (comparison baseline).
- FST research: docs/fst/research/research-register.md (RS-006 monetary policy, TBD doc), docs/fst/security/adversary-model-v0.1.md, docs/fst/scope/scope-v0.1.md.

Source verification status: RS-006 monetary-policy research entry is TBD (not yet produced); sources above are comparison baselines cited from RS-001/RS-002/RS-013 where inspected. A dedicated monetary-policy simulation document must be produced before production parameter selection.

## Alternatives evaluated

| Alternative | Advantages | Disadvantages | Classification |
|-------------|------------|---------------|----------------|
| Declining issuance + permanent small tail emission + congestion-adjusted base fee (burned) + bounded priority fee (to validators) | Predictable declining supply; long-run validator security from tail emission + priority fees; base fee burning counteracts issuance (deflationary pressure under congestion); congestion adjustment smooths fee volatility; priority fee bounds MEV extraction; deterministic and auditable | Tail emission causes perpetual (small) dilution; fee volatility remains under extreme congestion; requires tuning of congestion algorithm and tail rate; security budget under low-adoption/low-token-value scenarios needs simulation | APPROVED direction (D-015); parameters testnet-labelled |
| Fixed supply + fee-only validator security | No inflation/dilution; simple | Long-run validator security depends entirely on fees; security-budget instability if fees fall; high barrier to early participation | OPEN RESEARCH (comparison) |
| Alternative deterministic emission curves (linear, step, exponential decay) | Tunable issuance shape | Each requires simulation; no FST-specific justification yet | OPEN RESEARCH (comparison) |
| Unbounded priority fee / first-price auction | Simpler fee market | Fee volatility; MEV/censorship incentives; poor user predictability | Not viable |
| Organisation-controlled treasury / unilateral minting | Simple issuance control | Centralised control; conflicts with P1, P2, P8, D-015 | Not viable |

## Reasoning

The owner approved D-015: predetermined declining issuance approaching a small permanent tail emission, combined with congestion-adjusted transaction fees, with base fee burned and priority rewards to block-producing validators. This gives a deterministic, auditable supply schedule not dependent on administrative discretion (P1, P2, P8). The tail emission preserves long-run validator security beyond the fixed-supply horizon. Base-fee burning aligns fees with network congestion and creates deflationary counter-pressure to issuance. Bounded priority fees give users fee predictability and bound MEV extraction.

All numerical parameters (genesis supply, allocation, issuance schedule, emission rates, terminal issuance, tail rate, reward rates, fee parameters, congestion algorithm constants) are explicitly NOT production constants. They are research and engineering tasks requiring simulation across low-adoption, normal-adoption, high-adoption and low-token-value scenarios. Values used in implementation or testnets must be clearly labelled as testnet-only and never silently converted into production policy.

## Selected design

**Supply accounting components (strict invariants, exact bounded integer/fixed-point arithmetic):**
- Genesis allocation (configurable/testnet-labelled).
- Scheduled issuance by height or epoch (deterministic declining curve, tail emission).
- Validator rewards (from issuance + priority fees).
- Priority-fee rewards (bounded priority fee portion, to block producer).
- Base-fee burning (congestion-adjusted base fee portion, removed from supply).
- Total circulating supply, total bonded stake, burned amount (all independently verifiable).

**Issuance:** deterministic algorithm by height/epoch; declines toward a small continuing validator-reward emission; no administrative minting.

**Transaction pricing:** base fee = congestion-adjusted function of recent block demand (bounded, deterministic); priority fee = bounded user-specified tip; total fee = base fee + priority fee. Base fee burned; priority fee to block-producing validator.

**Conservation:** supply conservation checked every block (issuance + rewards − burned = supply delta; no overflow/underflow; maximum supply check where applicable).

**Simulation before parameters:** model low-adoption, normal-adoption, high-adoption, low-token-value scenarios; assess inflation/dilution, validator incentives, concentration, security-budget stability, fee predictability/volatility, collusion, MEV/censorship.

**Implementation locations:** new module `src/monetary/` and `include/feinstat/monetary/` (issuance schedule, reward accounting, fee computation, base-fee burn, supply conservation, overflow/underflow checks). Integrated into the FST ABCI application `FinalizeBlock`/`Commit` (ADR-0004) and staking rewards (ADR-0003).

## Security assumptions

- Exact integer/fixed-point arithmetic prevents rounding drift and overflow in supply accounting.
- Base-fee burn is deterministic and verifiable; no discretionary burning/minting.
- Validator rewards derived from issuance + priority fees only; no treasury or administrative override.

## Limitations and trade-offs

- Numerical parameters unset; incorrect tail rate or congestion algorithm could destabilize security budget or fee predictability.
- Base-fee burning reduces validator revenue relative to total fees (only priority fees go to validators); under low congestion validator revenue may be dominated by tail emission — simulation required.
- Low-token-value scenarios reduce the real economic value of rewards/stake, weakening security budget — must be modelled.
- Congestion adjustment can be manipulated (spam to raise base fee); bounded priority fee and admission control mitigate.

## Verification evidence

None yet — implementation pending. Tests required for: supply conservation, overflow/underflow, rounding/dust, extreme congestion, no-transaction periods, long-term declining issuance, tail-emission behaviour, low-token-value security scenarios, validator participation under different fee revenue, fee manipulation/MEV incentives, deterministic replay, restart recovery. Simulations must be produced and documented before production parameter selection.

## Cross-references

- [`decision-register.md`](../decisions/decision-register.md) — D-015 APPROVED.
- [`research-register.md`](../../research/research-register.md) — RS-006 (monetary policy, TBD).
- [`security/adversary-model-v0.1.md`](../security/adversary-model-v0.1.md) — economic security, concentration threat classes.
- [`adr/adr-0002-utxo-ledger-transaction-architecture.md`](../adr/adr-0002-utxo-ledger-transaction-architecture.md) — transaction fees in UTXO model.
- [`adr/adr-0003-permissionless-pos-validator-eligibility.md`](../adr/adr-0003-permissionless-pos-validator-eligibility.md) — validator rewards.
- [`adr/adr-0004-bft-consensus-cometbft-integration.md`](../adr/adr-0004-bft-consensus-cometbft-integration.md) — monetary/fees in application state via FinalizeBlock/Commit.
