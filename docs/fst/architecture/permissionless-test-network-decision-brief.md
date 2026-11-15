# FST Permissionless Test-Network Decision Brief — v0.1

**Status:** DRAFT — owner decision brief, not an approved decision package.
**Classification:** OPEN RESEARCH / PROVISIONAL as recorded (no decision is approved by this document).
**Related decisions:** D-015 (OPEN RESEARCH), D-016 (PROVISIONAL), D-017 (OPEN RESEARCH), D-018 (PROVISIONAL), D-019 (PROVISIONAL), D-024 (PROVISIONAL), D-035 (OPEN RESEARCH), D-036 (APPROVED).
**Related research:** RS-001 (consensus), RS-002 (Sybil resistance), RS-013 (consensus–validator), RS-015 (transaction model).
**Related documents:** `docs/fst/security/adversary-model-v0.1.md` (FST adversary model), `docs/fst/architecture/permissionless-test-network-evaluation.md` (deep candidate evaluation).

This document prepares **owner decision briefs** for six decision areas required before a minimal permissionless FST test network can be designed and implemented. It distinguishes **research findings** (from RS-001, RS-002, RS-013, RS-015 and the adversary model) from **owner decisions** (which remain unmade).

**No owner decision has been approved.** No consensus algorithm, Sybil-resistance mechanism, Byzantine threshold, finality model, or transaction model is selected, recommended for implementation, or silently approved here. All alternatives remain open.

## Decision brief 1 — Transaction model (RS-015; related: D-015, D-036)

### Available alternatives
Research RS-015 evaluates four models; two are credible candidates for FST's payment scope (native FST transfers, P2P, merchant, institutional, batch):

1. **Traditional UTXO** (Bitcoin model). Stateless, local validation; native double-spend prevention (each output spent once); high parallelism for non-conflicting inputs; deterministic fee prediction; change-output pattern; UTXO-set growth; weak fit for multi-input institutional batching without interaction.
2. **Account-based with nonce and chain-ID protection** (Ethereum/EIP-155 model). Simple global balance; natural multi-input institutional batching; simple wallet UX; nonce-based replay protection plus chain-ID binding. Globally shared mutable state; per-account nonce serialization; state-dependent validation (can fail mid-execution); weaker privacy (stable public addresses).
3. **eUTXO** (Cardano/Nervos Cell model). UTXO-style double-spend prevention with programmable locks and data-carrying outputs; deterministic off-chain validation; high parallelism; larger transactions; higher implementation complexity.
4. **Hybrid/object-cell** (Nervos Cell / object models). First-class assets, multi-object atomicity, delegated fee payment; high implementation complexity; least battle-tested for a pure payment network.

(eUTXO and hybrid are surveyed but not the primary sensitivity cases; both remain available.)

### Technical consequences
- UTXO: stateless validation enables off-chain pre-validation and parallel execution; reorgs re-unspend outputs cleanly (fits probabilistic finality); replay protection requires an added chain-ID binding mechanism (D-036 mechanisms OPEN RESEARCH).
- Account-based: state-dependent validation requires global state look-up; nonce ordering forces per-account serialization; reorgs revert nonce/balance changes creating replay risk; chain-ID binding (EIP-155 style) is native.
- Serialization format, nonce model, transaction-identifier format, and fee policy are all unresolved and blocked on this decision (core spec Section 8; RS-015 §2.3).

### Security assumptions
- Neither model provides anonymous privacy; both are pseudonymous and require additional privacy layers (RS-015 §4).
- UTXO double-spend prevention is native; account-based relies on nonce + balance enforced by global state.
- Replay protection across forks is not inherent to either; D-036 (APPROVED) requires it, mechanisms OPEN RESEARCH.
- Account-based nonce model couples transaction validity to chain position, increasing reorg/replay complexity under probabilistic finality (RS-015 §5.14; RS-013 §21).

### Financial implications
- Fee calculation differs: UTXO per-byte/vbyte; account-based gas (compute + storage). Fee market design (D-015 monetary policy) is OPEN RESEARCH and not selected here.
- No issuance, supply, or reward parameters are asserted (D-015 OPEN RESEARCH).

### Advantages and disadvantages
**UTXO:** + parallelism, native double-spend prevention, deterministic fees, SPV-friendly light clients; − change-output pattern, UTXO-set growth, weaker multi-input batching, replay protection needs added mechanism.
**Account-based:** + simple global balance, natural batching, simple UX, native chain-ID replay protection; − shared mutable state bottleneck, nonce complexity, front-running/ordering sensitivity, weaker privacy, harder pruning.

### Unresolved questions
- Which model best suits FST's financial use case (P2P, merchant, institutional, batch, settlement)?
- How does the model interact with the chosen consensus family and finality model (RS-013 §21)?
- What chain-ID binding mechanism provides cross-network replay protection (D-036 mechanisms)?
- What fee policy and fee market (D-015)?

### Research needed before implementation
- RS-015 is complete as a research deliverable but remains OPEN RESEARCH pending owner selection (D-015).
- Full-paper inspection of RS-015's primary sources (Bitcoin devguide, Ethereum EIP-155/EIP-1559, Cardano eUTXO, Nervos CKB) to confirm technical claims (RS-015 records these as inspected; verification status varies).
- Transaction serialization format (core spec Section 8) cannot be specified before this decision.

**Owner decision required:** Select transaction model (or retain both as sensitivity cases). **Not approved.** D-015 remains OPEN RESEARCH.

---

## Decision brief 2 — Sybil-resistance direction (RS-002; related: D-016 PROVISIONAL)

### Available alternatives
RS-002 Section 3 evaluates: Proof of Work, Proof of Stake, Delegated Proof of Stake, Proof of Space/Proof of Space-Time, Proof of Authority (baseline only, rejected for FST), reputation/social-trust/Proof-of-Personhood, resource-based/hybrid approaches.

Direction under consideration (PROVISIONAL, D-016): permissionless validator participation, exact mechanism unresolved.

### Technical consequences
- Sybil resistance converts cheap identity into an expensive resource (work, stake, space, or other) [Douceur2002 S5].
- The six concepts must be kept separate: node identity, peer admission, validator eligibility, validator selection, voting power, consensus quorum formation (RS-002 §2). A mechanism resisting Sybils at one layer may not at another.
- The Sybil layer's output (voting-power distribution) must respect the consensus fault bound; concentration breaks it regardless of Sybil mechanism effectiveness (RS-013 §9.2, §18.8).

### Security assumptions
- Without Sybil resistance, quorum thresholds and Byzantine bounds are meaningless (unlimited identities exceed any threshold) [Douceur2002 S5].
- PoW: honest-majority-of-hashrate (<50% adversarial); self-consistent (PoW is both Sybil mechanism and block-author selector) [Nakamoto2008 S7].
- PoS: honest-majority-of-stake (protocol-specific); stake concentration breaks the fault bound [OuroborosPraos2017 S13; Gasper2020 S15; IrresbergerYang2023 S39, PARTIALLY VERIFIED metadata only].
- DPoS concentrates power in 21–101 elected delegates — conflicts with P1/P2/P5; not an FST candidate under current principles (RS-002 §3.3).
- PoSpace/PoTime: storage cost; less mature deployment; VDF complexity (RS-002 §3.4).
- PoP/reputation: fails de facto Sybil resistance under economic incentive (puppeteering/oligopoly); privacy and exclusion issues (RS-002 §3.6).

### Financial implications
- Security budget depends on token value (stake) or hashrate value (work), both scaling with token value — economic circularity for a new network (RS-013 §7.14, §20; D-015 OPEN RESEARCH).
- Rewards/inflation, slashging, unbonding are economic parameters, unresolved (D-015 OPEN RESEARCH).

### Advantages and disadvantages
**PoW:** + genuinely permissionless, long deployment track record (Bitcoin), no stake concentration at genesis, no slashing complexity; − probabilistic finality, high energy cost, mining-pool concentration, 51% rental attacks on small networks, slow settlement.
**PoS:** + aligns with native FST asset (D-014), lower energy than PoW, deterministic checkpoint finality possible (Gasper); − economic circularity, wealth/stake concentration, long-range attacks, nothing-at-stake, weak subjectivity, complex slashing.

### Unresolved questions
- Which mechanism best suits FST's financial use case? (RS-002 Q1)
- How does the Sybil mechanism interact with the chosen consensus family? (RS-002 Q2)
- What voting-power distribution respects the consensus fault bound under FST's adversary model? (RS-002 Q3)
- How does FST prevent validator centralization (stake pools, mining pools, geographic concentration)? (RS-002 Q4)
- What are entry/exit mechanics (bonding, unbonding, slashing) that preserve safety? (RS-002 Q8)

### Research needed before implementation
- RS-002 remains OPEN RESEARCH; no mechanism selected.
- Additional source verification for mechanism-specific claims (full-paper inspection not yet performed per RS-013 §16.1).
- Sybil-resistance layer integration with consensus must be researched per composition (RS-013 §18.8).

**Owner decision required:** Confirm permissionless participation direction and select Sybil-resistance mechanism class. **Not approved.** D-016 remains PROVISIONAL (mechanism unresolved).

---

## Decision brief 3 — Consensus architecture (RS-001/RS-013; related: D-017 OPEN RESEARCH)

### Available alternatives
Three compositions were deeply evaluated (see `permissionless-test-network-evaluation.md`):

1. **C1: HotStuff-style BFT + stake-based validator eligibility** — deterministic finality, linear per-view communication (HotStuff), leader-driven, `<1/3 Byzantine voting power`, partial synchrony for liveness. Leader DoS target; stake centralisation.
2. **C3: Nakamoto consensus + Proof of Work** — genuinely permissionless, probabilistic finality, honest-majority-of-hashrate, O(n) block propagation, no slashing, long deployment track record. Energy cost, pool concentration, slow settlement.
3. **C5: PoS + separate finality gadget (Gasper-style: LMD-GHOST + Casper FFG)** — deterministic checkpoint finality, fork-choice/finality separation, ≥2/3 honest stake, accountable safety (≥1/3 slashable), partial synchrony for liveness. Weak-subjectivity trust, stake concentration, slashing complexity.

Considered alternatives (not shortlisted): C2 (Tendermint/CometBFT-style BFT + stake, close variant of C1), C4 (PoS longest-chain/Ouroboros Praos, probabilistic finality), C6 (DAG-based Narwhal/Tusk — not categorically stronger than C5), C7 (hybrid PoW/PoS — higher complexity).

### Technical consequences
- BFT candidates (C1, C5) assume partial synchrony for liveness; safety preserved under fault bound regardless of timing (PBFT/HotStuff). Liveness suspended during asynchrony/partition [FLP1985 S2; DLS1988 S3; HotStuff2019 S8].
- Nakamoto (C3) is more delay-tolerant but probabilistic finality needs longer confirmation under high delay; no safety guarantee during partition; longest-chain reorg on reconnect [Nakamoto2008 S7].
- Raft (FeinStatic baseline) is crash-fault only, permissioned, not Byzantine — preserved as reference only (D-024 PROVISIONAL); not a candidate.
- No consensus algorithm, validator-selection mechanism, or finality model may be implemented without an approved ADR and design review (AGENTS.md prohibited actions).

### Security assumptions
- C1/C5: `<1/3` (BFT) / `≥2/3 honest stake` (PoS) Byzantine voting power; quorum intersection; partial synchrony for liveness.
- C3: `<50% adversarial hashrate`; honest-majority-of-work; network-delay-dependent margin [PassEtAl2019 S27].
- No universal honest-majority model; thresholds are candidate-specific parameters (adversary model §3).
- FLP: deterministic consensus cannot guarantee liveness under full asynchrony with even one crash failure [S2, VERIFIED full text].
- CAP: during a partition, global linearizability and full availability cannot both hold [S6, VERIFIED].

### Financial implications
- Security budget ≈ cost to attack ≈ value of the scarce resource at stake (hashrate for PoW, stake for PoS); economically circular for a new network (RS-013 §7.14; D-015 OPEN RESEARCH).
- Validator rewards and fees are monetary-policy parameters, unresolved (D-015).

### Advantages and disadvantages
**C1:** + deterministic finality, linear per-view communication, well-understood safety; − permissioned BFT core needs stake layer, leader DoS target, stake centralisation, cross-layer coherence unresolved.
**C3:** + genuinely permissionless, long track record, self-consistent security argument, no stake concentration at genesis; − probabilistic finality, energy cost, pool concentration, <50% attacks feasible (selfish/piggyback mining), slow settlement.
**C5:** + deterministic checkpoint finality, formally composed fork choice + finality, accountable safety; − weak-subjectivity trust (P9 tension), stake concentration, custodial/LST centralisation, complex slashing, economic circularity.

### Unresolved questions
- Which consensus family best suits FST's requirements (permissionless, Byzantine, multi-transport, offline-capable, no privileged operator)?
- Deterministic or probabilistic finality? What settlement latency is acceptable? (RS-013 Q2)
- What Byzantine threshold is appropriate given FST's adversary model? (RS-013 Q3 — parameter, not constant)
- How does consensus adapt to multi-transport networks (mesh, radio, satellite, delay-tolerant)? (RS-013 Q7 — D-019)

### Research needed before implementation
- RS-013 and RS-002 remain OPEN RESEARCH; no consensus algorithm selected.
- Full-paper inspection of primary sources (HotStuff2019, Nakamoto2008, Gasper2020, CasperFFG2017) to verify protocol-specific claims (message complexity, finality type, fault bounds) — RS-013 §16.1 notes Kilo's inspection was metadata-only for several references.
- Formal verification targets (model checking, theorem proving) are protocol-specific and require implementation artifacts (RS-013 §10).

**Owner decision required:** Select consensus architecture family (or retain candidates as incomparable). **Not approved.** D-017 remains OPEN RESEARCH.

---

## Decision brief 4 — Finality requirements

### Available alternatives
1. **Deterministic finality** — once finalised, a block cannot be reverted under the protocol's assumptions (classical BFT, HotStuff, Casper FFG checkpoints). Suited to high-value settlement; no reversion under assumptions [RS-013 §3.11].
2. **Probabilistic finality** — reversion becomes exponentially unlikely with confirmation depth but is never impossible (Nakamoto longest-chain, Ouroboros Praos). Requires confirmation-depth risk management; economic finality (reversion cost exceeds transaction value) unresolved [RS-013 §3.11, §19].
3. **Hybrid** — probabilistic ordering with deterministic finality checkpoints (Casper FFG-style layering; RS-013 Candidate 4).

### Technical consequences
- Deterministic finality reduces transaction reorg/replay risk (relevant to transaction-model sensitivity, RS-013 §21, RS-015 §5.14).
- Probabilistic finality requires merchants to wait for confirmation depth; settlement risk must be managed economically.
- Finality model interacts with transaction model: deterministic finality reduces replay risk; account-based nonce ordering under probabilistic finality is more complex (RS-015 §5.14).
- Fork protection (D-036 APPROVED) requires unique network identity, replay protection, distinct currency identifiers — independent of finality model, but finality affects fork behaviour.

### Security assumptions
- Deterministic finality (BFT): safety under quorum intersection and fault bound; liveness under partial synchrony.
- Probabilistic finality (Nakamoto): honest-majority-of-hashrate; reversion probability decays exponentially with depth.
- No finality model is approved as a universal requirement (D-017 OPEN RESEARCH; scope-v0.1 §Decentralisation).

### Financial implications
- Finality model determines settlement risk for high-value transactions (scope-v0.1 §High-value settlement requirements).
- Economic finality (reversion cost > transaction value) for probabilistic finality is unresolved; depends on token value and security budget (D-015 OPEN RESEARCH).

### Advantages and disadvantages
**Deterministic:** + no reversion under assumptions, suits high-value settlement, simpler merchant settlement; − typically requires partial synchrony and a fixed/weighted validator set, leader DoS surface (BFT).
**Probabilistic:** + genuinely permissionless, delay-tolerant, no validator set to reconfigure; − reversion never impossible, slower settlement, confirmation-depth risk management.

### Unresolved questions
- What settlement latency is acceptable for FST's payment and settlement use cases?
- Deterministic or probabilistic finality (or hybrid)? (RS-013 Q2)
- How does finality interact with the transaction model and offline operation (D-018)?

### Research needed before implementation
- Finality-model research per candidate family (RS-013 §18, §21).
- Confirmation-depth risk analysis for probabilistic finality (not performed; no parameters).

**Owner decision required:** Finality model and acceptable settlement latency. **Not approved.** Part of D-017 OPEN RESEARCH.

---

## Decision brief 5 — Required adversary assumptions

### Available alternatives
The adversary model (adversary-model-v0.1.md) defines threat classes and parameterized thresholds without selecting an FST-wide honest-majority model:

| Parameter | Meaning | Candidate where applicable |
|-----------|---------|---------------------------|
| `f_bft` | Max Byzantine voting-power fraction (classically <1/3) | C1, C2 (BFT + stake) |
| `h_pow` | Min honest-hashrate fraction (<50% adversarial) | C3 (Nakamoto PoW) |
| `h_stake` | Min honest-stake fraction (≥2/3 for PoS finality) | C4, C5 (PoS) |
| `q_finality` | Fraction of voting power to finalize (≥2/3 supermajority) | C5 (Gasper) |

### Technical consequences
- Each candidate architecture must state its own assumptions; none is universal [RS-013 §3.2, §9.2].
- The Sybil layer must produce a voting-power distribution respecting the consensus fault bound; concentration breaks it (adversary model §3).
- FLP/CAP constraints: deterministic liveness impossible under full asynchrony; during partitions, consistency and availability cannot both hold [S2, S6].
- Threat classes covered: independent malicious node operators, Sybil identity operators, malicious validator coalitions, economically powerful participants, mining/staking pool operators, network-level attackers, malicious bootstrap/infrastructure providers, colluding merchants/submitters, compromised node software/supply chains (adversary model §5).

### Security assumptions
- Rational, bounded adversary; not omnipotent.
- Adaptive corruption (where candidate supports it).
- Network control short of total partition; eventual reconnection assumed for liveness.
- Cryptographic assumptions (signatures, hashes, VRFs/threshold signatures) unforgeable under candidate assumptions; D-023 cryptographic review required before primitive reuse.

### Financial implications
- Adversary resources (stake, hashrate) are economically valued; attack cost must exceed attack gain (economic security, RS-013 §7.10).
- Security budget circularity: token value funds security, security underpins token value (D-015 OPEN RESEARCH).

### Advantages and disadvantages
**Parameterized (no FST-wide constant):** + avoids assuming a universal honest-majority model; architecture-specific assumptions are explicit and reviewable; − does not give a single security bound for the network; owner must approve each parameter.
**Universal honest-majority:** + simple single bound; − contradicts the principle that thresholds are architecture-dependent; risks misapplying 3f+1 to permissionless settings [RS-013 §3.2].

### Unresolved questions
- What are FST's explicit adversary assumptions (number of Byzantine validators, network synchrony, adversary resources, adversary goals)? (adversary-model-v0.1.md §10 Q1)
- Which parameter values respect FST's principles and security goals? (Q2)
- How does Sybil resistance map economic cost to voting power without breaking the fault bound under concentration? (Q3)

### Research needed before implementation
- Adversary model (adversary-model-v0.1.md) remains OPEN RESEARCH; no threshold approved.
- Parameter values require owner decision per candidate architecture.

**Owner decision required:** Adversary assumptions and fault-bound parameters (per candidate). **Not approved.** D-017 OPEN RESEARCH.

---

## Decision brief 6 — Initial test-network security boundaries

### Scope
A minimal, internet-connected, permissionless test network (initial assumption: internet connectivity with partial synchrony; multi-transport mesh/radio/satellite/DTN is a future ambition, D-019 PROVISIONAL, RS-013 §19).

### Requirements-level boundaries (from `permissionless-test-network-evaluation.md` §4)

**Common to all candidates (TN-1..TN-12):**
- Unique network identifier and genesis separation; wrong-network rejection (D-036 APPROVED).
- Multiple independent bootstrap sources (not trust roots); peer exchange; signed node record validation.
- Node identity and authenticated TLS 1.3 sessions (established implementation, Milestones 2C–2D).
- Candidate validator participation without central approval (D-016 PROVISIONAL direction).
- Bounded, authenticated consensus communication; per-type payload limits; connection limits.
- Gossip-based transaction/block dissemination; syntactic validation before forwarding.
- Chain synchronization (genesis/snapshot/catch-up); state transfer reference (FeinStatic snapshots, D-032 PROVISIONAL).
- Fork detection and recovery; finality observation per candidate model.
- Network partition testing; independent validator operation; test instrumentation and security metrics (no secret logging).

**Candidate-specific (TN-C1, TN-C3, TN-C5):** stake bonding/unbonding, PoW difficulty adjustment, LMD-GHOST/Casper FFG checkpoints, leader DoS mitigation, slashing evidence, weak-subjectivity handling — all parameter values unresolved.

### Decisions that must be approved before implementation (TN §4.3)
- Transaction model (RS-015, D-015 OPEN RESEARCH)
- Sybil-resistance mechanism (RS-002, D-016 PROVISIONAL)
- Consensus algorithm (RS-001/RS-013, D-017 OPEN RESEARCH)
- Finality model (D-017 OPEN RESEARCH)
- Byzantine threshold / fault-bound parameters (adversary model §3 — parameter, not constant)
- Validator rewards, issuance, monetary policy (D-015 OPEN RESEARCH)
- Slashing rules, unbonding periods (RS-013 §6.8–6.9 OPEN RESEARCH)
- Weak-subjectivity checkpointing (if PoS) (RS-013 §6.11 OPEN RESEARCH)
- Chain-ID / transaction replay protection (D-036 mechanisms OPEN RESEARCH)

### Failure-injection scenarios (TN §4.4)
Byzantine validator behaviour, validator collusion, node failures, message loss/delay, network partition, Sybil attempts, recovery, long-offline rejoin (PoS candidates). These test assumptions; they do not approve any mechanism.

### Security boundaries (what is NOT approved)
- No Byzantine threshold is an FST constant.
- No consensus algorithm is selected.
- No Sybil-resistance mechanism is selected.
- No transaction model is selected.
- No finality model is selected.
- No genesis constants, token issuance, validator rewards, or transaction formats are finalized.
- No implementation of consensus, validators, transaction processing, mempool, wallets, or monetary policy is permitted without an approved ADR and design review (AGENTS.md prohibited actions).

### Research needed before implementation
- All items in Decision briefs 1–5 remain unresolved.
- Test instrumentation and security-metrics design (engineering, not research) — can proceed once the above decisions are made.
- Independent implementation conformance (RS-012, D-035 OPEN RESEARCH) — mainnet gating, not test-network gating.

**Owner decision required:** Approve the security boundaries and the pre-implementation decision list above. **Not approved.** All constituent decisions remain OPEN RESEARCH or PROVISIONAL.

---

## Summary of decision status

| Decision area | Related decision | Classification | Status |
|---------------|-----------------|----------------|--------|
| Transaction model | RS-015, D-015 | OPEN RESEARCH | Unresolved; owner decision required |
| Sybil-resistance mechanism | RS-002, D-016 | PROVISIONAL | Direction approved; mechanism unresolved |
| Consensus architecture | RS-001/RS-013, D-017 | OPEN RESEARCH | Unresolved; owner decision required |
| Finality model | D-017 | OPEN RESEARCH | Unresolved; owner decision required |
| Adversary assumptions / thresholds | adversary-model-v0.1.md | OPEN RESEARCH | Parameterized, not constants; owner decision required per candidate |
| Test-network security boundaries | This brief | OPEN RESEARCH | Requirements-level only; owner approval required before implementation |

**No decision in this brief is approved.** All classifications are preserved: D-015 OPEN RESEARCH, D-016 PROVISIONAL, D-017 OPEN RESEARCH, D-018 PROVISIONAL, D-024 PROVISIONAL, D-035 OPEN RESEARCH, D-036 APPROVED (fork protection requirements only; mechanisms OPEN RESEARCH).

Research findings are drawn from RS-001, RS-002, RS-013, RS-015 and the adversary model, with source-verification statuses recorded per RS-002 §8 / RS-013 §16. Owner decisions remain entirely with the project owner and are not implied by any content here.
