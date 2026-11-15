# FST Permissionless Test-Network Architecture Evaluation — v0.1

**Status:** DRAFT — research and architecture assessment, not an approved design.
**Classification:** OPEN RESEARCH (no consensus algorithm, Sybil-resistance mechanism, Byzantine threshold, finality model, or transaction model is approved or selected).
**Related decisions:** D-015 (OPEN RESEARCH), D-016 (PROVISIONAL), D-017 (OPEN RESEARCH), D-018 (PROVISIONAL), D-019 (PROVISIONAL), D-024 (PROVISIONAL), D-036 (APPROVED).
**Related research:** RS-001 (consensus), RS-002 (Sybil resistance), RS-013 (consensus–validator), RS-015 (transaction model).
**Related documents:** `docs/fst/security/adversary-model-v0.1.md` (FST adversary model), `docs/fst/architecture/permissionless-test-network-decision-brief.md` (owner decision briefs).

This document performs a deep technical evaluation of three consensus–Sybil compositions for a minimal permissionless FST test network, a transaction-model sensitivity analysis, a requirements-level test-network architecture, structured comparison matrices, and a conditional recommendation. It builds on RS-013 (consensus–validator research) and RS-002 (Sybil-resistance research), which remain OPEN RESEARCH. No architectural decision is approved, recommended for implementation, or silently selected here.

## 1. Candidate selection and shortlist justification

### 1.1 Starting point: the seven compositions

RS-013 Section 18 and RS-002 Section 6 define seven consensus–Sybil compositions (C1–C7):

| ID | Composition | Sybil mechanism | Agreement/ordering | Finality |
|----|-------------|----------------|-------------------|----------|
| C1 | HotStuff-style BFT + stake-based validator eligibility | Stake lockup | Leader-driven BFT, quorum certificates | Deterministic (<1/3 Byzantine) |
| C2 | Tendermint/CometBFT-style BFT + stake-weighted voting | Stake lockup | Round-based prevote/precommit | Deterministic (>2/3 voting power) |
| C3 | Nakamoto consensus + computational-work admission | Proof of Work (hashrate) | Longest chain, cumulative work | Probabilistic (<50% adversarial hashrate) |
| C4 | PoS longest-chain + stake-weighted proposer selection and fork choice | Stake lockup | VRF slot-leader election + chain selection | Probabilistic (settlement delay) |
| C5 | PoS + separate finality gadget (Gasper-style) | Stake lockup (Casper FFG) | LMD-GHOST fork choice + Casper FFG finality | Deterministic checkpoint (≥2/3 honest stake) |
| C6 | DAG-based dissemination + separate agreement/finality (Narwhal/Tusk-style) | External layer required (none intrinsic) | Narwhal DAG dissemination + Tusk async BFT or Narwhal-HotStuff | Deterministic (HotStuff) or randomized-probabilistic (Tusk) |
| C7 | Hybrid PoW/PoS (both hash power and stake required) | PoW + PoS | Protocol-specific (PoW finds blocks, PoS tickets finalize) | Protocol-specific |

### 1.2 Selection criteria

The shortlist was justified using explicit criteria, not by automatically selecting the examples named in the mission:

1. **Architectural family diversity:** candidates must come from meaningfully different families (leader-based BFT, Nakamoto-style, PoS with separate finality gadget) so the evaluation covers distinct security/operational trade-offs.
2. **Technical credibility:** each candidate must have published primary sources with verified or partially-verified inspection (per RS-013 §16 / RS-002 §8 verification records), so claims are traceable.
3. **Coherent security argument:** the composition must have a discernible mapping from Sybil-resistance output (voting-power distribution) to the consensus fault bound, per RS-013 §18.8 composition principle.
4. **Alignment with FST requirements:** permissionless participation (D-016 PROVISIONAL), Byzantine-resilience research direction (D-017 OPEN RESEARCH), no permanent privileged authority (P2), fork protection (D-036 APPROVED), multi-transport communication (D-019 PROVISIONAL), bounded offline operation (D-018 PROVISIONAL).
5. **Sufficient research depth:** enough existing source material to support a deep evaluation without conducting a new broad survey.
6. **No universal honest-majority assumption:** each candidate states its own threshold as a parameter, per the FST adversary model.

### 1.3 Shortlist: C1, C3, C5

| Selected | Composition | Rationale |
|----------|-------------|-----------|
| **C1** | HotStuff-style BFT + stake-based validator eligibility and weighted voting | Represents the leader-based BFT family. Deterministic finality, linear per-view communication (HotStuff), well-understood safety. Directly matches the mission's HotStuff-style BFT + stake example. Stake layer provides Sybil resistance; HotStuff provides agreement. Coherence is conditional (stake distribution must respect `<1/3 Byzantine voting power`). |
| **C3** | Nakamoto consensus + computational-work admission (Proof of Work) | Represents the Nakamoto family. Genuinely permissionless, long deployment track record (Bitcoin), PoW is both Sybil mechanism and block-author selector (self-consistent). Probabilistic finality, honest-majority-of-work. Directly matches the mission's Nakamoto-style PoW example. |
| **C5** | PoS longest-chain + separate finality gadget (Gasper-style: LMD-GHOST + Casper FFG) | Represents the PoS checkpoint-finality family. Separates fork choice (LMD-GHOST) from finality (Casper FFG), demonstrating component composition. Deterministic checkpoint finality qualified by weak subjectivity and slashing. Directly matches the mission's PoS longest-chain with separate finality gadget example. |

### 1.4 Considered alternatives (not shortlisted, with justification)

- **C2 (Tendermint/CometBFT-style BFT + stake):** Very close to C1 (both are leader-based BFT + stake). Not selected as a separate deep-evaluation candidate because it adds little architectural distinction beyond voting-power threshold (>2/3 vs <1/3) and proposer rotation. Recorded as a close variant of C1; C1 is the representative due to linear communication complexity (HotStuff) and documented leader-DoS surface.
- **C4 (PoS longest-chain, Ouroboros Praos-style, no separate finality gadget):** Probabilistic finality PoS. Considered but not selected because C5 (PoS + finality gadget) covers the PoS family with a more explicit fork-choice/finality separation, which is more decision-relevant for a financial network evaluating finality models. Praos is discussed in RS-013 §5.4 and §18.4.
- **C6 (DAG-based, Narwhal/Tusk-style):** Evaluated in the independent subagent report (`docs/fst/research/candidate-c6-evaluation.md`) and the C5-vs-C6 comparative assessment (`docs/fst/research/candidate-c5-c6-comparative-assessment.md`). C6 is **not categorically stronger** than C5 for FST. C6 has a structural advantage on dissemination-under-intermittency (relevant to D-019 multi-transport), but no intrinsic Sybil resistance, no unified security proof for the composition, and less deployment history. C5 has a more developed documented offline/recovery model (checkpoints, weak subjectivity, inactivity leak), though weak-subjectivity trust conflicts with P9. Neither satisfies D-018 offline operation without a separate bounded-offline-payment architecture. C6 is retained as a considered alternative, not shortlisted for deep evaluation, because research does not demonstrate a stronger case for FST's immediate test-network requirements (internet-connected initial network, per RS-013 §19).
- **C7 (Hybrid PoW/PoS):** Higher attack cost (two resources) but added architectural complexity, latency, and split security budget. Less deployment history. Not shortlisted; recorded in RS-013 §18.7.

**No candidate is selected, approved, or recommended for implementation.** All remain OPEN RESEARCH.

## 2. Deep candidate evaluation

Each candidate is evaluated across five dimensions: consensus/Sybil integration, security, operational architecture, performance/network behaviour, and financial/economic implications. No numeric performance estimates or economic parameters are invented. Theoretical properties, published measurements, and engineering assumptions are separated.

### 2.1 Candidate C1 — HotStuff-style BFT + stake-based validator eligibility

**Source:** RS-013 §5.2 (HotStuff), §18.1 (Candidate C1), §8 (network), §9 (security), §20 (economics), §22 (source records); deep-evaluation report by subagent.

#### 2.1.1 Consensus and Sybil integration

- **Sybil resistance component:** Stake lockup (Proof of Stake). Validator eligibility = bonded native-token stake above a threshold [Gasper2020 S15; RS-013 §18.1]. Identity is free to create, but voting power is proportional to staked tokens; creating many identities does not multiply voting power without more stake [Douceur2002 S5; RS-002 §2].
- **Agreement and ordering component:** HotStuff view-based BFT with quorum certificates (aggregated signatures); leader-driven; deterministic finality under the fault bound under partial synchrony [HotStuff2019 S8].
- **Voting power / proposer probability:** Stake-weighted voting power; proposer selected round-robin or stake-weighted from the active committee [HotStuff2002 S8; DiemBFT2020 S16]. Note: HotStuff2019 itself assumes a fixed equal-weight replica set; stake-weighting is an external composition layer asserted via DiemBFT2020 (PARTIALLY VERIFIED, metadata only).
- **Identity → scarce-resource mapping:** Bonded stake (economic capital locked as collateral).
- **Does Sybil resistance support the consensus membership model?** Conditionally. Requires the stake layer to map economic cost to voting power, and HotStuff's `<1/3 Byzantine voting power` (3f+1) bound to hold over the stake-weighted set. Coherence depends on stake distribution respecting the fault bound; concentration breaks it [Gasper2020 S15; IrresbergerYang2023 S39, PARTIALLY VERIFIED metadata only].
- **Coherence of security arguments:** Conditional, not established. HotStuff assumes a known fixed replica set and partial synchrony; stake-based eligibility introduces unknown participant count, economic concentration, unbonding/exit dynamics, and weak subjectivity — none part of HotStuff's model [CasperFFG2017 S14; Gasper2020 S15]. Cross-layer coherence is OPEN RESEARCH [RS-013 §18.1, §18.8].

#### 2.1.2 Security

- **Safety conditions:** `<1/3 Byzantine voting power` (3f+1 bound); quorums of 2f+1; quorum intersection (any two quorums share at least one honest participant) [LamportShostakPease1982 S4; CastroLiskov1999 S1]. Quorum intersection textual basis PARTIALLY VERIFIED (not stated in inspected PBFT text). Safety preserved always under fault bound; liveness after GST under partial synchrony [HotStuff2019 S8; DLS1988 S3].
- **Liveness conditions:** Partial synchrony, quorum connectivity, leader rotation via view changes [DLS1988 S3; CastroLiskov1999 S1]. Under full asynchrony, deterministic liveness is impossible (FLP) [FischerLynchPaterson1985 S2, VERIFIED full text].
- **Byzantine assumptions:** `<1/3 Byzantine voting power`, arbitrary malicious behaviour tolerated up to that bound. FST adversary model unresolved (D-017 OPEN RESEARCH) [RS-013 §9.1].
- **Quorum intersection:** Established BFT safety mechanism; for stake-weighted quorums, intersection must be defined over voting power (weighted intersection), not node count — not established in inspected sources.
- **Adaptive corruption:** Not addressed for C1 in RS-013 §18.1. Ouroboros Praos tolerates adaptive corruption with honest-majority-of-stake, but that is C4, not C1. UNRESOLVED for C1.
- **Equivocation:** Detectable in BFT (double-signing/voting on conflicting views). Slashing addresses it in PoS finality gadgets; HotStuff itself does not specify slashing [CasperFFG2017 S14; Gasper2020 S15; RS-013 §6.9].
- **Collusion and bribery:** Bribery to double-sign or withhold; slashing raises cost-of-corruption to ≥1/3 of total stake [STAKESURE2024 S34; a16zSlashing2023 S35]. Token toxicity alone insufficient [Bribers2025 S37]. Collusion resistance requires economic cost and geographic/infrastructure diversity [RS-013 §6.15].
- **Censorship:** Validator collusion can censor [RS-013 §9.9]. Leader-based BFT concentrates ordering power in the current leader, creating a per-view censorship surface. No mitigation quantified for C1.
- **Long-range attacks:** Relevant to stake. Adversary who previously controlled stake rewrites history from an old checkpoint using withdrawn/sold keys [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14; GažiEtAl2019]. Mitigations: weak-subjectivity checkpoints, unbonding > weak-subjectivity period, key evolution, Bitcoin checkpointing (Babylon), social consensus. C1 checkpoint schedule and weak-subjectivity period UNRESOLVED.
- **Resource-exhaustion attacks (leader DoS):** Leader-based BFT makes the current leader a DoS target [Sonnino et al. 2024 S16, VERIFIED abstract+paper: leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size]. Leader rotation/view changes mitigate but do not eliminate; view-change complexity under DoS is open research [HotStuff2019 S8 qualification].
- **Recovery after network disruption:** View changes, leader rotation, state sync enable recovery [RS-013 §8.7]. Recovery time depends on validator count and state size. During partitions, quorums may be unreachable; liveness sacrificed until reconnection; safety preserved under fault bound [DLS1988 S3; CastroLiskov1999 S1].

#### 2.1.3 Operational architecture

- **Validator entry and exit:** Permissionless entry requires on-chain registration or implicit entry [RS-013 §6.7]. Exit requires unbonding periods to prevent stake withdrawal before slashing can be applied; unbonding delay must exceed slashing detection window [CasperFFG2017 S14; Gasper2020 S15; RS-013 §6.7–6.8]. Long unbonding reduces capital efficiency; short periods weaken security (design trade-off, no values set).
- **Registration and eligibility:** Eligibility = bonded stake above threshold [Gasper2020 S15]. Minimum stake, hardware requirements, registration mechanism are eligibility conditions distinct from selection [RS-013 §2.2]. Exact threshold and hardware specs UNRESOLVED (D-015 OPEN RESEARCH; no parameters chosen per RS-013 §20).
- **Proposer/leader selection:** Proposer selected round-robin or stake-weighted from active committee [HotStuff2019 S8; DiemBFT2020 S16]. HotStuff is leader-driven; leader-centric communication [RS-013 §8.4].
- **Committee formation:** Not explicitly detailed for C1. Selection options: random sampling (sortition), round-robin, stake-weighted [RS-013 §2.3]. Random sampling requires a randomness source (VRF/beacon chain) whose security assumptions require separate research. Committee = active validator set; formation mechanism UNRESOLVED.
- **Validator rotation:** Rotation limits the window in which a corrupted validator set can cause harm [RS-013 §6.10]. Frequency: design trade-off, no values set. Epoch-based reconfiguration (DiemBFT) referenced as helpful for entry/exit [DiemBFT2020 S16, PARTIALLY VERIFIED].
- **State synchronization:** New/rejoining validators must synchronize state; snapshots and state proofs reduce sync time [RS-013 §8.6]. FeinStatic snapshot-transfer patterns are reference material (D-032 PROVISIONAL) but cannot be modified during Milestone 1. Exact state-sync protocol for C1: UNRESOLVED.
- **Recovery and checkpoint assumptions:** Checkpoints/weak subjectivity needed for PoS long-range defence [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14]. Offline > weak-subjectivity period requires external trust to rejoin [S40]. C1 checkpoint schedule UNRESOLVED.
- **Slashing/punishment:** Slashing penalises double-signing/offline time; requires evidence collection and slashing mechanism [CasperFFG2017 S14; Gasper2020 S15]. Slashing centralisation risk if a small group decides misbehaviour — governance design question [RS-013 §6.9, §7.7]. Exact slashing rules for C1: UNRESOLVED.
- **Independent verification:** Block/header verification cheap; chain selection from genesis requires weak subjectivity in PoS — new/returning nodes cannot fully independently determine canonical chain without external checkpoint/trust source [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14]. Principles P9/P4 apply. Independent verification of C1's stake-weighted quorum model: UNRESOLVED.

#### 2.1.4 Performance and network behaviour

- **Communication complexity:** HotStuff O(n) communication per view via quorum certificates [HotStuff2019 S8, VERIFIED abstract+full text QC/complexity]. Counts QC communication per view; O(n) assumes QC aggregation; without aggregation raw message count differs. Worst case O(n²) under cascading leader failures (qualification, not a deployed measurement). PBFT comparison: O(n²) per view [CastroLiskov1999 S1, PARTIALLY VERIFIED]. These are protocol-paper asymptotic results, not deployment measurements [RS-013 §8.1]. No numeric performance estimates invented.
- **Bandwidth:** Validators exchange votes on every block. High validator counts increase bandwidth linearly per view (HotStuff) or quadratically (PBFT). Design characteristics, not deployment measurements; no fixed per-transaction bandwidth figure [RS-013 §8.2].
- **Latency sensitivity:** BFT finality requires multiple communication rounds; latency scales with round count and network delay. Exact round counts and latency are protocol- and deployment-specific; no fixed latency ranges [RS-013 §8.3].
- **Message-size and propagation:** Aggregated quorum certificates (threshold/BLS signatures) keep per-view messages linear [HotStuff2019 S8]. Propagation requirements and message-size limits for C1: UNRESOLVED (no implementation spec).
- **Committee-size sensitivity:** Larger validator sets increase per-view bandwidth linearly (HotStuff) but do not change asymptotic complexity. Leader DoS cost independent of committee size [Sonnino et al. 2024 S16]. Concrete committee-size sensitivity for C1: UNRESOLVED.
- **Operation under packet loss:** Packet loss increases timeouts and retransmissions, degrading liveness; degree protocol- and implementation-specific [RS-013 §8.9]. No quantified tolerance for C1.
- **Operation under prolonged partitions:** During partitions quorums may be unreachable; liveness sacrificed until reconnection; safety preserved under fault bound [DLS1988 S3; CastroLiskov1999 S1]. CAP: during a partition, unrestricted local spending and global double-spend prevention cannot both be guaranteed [GilbertLynch2002 S6; RS-013 §8.10]. FLP: no deterministic consensus with guaranteed liveness under pure asynchrony [FLP1985 S2]. C1 offers no safety during partition beyond the fault bound; liveness resumes after connectivity returns.
- **Recovery after connectivity returns:** View changes, leader rotation, state sync enable recovery; recovery time depends on validator count and state size [RS-013 §8.7–8.8]. No quantified recovery time for C1.

#### 2.1.5 Financial and economic implications

All economic claims are qualitative design concerns per RS-013 §20: D-015 OPEN RESEARCH; no issuance, supply, inflation, validator rewards, or final economic parameters chosen; no attack cost, reward rate, issuance schedule, or supply figure asserted for FST.

- **Security-budget dependencies:** Economic security ≈ cost to attack ≈ value of stake at stake; requires token value high enough to make attacks uneconomic; protocol-specific and economically circular for a new network [CasperFFG2017 S14; Gasper2020 S15; RS-013 §7.5, §7.14].
- **Stake concentration:** Stake concentration leads to centralisation and increases collusion risk [RS-013 §6.13]. Delegation to pools amplifies concentration [RS-013 §6.6, §7.8]. IrresbergerYang2023 [S39, PARTIALLY VERIFIED metadata only] studies PoS coin concentration; finding is study-specific, not a universal law [RS-013 §6.14].
- **Reward and fee dependencies:** Rewards incentivise participation but increase inflation; reward design affects centralisation (economies of scale may favour large validators) [RS-013 §7.2]. Fees pay for block space; fee markets may centralise block production through MEV — observed deployment phenomenon in Ethereum [DaianEtAl2020 S22, PARTIALLY VERIFIED metadata only, Ethereum-specific, not generalized].
- **Participation costs:** Token acquisition cost (market price × minimum stake), validator hardware, uptime requirements, slashing risk, unbonding-period illiquidity, opportunity cost of locked capital [RS-013 §3.2]. No minimum stake or hardware figure set for FST (UNRESOLVED).
- **Entry barriers:** Economic barrier (token acquisition) plus operational barrier (hardware/uptime). Delegation lowers entry barriers but concentrates power [RS-013 §6.6]. No FST entry parameters defined.
- **Wealth concentration:** Token wealth may concentrate through rewards (rich-get-richer) — economic hypothesis, not observed universal law [RS-013 §6.14, §7.2].
- **Extractable value and transaction ordering:** Leader/proposer controls transaction ordering → MEV extraction and frontrunning risk [DaianEtAl2020 S22; RS-013 §7.3, §9.9]. C1 leader selects/orders transactions per view, creating an ordering-control surface. MEV mitigation for C1: UNRESOLVED.
- **Economic circularity for a new network:** Stake-based Sybil mechanisms require FST to have market value to fund security — economic circularity [RS-013 §19].

### 2.2 Candidate C3 — Nakamoto consensus + computational-work admission (Proof of Work)

**Source:** RS-013 §5.3 (Nakamoto-style PoW), §18.3 (Candidate C3), §8, §9, §20, §22; independent subagent extraction summary.

#### 2.2.1 Consensus and Sybil integration

- **Sybil resistance component:** Proof of Work (hashrate cost / computational cost per block/identity) [Nakamoto2008 S7]. Node identity is free, but voting power is pegged to actual computational work performed; splitting hashrate across many identities yields no more influence than one node — "one-CPU-one-vote, not one-node-one-vote" [Nakamoto2008 S7; Douceur2002 S5].
- **Agreement and ordering component:** Longest-chain rule with cumulative work fork choice; probabilistic finality [Nakamoto2008 S7].
- **Voting power / proposer probability:** Hashrate-weighted; the puzzle solver (miner who finds a valid proof-of-work hash) proposes the next block [Nakamoto2008 S7].
- **Identity → scarce-resource mapping:** Hash rate / computation (electricity + specialized hardware).
- **Does Sybil resistance support the consensus membership model?** Yes — PoW is both Sybil-resistance mechanism and block-author selector; the composition is self-consistent [RS-013 §18.3]. Honest-majority-of-work assumption underlies both Sybil resistance and agreement.
- **Coherence of security arguments:** Yes, conditionally — PoW is both Sybil mechanism and block-author selector; honest-majority-of-work is self-consistent [Nakamoto2008 S7; PassEtAl2019 S27]. Well-understood deployment track record (Bitcoin). Network-delay-dependent margin [PassEtAl2019 S27].

#### 2.2.2 Security

- **Safety conditions:** `<50% adversarial hashrate` (honest-majority-of-work); network-delay-dependent margin [Nakamoto2008 S7; PassEtAl2019 S27]. Probability of attacker catch-up drops exponentially with confirmation depth k. Safety is probabilistic, never absolute.
- **Liveness conditions:** Honest-majority hash rate; progress continues as long as honest miners collectively outpace any withholding/forking adversary [Nakamoto2008 S7; RS-013 §9.4].
- **Byzantine assumptions:** `<50% adversarial hashrate` rather than `<1/3 Byzantine replicas`. No fixed replica set or 3f+1 bound; the 3f+1 bound cannot be applied directly in a permissionless setting with unknown participant count [RS-013 §3.2]. FST adversary model unresolved (D-017 OPEN RESEARCH).
- **Quorum intersection:** N/A — Nakamoto consensus has no quorum model, no quorum-intersection argument, no fixed voting set. Safety arises probabilistically from cumulative-work chain selection, not deterministic quorum intersection [RS-013 §3.2, §12].
- **Adaptive corruption:** Not formally addressed by Nakamoto2008 for PoW. "Corruption" maps to acquiring/redirecting hashrate rather than corrupting identified validator keys. Whether an adversary can adaptively acquire >50% hashrate is unresolved. UNRESOLVED for C3.
- **Equivocation:** PoW miners can mine on multiple chains (split hashrate across forks), but unlike PoS nothing-at-stake, splitting hashrate dilutes the miner's own power on each chain. No slashing; cost is opportunity cost (split hashrate) [Nakamoto2008 S7; RS-002 §6.12].
- **Collusion and bribery:** Mining pools can collude; pool operators direct hashrate but do not own it (miners can switch pools). Bribery economics: attack payoff must exceed cost plus lost future mining revenue [HanEtAl2021 S31; MITDCI2020 S30].
- **Censorship:** Pool-level censorship: a pool or coalition controlling >50% (or <50% via selfish-mining variants) can exclude transactions. Censorship resistance depends on pool diversity and miners' ability to switch pools [RS-002 §3.1, §4; RS-013 §7.11].
- **Long-range attacks:** N/A to PoW. Historical work is embedded in block headers as cumulative hashes; recomputing an alternative deep history requires re-performing cumulative computational work. No withdrawn-stake/old-key vector [Nakamoto2008 S7; RS-002 §4 (PoS-scoped only); RS-013 §6.11 (PoS-scoped)].
- **Resource-exhaustion attacks:** Hashrate rental attacks: adversary can rent hashrate (e.g., NiceHash) to mount a 51% attack on smaller coins at marginal rental cost. Reduces effective attack cost for smaller networks [MITDCI2020 S30].
- **Selfish mining and piggyback mining (<50% attacks):** Selfish mining and piggyback mining enable profitable attacks below 50% hashrate by withholding blocks and strategically releasing them to orphan honest blocks [MajorityIsNotNeeded2023 S32; HanEtAl2021 S31; EyalSirer2014]. The honest-50% threshold is not a strict safety boundary in practice.
- **Recovery after network disruption:** After a partition heals, nodes adopt the chain with most accumulated work (longest chain), causing reorgs of the minority chain. Safety not guaranteed during partition; liveness resumes on reconnection [Nakamoto2008 S7; RS-002 §3.1; RS-013 §8.5, §13.19].

#### 2.2.3 Operational architecture

- **Validator entry and exit:** Open entry/exit; hashrate can enter and leave freely. No registration, bonding, unbonding, slashing, or permission required [Nakamoto2008 S7; RS-013 §18.3; RS-002 §3.1].
- **Registration and eligibility:** Any participant with hash rate can mine — implicit entry. No explicit registration or eligibility gate beyond possessing mining hardware and ability to perform hashes [Nakamoto2008 S7; RS-013 §5.3].
- **Proposer/leader selection:** The puzzle solver proposes the next block. No separate leader election or rotation [Nakamoto2008 S7; RS-013 §18.3].
- **Committee formation:** N/A — no committee; every miner is a potential proposer [RS-013 §18.3].
- **Validator rotation:** N/A — no validator set to rotate; miners enter/exit freely based on hashrate [RS-013 §6.10, §18.3].
- **State synchronization:** Full nodes download and validate blocks from genesis (or snapshot); light clients verify headers and proofs. State synchronization mechanics (snapshot format, streaming protocol, state commitment scheme) are OPEN RESEARCH (RS-009, D-032 PROVISIONAL) [RS-013 §8.6; node-roles.md §9].
- **Recovery and checkpoint assumptions:** Probabilistic finality with k-block confirmation depth. No checkpoints in Nakamoto2008; finality is exponential-decay probabilistic, never absolute [Nakamoto2008 S7; RS-013 §3.11].
- **Slashing/punishment:** None by protocol design; misbehaviour deterred by economic cost (opportunity cost of split hashrate, lost future revenue), not protocol-level penalties [Nakamoto2008 S7; RS-013 §6.9 (PoS-scoped); RS-002 §3.1].
- **Independent verification:** Yes — any node can verify a block's PoW by re-hashing the header; header verification is cheap. Full verification requires running a full node [Nakamoto2008 S7; RS-002 §3.1; RS-013 Principle P9].

#### 2.2.4 Performance and network behaviour

- **Communication complexity:** O(n) block propagation (block broadcast/gossip to all nodes). No all-to-all voting or O(n²) consensus messaging. Asymptotic protocol-paper result, not deployment-measured [RS-013 §8.1, §12; Nakamoto2008 S7].
- **Bandwidth:** Not quantified in sources. Block size and block interval determine bandwidth; no fixed per-transaction bandwidth figure assigned [RS-013 §8.2]. No number invented.
- **Latency sensitivity:** Block interval and confirmation depth determine settlement latency. Probabilistic finality requires longer confirmation under high delay [Nakamoto2008 S7; PassEtAl2019 S27; RS-013 §8.3, §13.19]. No fixed latency ranges for FST (D-017 OPEN RESEARCH).
- **Message-size and propagation:** Blocks gossiped between peers; transactions propagated before block inclusion. Message-size limits are an implementation concern (FeinStatic baseline: 1 MiB frame cap, per-type payload limits). Propagation scope (gossip vs. direct to proposers) unresolved [RS-015 §5.13; RS-013 §2.5].
- **Committee-size sensitivity:** N/A — no committee [RS-013 §12].
- **Operation under packet loss:** Not specifically addressed by Nakamoto2008 or RS-013/RS-002 for PoW. Packet loss increases timeouts and retransmissions, degrading liveness; degree protocol- and implementation-specific [RS-013 §8.9]. UNRESOLVED.
- **Operation under prolonged partitions:** Under partition, each side mines its own tip. On reconnection, the chain with most accumulated work wins, reorging the minority. No safety guarantee during partition; temporary forks resolved by work [Nakamoto2008 S7; RS-002 §3.1].
- **Recovery after connectivity returns:** Nodes resync by downloading missing blocks and adopting the most-work chain; reorgs of the minority chain occur automatically. Snapshot and catch-up sync patterns from FeinStatic are reference material (D-032 PROVISIONAL) [RS-013 §8.6–8.7; node-roles.md §9].

#### 2.2.5 Financial and economic implications

All economic claims are qualitative design concerns per RS-013 §20; D-015 OPEN RESEARCH; no numbers invented.

- **Security-budget dependencies:** PoW security budget ≈ cost of hardware + energy expended on hashing; attack cost scales with total hashrate value, which scales with token value (miner revenue = block subsidy + fees, denominated in the token). Token value funds security; security underpins token value — circular for a new network [Nakamoto2008 S7; RS-002 §3.1; RS-013 §7.4, §7.14, §19].
- **Mining concentration:** Mining pools concentrate hashrate; historically 4–5 pools controlled >51% of Bitcoin hashrate on many days (deployment observation, not independently verified by Kilo). Pool operators direct hashrate but do not own it — miners can switch pools. Pool centralisation is the dominant PoW centralisation vector [RS-002 §3.1; MITDCI2020 S30].
- **Reward and fee dependencies:** Miner revenue = block subsidy + transaction fees. Issuance schedule determines miner revenue and thus attack cost. Halvings reduce security budget unless price or fees rise [Nakamoto2008 S7; RS-002 §3.1; RS-013 §7.2, §7.13].
- **Participation costs:** Mining hardware (ASIC/GPU), electricity, internet connection, mining software; optionally pool membership. CAPEX for hardware; ongoing electricity cost; cooling/facility; pool fees [RS-002 §3.1; MITDCI2020 S30]. No specific costs for FST.
- **Entry barriers:** High fixed cost (hardware CAPEX + electricity) creates a natural barrier but also concentrates where electricity is cheap. No permission, no lock-up, no unbonding [RS-002 §3.1; RS-013 §6.7].
- **Wealth concentration:** Mining rewards accrue to hashrate owners; economies of scale may favour large miners/pools. Economic hypothesis/design concern, not a protocol guarantee [RS-002 §3.1; RS-013 §6.13]. IrresbergerYang2023 [S39] is PoS-specific and not applicable to C3.
- **Extractable value and transaction ordering:** MEV from transaction ordering documented as Ethereum-specific observed deployment phenomenon [DaianEtAl2020 S22, PARTIALLY VERIFIED metadata only]. Not generalizable to PoW C3; no PoW-specific MEV analysis in baseline sources. Miner/proposer controls transaction ordering within a block, creating MEV opportunity [RS-013 §7.12, §13.19].
- **Security budget independence:** PoW requires hardware/energy expenditure independent of FST value initially, but security budget still scales with token value [RS-013 §19].

### 2.3 Candidate C5 — PoS + separate finality gadget (Gasper-style)

**Source:** RS-013 §5.5 (Gasper), §18.5 (Candidate C5), §8, §9, §20, §22; deep-evaluation report `docs/fst/research/candidate-c5-evaluation.md`.

#### 2.3.1 Consensus and Sybil integration

- **Sybil resistance component:** Stake lockup via Casper FFG. Validator eligibility requires depositing (bonding) native tokens as collateral [Gasper2020 S15]. Identity is free to create, but voting power is proportional to staked tokens; creating additional identities does not multiply voting power without more stake [Gasper2020 S15; Douceur2002 S5].
- **Agreement and ordering component:** Split across two composed mechanisms [Gasper2020 S15]:
  - **LMD-GHOST fork choice** — selects the canonical chain among competing tips by weighing attestations with the "latest message" per validator (weighted by stake). Performs ordering but does not finalize.
  - **Casper FFG finality gadget** — validators vote on pairs (source checkpoint, target checkpoint); a checkpoint is "justified" when ≥2/3 of total stake attests, and "finalized" when a justified checkpoint is directly followed by another justified checkpoint [CasperFFG2017 S14; Gasper2020 S15]. Performs agreement/finality.
- This is a **composition** (fork choice separate from finality), per RS-013 §2.10 composition principle [RS-013 §18.5].
- **Voting power / proposer probability:** Voting power proportional to staked (bonded) tokens [Gasper2020 S15]. Proposer assigned per slot from the validator set (stake-weighted per slot) [Gasper2020 S15]. Finality threshold: ≥2/3 of total stake supermajority [CasperFFG2017 S14; Gasper2020 S15]. Exact per-slot assignment algorithm and parameters not fully established from inspected material (PARTIALLY VERIFIED).
- **Identity → scarce-resource mapping:** Validator identity = cryptographic key pair (free); scarce resource = native FST token stake locked as bond. Mapping: identity → bonded stake → voting power weight ∝ stake [Gasper2020 S15]. Unbonding releases stake after a delay; delay must exceed weak-subjectivity period / slashing detection window [BabylonBlog2023 S41; EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14].
- **Does Sybil resistance support the consensus membership model?** Conditionally. Casper FFG's accountable-safety and liveness proofs assume the validator set's stake distribution respects the ≥2/3 honest-stake assumption. Stake lockup does not by itself guarantee the voting-power distribution respects the fault bound: stake concentration (whale concentration, custodial staking, liquid-staking derivatives) can concentrate voting power and break ≥2/3 [a16zSlashing2023 S35; IrresbergerYang2023 S39, PARTIALLY VERIFIED metadata only].
- **Coherence of security arguments:** Coherent internally (fork choice + finality gadget formally combined; accountable safety theorem: if two conflicting checkpoints are finalized, ≥1/3 of total stake is slashable) [CasperFFG2017 S14; Gasper2020 S15]. Not coherent unconditionally: depends on (a) honest ≥2/3 stake, (b) slashing detection and enforcement, (c) weak-subjectivity checkpoints for new/returning nodes, (d) unbonding > weak-subjectivity period [CasperFFG2017 S14; Gasper2020 S15; EthereumWeakSubjectivity2024 S40; BabylonBlog2023 S41]. Finality is deterministic checkpoint finality qualified by Byzantine-stake and weak-subjectivity assumptions [Gasper2020 S15; EthereumWeakSubjectivity2024 S40].

#### 2.3.2 Security

- **Safety conditions:** Accountable safety: if two conflicting checkpoints are finalized, at least 1/3 of total stake is slashable (equivocated) [CasperFFG2017 S14; Gasper2020 S15]. Deterministic checkpoint finality: a finalized checkpoint cannot be reverted under the protocol's Byzantine-stake and slashing assumptions [Gasper2020 S15]. Finality is qualified, not unconditional irreversibility [CasperFFG2017 S14; EthereumWeakSubjectivity2024 S40]. Assumptions: ≥2/3 honest stake; slashing enforced; quorum intersection over stake-weighted votes.
- **Liveness conditions:** Plausible liveness under ≥2/3 honest stake and partial synchrony [CasperFFG2017 S14; Gasper2020 S15]. Inactivity leak can recover liveness if offline validators' weight drops below 1/3 [Gasper2020 S15; EthereumWeakSubjectivity2024 S40].
- **Byzantine assumptions:** ≥2/3 honest stake for finality; accountable safety (≥1/3 slashable on conflicting finality) [CasperFFG2017 S14; Gasper2020 S15]. Partial synchrony for liveness. FST adversary model unresolved (D-017 OPEN RESEARCH).
- **Quorum intersection:** Stake-weighted quorum intersection; ≥2/3 supermajority ensures any two quorums intersect in sufficient honest stake [CasperFFG2017 S14; Gasper2020 S15].
- **Adaptive corruption:** Not established for Gasper from inspected sources [RS-013 §18.5]. UNRESOLVED.
- **Equivocation:** Slashing conditions (S1/S2) penalize double-signing/checkpoint equivocation [CasperFFG2017 S14; Gasper2020 S15]. Detectable and slashable.
- **Collusion and bribery:** Bribery to double-sign or withhold; slashing raises cost-of-corruption ≥1/3 stake [STAKESURE2024 S34; a16zSlashing2023 S35; Bribers2025 S37]. Token toxicity alone insufficient [Bribers2025 S37].
- **Censorship:** Validator collusion can censor [RS-013 §9.9]. Per-slot proposer controls ordering within a slot, creating a censorship surface. Inclusion lists / proposer-separator designs are mitigations, not protocol guarantees [RS-013 §7.11].
- **Long-range attacks:** Relevant to PoS. Adversary who previously controlled stake rewrites history from an old checkpoint using withdrawn/sold keys [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14; GažiEtAl2019]. Mitigations: weak-subjectivity checkpoints, unbonding > weak-subjectivity period, key evolution, Bitcoin checkpointing (Babylon), social consensus [S40; S41; S14].
- **Resource-exhaustion attacks:** Not specifically analyzed for C5 in baseline. UNRESOLVED.
- **Recovery after network disruption:** During a partition lacking ≥2/3 honest-stake quorum, finality halts; safety preserved; liveness suspended until reconnection [DLS1988 S3; Gasper2020 S15; RS-013 §8.5]. Nodes offline longer than the weak-subjectivity period cannot independently rejoin; require external trusted checkpoint [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14].

#### 2.3.3 Operational architecture

- **Validator entry and exit:** Permissionless staking; entry by bonding stake; exit via unbonding period. Unbonding > weak-subjectivity period required to prevent long-range attacks after exit [BabylonBlog2023 S41; EthereumWeakSubjectivity2024 S40; RS-013 §18.5].
- **Registration and eligibility:** Eligibility = bonded stake above threshold [Gasper2020 S15]. Minimum stake, hardware, registration mechanism are eligibility conditions [RS-013 §2.2]. Exact values UNRESOLVED (D-015 OPEN RESEARCH).
- **Proposer/leader selection:** Proposer assigned per slot from validator set (stake-weighted) [Gasper2020 S15]. Per-slot proposer is a leader target for DoS [Sonnino et al. 2024 S16, VERIFIED for HotStuff-like leaders; PARTIALLY VERIFIED as applied to C5].
- **Committee formation:** Committee-based selection from staked validators [RS-013 §5.5]. Random sampling requires a randomness source (VRF/beacon chain) whose assumptions require separate research [RS-013 §2.3].
- **Validator rotation:** Validator set changes per epoch; rotation limits the window in which a corrupted validator set can cause harm [RS-013 §6.10]. Frequency: design trade-off, no values set.
- **State synchronization:** New/rejoining validators synchronize state; snapshots and state proofs reduce sync time [RS-013 §8.6]. FeinStatic snapshot-transfer patterns reference (D-032 PROVISIONAL). Exact protocol UNRESOLVED.
- **Recovery and checkpoint assumptions:** Checkpoints and weak subjectivity provide defence against long-range attacks [EthereumWeakSubjectivity2024 S40]. Unbonding > weak-subjectivity period [S41; S40]. Checkpoint schedule and weak-subjectivity period for FST: UNRESOLVED.
- **Slashing/punishment:** Slashing penalises double-signing/offline time; requires evidence collection and slashing mechanism [CasperFFG2017 S14; Gasper2020 S15]. Slashing centralisation risk if a small group decides misbehaviour — governance design question [RS-013 §6.9, §7.7]. Exact slashing rules for C5: UNRESOLVED.
- **Independent verification:** Block/header verification cheap; chain selection from genesis requires weak subjectivity in PoS — new/returning nodes cannot fully independently determine canonical chain without external checkpoint/trust source [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14]. Weak-subjectivity trust conflicts with P9 independent verification. Independent verification of C5: UNRESOLVED.

#### 2.3.4 Performance and network behaviour

- **Communication complexity:** Not assigned for C5 [RS-013 §8.1]. Casper FFG requires validators to exchange votes on every checkpoint; bandwidth scales with validator count. Design characteristic, not deployment measurement. No fixed per-transaction bandwidth figure [RS-013 §8.2].
- **Bandwidth:** Not quantified. No number invented.
- **Latency sensitivity:** BFT finality requires multiple communication rounds; latency scales with round count and network delay. Variable latency increases timeouts and degrades liveness [RS-013 §8.3, §8.9]. No fixed latency ranges [RS-013 §8.3].
- **Message-size and propagation:** Attestations gossiped through P2P layer [RS-013 §5.5]. Propagation requirements and message-size limits for C5: UNRESOLVED.
- **Committee-size sensitivity:** Larger validator sets increase per-checkpoint bandwidth. Concrete sensitivity for C5: UNRESOLVED.
- **Operation under packet loss:** Packet loss increases timeouts and retransmissions, degrading liveness; degree protocol- and implementation-specific [RS-013 §8.9]. No quantified tolerance for C5.
- **Operation under prolonged partitions:** During partitions lacking ≥2/3 honest-stake quorum, finality halts; safety preserved; liveness suspended until reconnection [DLS1988 S3; Gasper2020 S15; RS-013 §8.5]. CAP/FLP constraints apply [GilbertLynch2002 S6; FLP1985 S2].
- **Recovery after connectivity returns:** View changes / leader rotation / state sync enable recovery; recovery time depends on validator count and state size [RS-013 §8.7–8.8]. Inactivity leak can recover liveness if offline validators' weight drops below 1/3 [Gasper2020 S15; EthereumWeakSubjectivity2024 S40]. Nodes offline > weak-subjectivity period require external checkpoint to rejoin [S40].

#### 2.3.5 Financial and economic implications

All economic claims are qualitative design concerns per RS-013 §20; D-015 OPEN RESEARCH; no numbers invented.

- **Security-budget dependencies:** Economic security ≈ cost to attack ≈ value of stake at stake; requires token value high enough to make attacks uneconomic; economically circular for a new network [CasperFFG2017 S14; Gasper2020 S15; RS-013 §7.5, §7.14, §19].
- **Stake concentration:** Stake concentration leads to centralisation and increases collusion risk [RS-013 §6.13]. Custodial staking / liquid-staking derivatives concentrate voting power in derivative managers [a16zSlashing2023 S35]. Delegation to pools amplifies concentration [RS-013 §6.6, §7.8].
- **Reward and fee dependencies:** Staking rewards = inflation + fees; determine security budget and token dilution. Stakers earn yield; non-stakers diluted [RS-013 §7.2, §7.3, §10].
- **Participation costs:** Token acquisition cost (market price × minimum stake), validator hardware, uptime requirements, slashing risk, unbonding-period illiquidity, opportunity cost [RS-013 §3.2]. No minimum stake for FST (UNRESOLVED).
- **Entry barriers:** Economic barrier (token acquisition) plus operational barrier (hardware/uptime). Delegation lowers entry barriers but concentrates power [RS-013 §6.6].
- **Wealth concentration:** Token wealth may concentrate through rewards — economic hypothesis, not observed universal law [RS-013 §6.14, §7.2].
- **Extractable value and transaction ordering:** Proposer controls transaction ordering within a slot → MEV extraction/frontrunning risk [DaianEtAl2020 S22, PARTIALLY VERIFIED metadata only, Ethereum-specific; RS-013 §7.3, §9.9].
- **Economic circularity:** Stake-based Sybil mechanisms require FST to have market value to fund security — circularity for a new network [RS-013 §19].

### 2.4 Considered alternative C6 — DAG-based dissemination + separate agreement (not shortlisted)

**Source:** RS-013 §5.6, §18.6; `docs/fst/research/candidate-c6-evaluation.md`; `docs/fst/research/candidate-c5-c6-comparative-assessment.md`.

- **Sybil resistance:** None intrinsic to Narwhal/Tusk; external stake/work/space layer required [NarwhalTusk2022 S10; RS-013 §18.6].
- **Agreement/ordering:** Narwhal DAG mempool for dissemination (asynchronous, tolerates asynchrony); Tusk async BFT consensus or Narwhal-HotStuff for ordering [NarwhalTusk2022 S10].
- **Voting power/proposer probability:** Depends on composed consensus layer + external Sybil layer; UNRESOLVED for C6-specific derivation [RS-013 §18.6].
- **Security:** Tusk: asynchronous BFT safety under fault bound (<1/3 Byzantine committee) using Narwhal DAG as data structure; randomized probabilistic termination under asynchrony (circumvents FLP via randomization, not deterministic) [NarwhalTusk2022 S10; FLP1985 S2]. Narwhal-HotStuff: inherits partial-synchrony liveness [HotStuff2019 S8]. No single unified security proof for the composition [RS-013 §18.6].
- **Operational:** Dissemination layer independent of validator set; entry/exit depends on composed consensus. No checkpoint/weak-subjectivity model documented; no established offline-recovery mechanism [RS-013 §18.6].
- **Performance:** Dissemination cost separate from consensus cost [NarwhalTusk2022 S10]; no single linear-per-transaction complexity assigned [RS-013 §8.1]. Tusk leaderless (removes leader DoS target) [Sonnino et al. 2024 S16 applied to Narwhal-HotStuff is PARTIALLY VERIFIED as a concern]. Less deployment history [RS-013 §18.6].
- **Economic:** Depends on composed Sybil layer; economic circularity if stake-based [RS-013 §18.6, §19].
- **Assessment:** C6 is not categorically stronger than C5 for FST. C6 has a structural advantage on dissemination-under-intermittency (relevant to D-019), but C5 has a more developed documented offline/recovery model. Neither satisfies D-018 without a separate bounded-offline-payment architecture. C6 retained as a considered alternative; research does not demonstrate a stronger case for FST's initial internet-connected test network.

## 3. Transaction-model sensitivity analysis

**Source:** RS-015 (`docs/fst/research/transaction-model-research.md`), complete as a research deliverable but OPEN RESEARCH pending owner selection (D-015 OPEN RESEARCH). RS-013 §21 (transaction-model dependencies). No transaction model is selected or approved here.

RS-015 compares two credible candidates for FST: (1) traditional UTXO, and (2) account-based with nonce and chain-ID protection. (eUTXO and hybrid/object-cell are also surveyed but not the primary sensitivity cases.) Both remain available for architectural sensitivity analysis per the mission (RS-015 completed but unresolved).

### 3.1 How each consensus candidate interacts with the transaction models

| Dimension | UTXO (traditional) | Account-based (nonce + chain-ID) |
|-----------|-------------------|----------------------------------|
| **Validation complexity** | Stateless, local; depends only on transaction and referenced inputs [RS-015 §3.1]. | State-dependent; requires reading sender balance/nonce from global state [RS-015 §3.2]. |
| **Double-spend prevention** | Native: each output spent once [RS-015 §3.1]. | Native: nonce used once in order + balance check [RS-015 §3.2]. |
| **Replay protection** | Not inherent; needs chain-ID binding added [RS-015 §3.1, §5.3]. | EIP-155 chain ID bound into signature + nonce [RS-015 §3.2, §5.3]. D-036 mechanisms OPEN RESEARCH for both. |
| **Transaction dependencies** | DAG via txid/vout; dependent txs (spend unconfirmed outputs) possible [RS-015 §3.1]. | Per-account nonce sequence; cross-account independent except shared-state effects [RS-015 §3.2]. |
| **Parallel validation** | High for non-conflicting inputs [RS-015 §4]. | Low: shared global state + nonce ordering force per-account serialization [RS-015 §3.2, §4]. |
| **Determinism** | High: off-chain checkable; fee predictable [RS-015 §3.1]. | Low: state-dependent; can fail mid-execution; fees not precisely predictable [RS-015 §3.2]. |
| **State growth/storage** | UTXO set, prunable (spent outputs removed) [RS-015 §3.1]. | Global state tree, harder to prune (accounts mutable, referenced by contracts) [RS-015 §3.2]. |
| **Institutional/batch** | Multi-input consolidation requires multi-signature coordination [RS-015 §3.1]. | Natural aggregate balance; strong fit for multi-input institutional payments [RS-015 §3.2]. |
| **Reorg/replay risk** | Reorg re-unspends outputs; txs return to mempool; double-spend may become valid on other chain [RS-015 §5.14]. | Reorg reverts nonce/balance changes; replay risk across reorg [RS-015 §5.14]. |

#### 3.1.1 C1 (HotStuff BFT + stake) sensitivity

- **Ordering/finality interaction:** C1's deterministic finality makes confirmation irreversible under assumptions, reducing reorg/replay risk for both models. Under probabilistic reorgs (if finality assumptions fail), UTXO re-spends outputs cleanly; account-based nonce ordering couples validity to chain position, making reorgs and replay protection more complex [RS-015 §5.14; RS-013 §21].
- **Parallel validation:** C1 orders transactions serially per view; the transaction model determines whether execution can be parallelized post-ordering. UTXO's stateless validation and native double-spend prevention simplify integration; account-based nonce ordering couples validity to chain position [RS-013 §21].
- **Sensitivity:** Low-to-moderate. C1's deterministic finality reduces transaction-model sensitivity around reorgs. UTXO's stateless validation is a better structural fit with BFT's batch ordering; account-based requires state-dependent validation at execution time. Neither model is selected; both remain sensitivity cases [RS-013 §21].

#### 3.1.2 C3 (Nakamoto PoW) sensitivity

- **Ordering/finality interaction:** C3's probabilistic finality means reorgs are possible. UTXO's native double-spend prevention and clean re-unspend-on-reorg simplify integration with probabilistic finality. Account-based nonce ordering under probabilistic finality creates replay risk and nonce-gap complexity [RS-015 §5.14; RS-013 §21].
- **Sensitivity:** Moderate. UTXO is the better structural fit for probabilistic finality (reorgs re-unspend outputs cleanly). Account-based nonce ordering couples validity to chain position, making reorgs more complex. This is the clearest transaction-model sensitivity difference among the candidates.
- **Dissemination:** Gossip-based in both models; propagation scope unresolved [RS-015 §5.13; RS-013 §2.5].

#### 3.1.3 C5 (PoS + finality gadget) sensitivity

- **Ordering/finality interaction:** C5's deterministic checkpoint finality reduces reorg exposure relative to probabilistic finality. Weak subjectivity (required for independent chain selection from genesis) interacts with transaction-model replay protection: new/returning nodes need an external checkpoint, which affects how transactions are validated after long disconnection [EthereumWeakSubjectivity2024 S40; CasperFFG2017 S14].
- **Parallel validation:** Same structural considerations as C1; C5 orders via LMD-GHOST fork choice then finalizes via Casper FFG. Transaction execution applies the agreed order deterministically [RS-013 §2.9].
- **Sensitivity:** Low-to-moderate. Deterministic checkpoint finality reduces reorg sensitivity; weak subjectivity adds a trust dependency for independent verification that is orthogonal to but compounded by the transaction model. Neither model selected.

#### 3.1.4 Overall sensitivity conclusion

- **UTXO** is structurally better aligned with probabilistic finality (C3) and with stateless, parallel validation (all candidates). Its reorg handling (re-unspend outputs) is clean.
- **Account-based** is structurally better aligned with institutional multi-input batching and simple wallet UX, but its nonce ordering couples validity to chain position, increasing reorg/replay complexity under probabilistic finality and weak subjectivity.
- **Neither model is selected.** RS-015 remains OPEN RESEARCH pending owner selection (D-015 OPEN RESEARCH). Both UTXO and account-based remain available as sensitivity cases for consensus evaluation, per the mission.
- **Dissemination:** Gossip-based in all models; propagation scope (gossip vs. direct to proposers) is unresolved (RS-013 component 2.5; RS-015 §5.13). Narwhal-style DAG dissemination separates dissemination from consensus [NarwhalTusk2022 S10; RS-013 §2.5] — relevant if C6 were later selected, but not for the shortlisted candidates.
- **Finality interactions:** Deterministic finality (C1, C5) reduces transaction-reorg/replay risk vs probabilistic finality (C3). This is a cross-cutting sensitivity point independent of the transaction-model choice.

## 4. Initial test-network requirements

This section derives a requirements-level architecture for a minimal permissionless FST test network. Requirements are not implementation specifications; they define what the test network must support, separating common, architecture-specific, and pre-implementation decisions. No genesis constants, token issuance, validator rewards, transaction formats, or consensus parameters are finalized.

### 4.1 Requirements common to all candidate architectures

| ID | Requirement | Rationale |
|----|-------------|-----------|
| TN-1 | **Network identity and genesis separation:** Each independent test network has a unique network identifier bound into signed node records and session context; wrong-network rejection enforced. | Fork protection (D-036 APPROVED); prevents cross-network replay and confusion. Core spec §2.1–2.3 (established implementation). |
| TN-2 | **Bootstrap and peer discovery:** Multiple independent bootstrap sources (not trust roots); peer exchange; signed node record validation (signature, sequence, expiry, network ID). | Connectivity assistance only; no single point of control (P1, P2). Threat model T-08. |
| TN-3 | **Node identity and authenticated sessions:** Versioned NodeId, Ed25519 identity keys, TLS 1.3 mutual-authenticated sessions with node-record binding; ephemeral TLS certificate separate from long-term identity key. | Established implementation (Milestones 2C–2D). Authentication ≠ Sybil resistance. |
| TN-4 | **Candidate validator participation:** Validator-capable nodes may join as candidates without central approval; no permissioned roster. | Permissionless participation direction (D-016 PROVISIONAL). Node roles §6 (future role, not implemented). |
| TN-5 | **Consensus communication:** Authenticated, bounded message exchange among candidate validators; per-type payload size limits; frame checksums; connection limits. | Network-layer baseline; consensus message types unresolved (D-017 OPEN RESEARCH). |
| TN-6 | **Transaction and block dissemination boundaries:** Gossip-based dissemination; syntactic validation before forwarding; propagation scope unresolved. | Transaction lifecycle layers separated (RS-015 §5); dissemination scope is RS-013 component 2.5 (unresolved). |
| TN-7 | **Chain synchronization:** Genesis sync, snapshot sync, catch-up sync after disconnection; state transfer mechanics reference FeinStatic snapshot patterns (D-032 PROVISIONAL) but are unresolved. | Node roles §9; state commitment scheme OPEN RESEARCH (RS-009). |
| TN-8 | **Fork detection and recovery:** Detect chain divergence; adopt canonical chain per candidate fork-choice rule; state sync on reconnection. | Fork-choice and finality unresolved (D-017 OPEN RESEARCH). |
| TN-9 | **Finality observation:** Observe finalized checkpoints/blocks per candidate finality model; expose finality status to independent verifiers. | Finality model unresolved (D-017 OPEN RESEARCH); safety/liveness distinguished per adversary model §1. |
| TN-10 | **Network partition testing:** Test operation under partition, message loss, delay, and reconnection; safety preserved, liveness suspended until reconnection (CAP/FLP). | Threat model T-04; consensus-validator-research §3.7, §8.5. |
| TN-11 | **Independent validator operation:** Validators run independently; no node category has privileged authority (P2); independent verification of state (P9). | Principles P1, P2, P4, P9. |
| TN-12 | **Test instrumentation and security metrics:** Instrument Byzantine behaviour, collusion, node failures, message loss, partition, Sybil attempts, and recovery; record metrics without exposing secrets. | Supports adversary-model validation; no secret logging (AGENTS.md §9). |

### 4.2 Requirements specific to each architecture

#### C1 (HotStuff BFT + stake)
- TN-C1-1: Stake bonding/unbonding mechanism for validator eligibility (amount, duration unresolved — D-015 OPEN RESEARCH).
- TN-C1-2: Epoch-based validator-set reconfiguration and committee rotation (frequency unresolved).
- TN-C1-3: Leader rotation and view-change mechanism; leader DoS mitigation (unresolved; Sonnino et al. 2024 S16 shows leader-targeted DoS disrupts HotStuff liveness).
- TN-C1-4: Quorum-certificate aggregation and broadcast among stake-weighted validators.
- TN-C1-5: Slashing evidence collection and punishment (rules unresolved).
- TN-C1-6: Weak-subjectivity checkpointing for PoS chain selection from genesis (schedule unresolved).

#### C3 (Nakamoto PoW)
- TN-C3-1: Proof-of-work difficulty adjustment and block interval (parameters unresolved).
- TN-C3-2: Mining software and pool participation (optional; miners can switch pools).
- TN-C3-3: Longest-chain fork choice and k-block confirmation-depth observation.
- TN-C3-4: No slashing, no unbonding, no registration (open entry/exit).
- TN-C3-5: Hashrate rental-attack exposure for small test networks (MITDCI2020 S30).

#### C5 (PoS + finality gadget)
- TN-C5-1: Stake bonding/unbonding; unbonding > weak-subjectivity period (values unresolved).
- TN-C5-2: LMD-GHOST fork choice and Casper FFG checkpoint finality; ≥2/3 stake supermajority.
- TN-C5-3: Per-slot proposer selection and leader DoS mitigation (PARTIALLY VERIFIED concern).
- TN-C5-4: Slashing conditions (S1/S2) and detection window (values unresolved).
- TN-C5-5: Weak-subjectivity checkpoints and trusted-checkpoint handling for rejoin after long disconnection (conflicts with P9; unresolved).

### 4.3 Decisions that must be approved before implementation

| Decision | Related | Classification |
|----------|---------|----------------|
| Transaction model (UTXO vs account-based) | RS-015, D-015 | OPEN RESEARCH |
| Sybil-resistance mechanism | RS-002, D-016 | PROVISIONAL (mechanism unresolved) |
| Consensus algorithm | RS-001/RS-013, D-017 | OPEN RESEARCH |
| Finality model (deterministic vs probabilistic) | D-017 | OPEN RESEARCH |
| Byzantine threshold / fault-bound parameters | Adversary model §3 | Parameter, not constant — owner decision |
| Validator rewards, issuance, monetary policy | D-015 | OPEN RESEARCH |
| Slashing rules, unbonding periods | RS-013 §6.8–6.9 | OPEN RESEARCH |
| Weak-subjectivity checkpointing (if PoS) | RS-013 §6.11 | OPEN RESEARCH |
| Chain-ID / transaction replay protection mechanism | D-036 mechanisms | OPEN RESEARCH |

None of these may be implemented as production design without owner agreement and an approved ADR (AGENTS.md prohibited actions).

### 4.4 Failure-injection scenarios for the test network

| Scenario | Description | Purpose |
|----------|-------------|---------|
| Byzantine validator behaviour | Validators equivocate, withhold votes, or send conflicting state within protocol bounds. | Validate safety under fault-bound parameter. |
| Validator collusion | Subset of validators coordinates to censor transactions or withhold finality votes. | Validate censorship-resistance and liveness assumptions. |
| Node failures | Validators or full nodes crash and restart; some do not return. | Validate quorum availability and recovery (TN-7, TN-8). |
| Message loss/delay | Network drops, delays, or reorders consensus messages. | Validate liveness under partial synchrony; timeout behaviour. |
| Network partition | Network split into disconnected components; reconnection after varying durations. | Validate safety preservation and liveness suspension (CAP/FLP); reconnection recovery. |
| Sybil attempt | Adversary creates many identities to influence discovery or (if implemented) validator participation. | Validate that authentication ≠ Sybil resistance; bootstrap-source resilience (T-02, T-08). |
| Recovery | Connectivity returns after partition; nodes resync and adopt canonical chain. | Validate state sync and fork-choice recovery (TN-7, TN-8). |
| Long offline (PoS candidates) | Nodes offline beyond weak-subjectivity period attempt to rejoin. | Validate weak-subjectivity/external-checkpoint dependency (C5, C1) — tests assumption, not approval. |

Scenarios are run at requirements level; no consensus implementation is required to define them.

## 5. Comparison matrices

All ratings are qualitative and justified; no numeric scores invented. "Theoretical" = protocol-paper property; "Published" = deployment measurement or empirical study; "Engineering" = assumption not yet validated.

### 5.1 Security assumptions

| Criterion | C1 (BFT + stake) | C3 (Nakamoto PoW) | C5 (PoS + finality gadget) | Basis |
|-----------|-----------------|-------------------|---------------------------|-------|
| Sybil mechanism | Stake lockup | Proof of Work | Stake lockup (Casper FFG) | RS-013 §18 |
| Fault-bound parameter | `f_bft` (<1/3 Byzantine voting power) | `h_pow` (<50% adversarial hashrate) | `q_finality` (≥2/3 honest stake) | Adversary model §3; RS-013 §9.2 |
| Safety type | Deterministic under fault bound | Probabilistic (k-depth) | Deterministic checkpoint (qualified) | RS-013 §3.11, §12 |
| Liveness assumption | Partial synchrony | Honest-majority hash rate | Partial synchrony, ≥2/3 stake | RS-013 §9.4 |
| Quorum model | Weighted quorum intersection | None (cumulative work) | Stake-weighted quorum (≥2/3) | RS-013 §3.2, §12 |
| Long-range attack surface | Relevant (stake) | N/A | Relevant (stake) | RS-013 §6.11 |
| Nothing-at-stake | N/A (no slashing in BFT core) | N/A (cost = split hashrate) | Relevant (slashing addresses) | RS-013 §6.12 |
| Adaptive corruption | UNRESOLVED | UNRESOLVED | Not established | RS-013 §18 |

### 5.2 Permissionless participation

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Network admission | Permissionless staking (no central approval) | Fully open (any hashrate) | Permissionless staking (no central approval) | RS-013 §18; D-016 PROVISIONAL |
| Registration required | On-chain registration (unresolved) | None (implicit) | On-chain bonding (unresolved) | RS-013 §6.7 |
| Entry barrier | Token acquisition + hardware | Hardware + electricity | Token acquisition + hardware | RS-013 §3.2, §7 |
| Exit mechanism | Unbonding (unresolved) | None (stop mining) | Unbonding (unresolved) | RS-013 §6.7–6.8 |

### 5.3 Byzantine safety and liveness

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Safety guarantee | Deterministic (<1/3) | Probabilistic (<50% hashrate) | Deterministic checkpoint (qualified ≥2/3) | RS-013 §12 |
| Liveness guarantee | Conditional (partial synchrony) | Conditional (honest majority) | Conditional (partial synchrony, ≥2/3) | RS-013 §9.4 |
| FLP/CAP handling | Safety always; liveness after GST | Probabilistic; partition reorgs | Safety preserved; liveness after GST | RS-013 §3, §8.5 |
| Leader DoS surface | High (leader per view) | None (no leader) | High (per-slot proposer) | RS-013 §8.4; S16 |

### 5.4 Finality

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Finality type | Deterministic | Probabilistic | Deterministic checkpoint | RS-013 §3.11, §12 |
| Settlement risk | Low under assumptions | Requires confirmation-depth risk mgmt | Low under assumptions (qualified by weak subjectivity) | Scope-v0.1 §Offline; RS-013 §19 |
| Reorg possible after finality | No (under assumptions) | Yes (exponentially unlikely) | No (under assumptions; qualified by weak subjectivity) | RS-013 §3.11; S40 |
| Weak subjectivity | Required (PoS) | None | Required (PoS) | S40; S14 |

### 5.5 Network resilience

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Partition tolerance | Safety preserved; liveness suspended | Minority reorged on reconnect | Safety preserved; liveness suspended | RS-013 §8.5; S6; S2 |
| Multi-transport fit (D-019) | Partial-synchrony dependent | More delay-tolerant | Partial-synchrony dependent | RS-013 §19 |
| Leader/central target | Leader per view | None | Per-slot proposer | S16 |
| Offline operation (D-018) | Needs separate offline-payment architecture | Needs separate offline-payment architecture | Needs separate offline-payment architecture | RS-013 §3.13 |

### 5.6 Centralization risks

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Concentration vector | Stake concentration, leader DoS | Mining-pool concentration | Stake concentration, pools/LSTs | RS-013 §6.6, §6.13, §7 |
| Wealth concentration | Economic hypothesis | Hashrate concentration | Economic hypothesis | RS-013 §6.14, §7.2 |
| Infrastructure/geographic | Operational risk | Mining farms in low-cost energy | Operational risk | RS-013 §6.17 |
| Governance capture | Stake-weighted governance risk | Pool operator influence | Stake-weighted governance risk | RS-013 §6.16 |

### 5.7 Economic dependencies

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Security budget | Token-value circularity | Hashrate value + token value | Token-value circularity | RS-013 §7.5, §7.14, §19, §20 |
| Reward/fee dependency | Rewards + fees | Block subsidy + fees | Staking rewards + fees | RS-013 §7.2, §7.3 |
| Participation cost | Token + hardware | Hardware + electricity | Token + hardware | RS-013 §3.2 |
| MEV / ordering | Leader controls ordering | Miner controls ordering | Proposer controls ordering | RS-013 §7.3, §9.9; S22 |

### 5.8 Implementation complexity

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Consensus complexity | Moderate-high (BFT + stake layer) | Low-moderate (longest chain) | High (fork choice + finality gadget composition) | RS-013 §18 |
| Sybil layer complexity | High (stake economics) | Low (PoW native) | High (stake + slashing + weak subjectivity) | RS-013 §6, §18 |
| Operational complexity | High (epochs, unbonding, slashing) | Low (open entry/exit) | High (epochs, unbonding, slashing, checkpoints) | RS-013 §6.7–6.9 |
| Deployment history | Limited (HotStuff derivatives) | Extensive (Bitcoin) | Moderate (Ethereum consensus clients, multiple independent impls) | RS-013 §11 |

### 5.9 Test-network suitability

| Criterion | C1 | C3 | C5 | Basis |
|-----------|----|----|----|-------|
| Permissionless test entry | Stake required (token value needed) | No token required initially | Stake required (token value needed) | D-015 OPEN; RS-013 §19 |
| Safety/liveness observability | Deterministic (easier to observe) | Probabilistic (requires depth observation) | Deterministic checkpoint (qualified) | RS-013 §3.11 |
| Failure injection | Well-defined (BFT faults) | Well-defined (PoW faults) | Well-defined (PoS faults) | RS-013 §9 |
| Weak-subjectivity trust | Required | None | Required (P9 tension) | S40 |
| Initial network assumption | Internet + partial synchrony | Internet | Internet + partial synchrony | RS-013 §19 |

## 6. Recommendation

**No unconditional recommendation is made.** The evidence supports a **conditional** conclusion:

- **C3 (Nakamoto PoW)** is preferable if FST prioritizes genuine permissionless entry without initial token value, a long deployment track record, and no slashing/weak-subjectivity complexity, and can tolerate probabilistic finality and mining centralization/energy concerns.
- **C1 (HotStuff BFT + stake)** is preferable if FST prioritizes deterministic finality and linear per-view communication, and can resolve stake economics, leader DoS, and weak-subjectivity trust.
- **C5 (PoS + finality gadget)** is preferable if FST prioritizes deterministic checkpoint finality with a formally composed fork-choice/finality separation, and can resolve weak-subjectivity trust, slashing, and stake concentration.

The candidates remain **incomparable without owner decisions** on finality model (deterministic vs probabilistic), Sybil-resistance mechanism (work vs stake), and monetary policy (token value for security budget). No recommendation automatically becomes an approved FST decision (D-016 PROVISIONAL, D-017 OPEN RESEARCH, D-015 OPEN RESEARCH).

## 7. Unresolved research

All of the following remain OPEN RESEARCH; none is resolved by this evaluation:

1. FST explicit adversary model parameters (`f_bft`, `h_stake`, `h_pow`, `q_finality`) — adversary-model-v0.1.md §10.
2. Transaction model (UTXO vs account-based) — RS-015, D-015 OPEN RESEARCH.
3. Sybil-resistance mechanism — RS-002, D-016 PROVISIONAL.
4. Consensus algorithm — RS-001/RS-013, D-017 OPEN RESEARCH.
5. Finality model and settlement latency — D-017 OPEN RESEARCH.
6. Validator rewards, issuance, monetary policy — D-015 OPEN RESEARCH.
7. Slashing rules, unbonding periods, weak-subjectivity checkpointing — RS-013 §6.8–6.9, §6.11.
8. Chain-ID / transaction replay protection mechanism — D-036 mechanisms OPEN RESEARCH.
9. Independent client implementation count — RS-012, D-035 OPEN RESEARCH.
