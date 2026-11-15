# FST Consensus and Validator Research

**Status:** RESEARCH DOCUMENT — OPEN RESEARCH.
**Research ID:** RS-013 (registered in [`research-register.md`](research-register.md)).
**Related decisions:** D-016 (permissionless validator participation), D-017 (BFT research direction), D-024 (FeinStatic Raft preserved as reference), D-035 (independent client implementation count).
**Classification:** OPEN RESEARCH — no consensus algorithm, validator-selection mechanism, or finality model is approved or selected.

This document is a research summary for the FST project. It surveys existing knowledge on
permissionless consensus and validator architecture, identifies limitations, and recommends
technically credible directions for further investigation. It does **not** select a final
consensus algorithm. All mechanisms described here remain open research.

## 1. Executive summary

FST requires a permissionless, Byzantine-resilient consensus protocol for a globally
distributed financial network. The existing FeinStatic implementation uses Raft, which is
crash-fault tolerant and permissioned; it cannot be incrementally extended to meet FST's
requirements (principle P9, decision D-017, decision D-024). This research surveys
consensus theory, protocol families, validator membership, economics, network resilience,
security, formal verification, existing implementations, and a comparative evaluation.
The strongest candidate architecture families for FST are classical BFT, leader-based BFT
(HotStuff-style), proof-of-work Nakamoto-style, proof-of-stake longest-chain, and DAG-based
mempool-consensus separation. Each involves trade-offs among permissionless entry, Byzantine
fault tolerance, finality type, bandwidth, latency, and decentralisation. No single family
satisfies all FST requirements without unresolved research in validator economics, Sybil
resistance, and partition tolerance.

## 2. Consensus theory fundamentals

### 2.1 Distributed agreement

Distributed agreement requires a set of participants to reach a common decision despite
failures. The fundamental impossibility result is the FLP impossibility: in an asynchronous
system, no deterministic consensus protocol can guarantee both safety and liveness with even
one crash failure [FischerLynchPaterson1985]. This means FST must make assumptions about
timing (partial synchrony or synchrony) or use randomisation to achieve liveness.

### 2.2 Byzantine fault tolerance

Byzantine fault tolerance extends crash-fault tolerance to arbitrary (malicious) behaviour.
The classical result is that Byzantine agreement requires at least 3f+1 participants to
tolerate f Byzantine faults [LamportShostakPease1982], [CastroLiskov1999]. In a
permissionless setting with unknown participant count, this bound cannot be applied directly;
Sybil resistance must be layered on top.

### 2.3 Sybil attacks and open membership

A Sybil attack creates many identities to gain disproportionate influence [Douceur2002].
In a permissionless network, identity is cheap to create, so some costly or scarce resource
(Proof of Work, stake, reputation, or social graph) must gate participation. Without Sybil
resistance, quorum intersection and Byzantine thresholds are meaningless.

### 2.4 Safety and liveness

Safety: nothing bad happens (e.g., two conflicting blocks are never both finalised).
Liveness: something good eventually happens (e.g., new blocks are finalised).
FLP implies that under asynchrony, safety and liveness cannot both be guaranteed
deterministically. BFT protocols typically guarantee safety unconditionally and liveness
under partial synchrony.

### 2.5 Partial synchrony and synchrony assumptions

Partial synchrony [DworkLynchStockmeyer1988]: there exists an unknown bound on message
delay and processing time, but protocols cannot rely on it for safety. Most BFT protocols
assume partial synchrony for liveness. Pure synchrony assumes known bounds; pure asynchrony
assumes none.

### 2.6 Network partitions

A partition splits the validator set into disconnected groups. By the CAP theorem
[Brewer2000], a system cannot simultaneously guarantee consistency and availability during
a partition. BFT protocols typically preserve safety during partitions and sacrifice
liveness until a quorum reconnects.

### 2.7 Message delays, loss and reordering

Networks may delay, drop, or reorder messages. Consensus protocols must handle this via
retransmission, timeouts, sequence numbers, and view changes. Unbounded message delay breaks
liveness under synchrony assumptions.

### 2.8 Byzantine participants and collusion

Byzantine participants may deviate arbitrarily from the protocol, including colluding,
equivocating (sending conflicting messages to different peers), and withholding messages.
Collusion among f Byzantine validators can prevent liveness (by withholding votes) or
attempt safety violations (by equivocating), subject to the 3f+1 bound.

### 2.9 Quorum intersection

Quorum intersection ensures any two quorums share at least one honest participant. This is
the mechanism by which BFT safety is achieved: if two conflicting decisions were made, the
shared honest participant would have voted for both, violating its own protocol rules.

### 2.10 Forks and conflicting histories

A fork occurs when the network diverges into multiple chains. Deterministic finality
prevents forks after finalisation; probabilistic finality (Nakamoto-style) makes forks
increasingly unlikely but never impossible. FST's fork-protection requirements (decision
D-036) require unique network identities, replay protection, and distinct currency
identifiers for any independent fork.

### 2.11 Deterministic and probabilistic finality

Deterministic finality: once a block is finalised, it cannot be reverted (BFT protocols).
Probabilistic finality: reversion becomes exponentially unlikely with depth but is never
impossible (Nakamoto-style). FST's finality model is unresolved (decision D-017).

### 2.12 Consensus recovery after network disruption

After a partition or disruption, the protocol must recover liveness when a quorum of honest
validators reconnects. Recovery mechanisms include view changes, leader rotation, state
synchronisation, and checkpointing.

### 2.13 Four distinct operation modes

FST's scope distinguishes four modes that must not be conflated:

1. **Global blockchain consensus:** all validators agree on a single canonical chain.
2. **Local regional consensus:** a subset of validators agrees locally during disconnection.
3. **Offline transactions under pre-authorised spending limits:** local spending authority
   with bounded risk, reconciled later.
4. **Global settlement after connectivity returns:** reconciling local operations into the
   global chain.

Local offline continuity is **not** equivalent to global consensus (scope-v0.1.md,
Offline financial operation).

## 3. Permissionless consensus requirements

Based on FST's approved principles (P1, P2, P4, P5, P6, P7, P8, P9, P10) and scope:

- **Permissionless participation:** any independent participant may join validation without
  approval from a central authority (decision D-016).
- **No permanent privileged operator:** no central administrator or master key (P8).
- **Byzantine fault tolerance:** safety under explicitly bounded malicious behaviour (P9).
- **Upgrade rejection:** participants may reject protocol upgrades (P5).
- **Fork legitimacy:** no fork has exclusive legitimacy (P6, P7).
- **Founder independence:** the network survives without its founding team (P10).
- **Multi-transport communication:** internet, mesh, radio, satellite, delay-tolerant
  (decision D-019).
- **Offline operation:** bounded local spending authority and eventual reconciliation
  (decision D-018).

The exact consensus algorithm, Sybil-resistance mechanism, validator economics, and
finality model remain open research (scope-v0.1.md, Decentralisation).

## 4. Consensus family comparisons

### 4.1 Classical Byzantine fault-tolerant protocols

**Examples:** PBFT [CastroLiskov1999], Paxos variants (crash-fault only).

**Membership assumptions:** fixed, known validator set; permissioned.

**Message complexity:** O(n²) per view for PBFT (all-to-all communication).

**Quorum rules:** 2f+1 of 3f+1 for prepare and commit.

**Resilience:** tolerates f Byzantine of 3f+1 total.

**Scalability limitations:** quadratic message complexity limits validator count; fixed
membership prevents permissionless entry.

**FST relevance:** PBFT provides strong safety and deterministic finality but requires
permissioned membership. Adapting it to permissionless entry requires a Sybil-resistance
layer on top (stake or work), which changes the quorum model.

### 4.2 Proof-of-stake-based BFT protocols

**Examples:** Tendermint / CometBFT [Buchman2016], HotStuff [YinMalkhiReiterEtAl2019].
DiemBFT is a HotStuff-based protocol with epoch changes and reconfiguration [DiemBFTWhitepaper2020 —
reference not in table; requires independent verification].

**Tendermint / CometBFT:**
- Leader-based BFT with round-robin proposers.
- O(n²) communication per block (voting).
- Deterministic finality.
- Fixed validator set with stake-weighted voting in practice.
- Block time ~1 second.

**HotStuff:**
- O(n) communication per view (linear, via quorum certificates).
- Deterministic finality.
- Pipelined consensus (HotStuff / Jolting / Chained-HotStuff).
- View-change complexity O(n²) worst case.

**DiemBFT:**
- HotStuff-based, with epoch changes and reconfiguration.
- Deterministic finality.

**Sybil resistance:** none intrinsic; requires external stake/token mechanism.

**FST relevance:** leader-based BFT offers deterministic finality and reasonable latency
but has quadratic (Tendermint) or linear-with-view-change-quadratic (HotStuff)
communication. Permissionless entry requires staking, which introduces economic centralisation
risks.

### 4.3 Nakamoto-style consensus

**Examples:** Bitcoin [Nakamoto2008].

**Mechanism:** Proof of Work, longest-chain rule, cumulative work.

**Finality:** probabilistic; k-block confirmation depth.

**Sybil resistance:** Proof of Work (hash rate cost).

**Resilience:** tolerates <50% hash rate controlled by adversary (honest majority
assumption).

**Scalability:** limited by block size and block interval; orphan rate increases with
network propagation delay.

**FST relevance:** Nakamoto-style consensus is genuinely permissionless and has a long
deployment track record. However, probabilistic finality, high energy cost, mining
centralisation risk, and slow finality (minutes to hours) are significant drawbacks for a
financial network.

### 4.4 Proof-of-stake longest-chain protocols

**Examples:** Ouroboros (Cardano) [KiayiasRussellEtAl2017], Ethereum Gasper
[ButerinGriffith2019], [CasperFFG2019].

**Mechanism:** stake-weighted block proposal and chain selection.

**Finality:** Ouroboros Praos provides probabilistic finality with settlement delay;
Gasper combines LMD-GHOST fork-choice with Casper FFG finality gadget (deterministic
finality on checkpoints).

**Sybil resistance:** stake (token ownership).

**Resilience:** honest stake majority assumptions; nothing-at-stake and long-range attacks
are known concerns.

**FST relevance:** staking-based protocols align with FST's native asset but introduce
wealth-concentration and centralisation risks. Economic security depends on token value,
which is circular for a new network.

### 4.5 DAG-based and asynchronous consensus

**Examples:** Narwhal and Tusk [DanezisNissenbaumEtAl2022], AlephBFT
[Petermann2020 — reference not in table; requires independent verification], Avalanche-family
[AvaWhitepaper2019 — reference not in table; requires independent verification].

**Narwhal and Tusk:**
- Separates mempool (DAG of transactions) from consensus (Tusk).
- Linear communication per transaction.
- High throughput under good network conditions.

**AlephBFT:**
- DAG-based, asynchronous BFT.
- Committee-based, dynamic membership.

**Avalanche-family:**
- Probabilistic finality via repeated sub-sampled voting.
- High throughput, low latency under synchrony.
- Security model differs from BFT (probabilistic safety under bounded adversary).

**FST relevance:** DAG-based designs offer throughput and latency advantages but add
complexity. Asynchronous variants sacrifice liveness guarantees under asynchrony (FLP).
Avalanche-style probabilistic safety does not match BFT's deterministic safety guarantees.

## 5. Validator membership and Sybil resistance

### 5.1 Validator identity

Permissionless validators need cryptographic identities (public keys). Identity creation
must be cheap to encourage participation but expensive enough to prevent Sybil attacks
when combined with a Sybil-resistance resource.

### 5.2 Sybil resistance

Without Sybil resistance, an adversary can create unlimited identities and exceed any
Byzantine threshold. Mechanisms:

- **Proof of Work:** computational cost per identity/block [Nakamoto2008].
- **Proof of Stake:** token lockup as economic cost [KiayiasRussellEtAl2017].
- **Proof of Space/Time:** storage cost [ChiaProofOfSpaceAndTime — reference not in table;
  marked UNRESOLVED; requires independent verification].
- **Social graph / reputation:** not well-established for open financial networks.

Each mechanism has distinct centralisation and entry-barrier characteristics.

### 5.3 Validator selection

Selection determines who proposes and votes. Options:
- Random sampling (sortition) from eligible set.
- Round-robin rotation.
- Stake-weighted selection.
- Hash-rate weighted (PoW).

Random sampling with stake weighting reduces centralisation but requires a source of
randomness (verifiable random functions, beacon chains).

### 5.4 Stake-based participation

Validators lock tokens as collateral. Misbehaviour can be slashed. Stake concentration
leads to centralisation; delegation to pools amplifies this.

### 5.5 Mining-based participation

Miners invest in hardware and energy. Pool centralisation is a known issue in PoW
networks.

### 5.6 Delegation and staking pools

Delegation lowers entry barriers but concentrates power in pool operators. This is a
known centralisation vector in PoS networks.

### 5.7 Validator entry and exit

Permissionless entry requires on-chain registration or implicit entry (e.g., mining).
Exit requires unbonding periods to prevent stake withdrawal before slashing can be
applied. Long unbonding periods reduce capital efficiency; short periods weaken security.

### 5.8 Bonding and unbonding

Bonding locks stake for validation. Unbonding releases it after a delay. The delay must
exceed the slashing detection window.

### 5.9 Slashing

Slashing penalises misbehaviour (double-signing, offline time). It requires evidence
collection and a slashing mechanism. Slashing introduces centralisation risk if a small
group decides what counts as misbehaviour.

### 5.10 Validator rotation

Rotating validators limits the window in which a corrupted validator set can cause harm.
Rotation must be frequent enough for security but infrequent enough for stability.

### 5.11 Long-range attacks

In PoS, an adversary who previously controlled stake can rewrite history from an old
checkpoint ("long-range attack"). Defences include checkpoints, weak subjectivity, and
finality gadgets [Buterin2016].

### 5.12 Nothing-at-stake attacks

In PoS, validators can vote on multiple chains at no cost (unlike PoW, where mining on
multiple chains splits hash rate). Finality gadgets and slashing address this.

### 5.13 Stake concentration

A small number of large stakeholders can dominate validation, reducing decentralisation
and increasing collusion risk.

### 5.14 Wealth concentration

Token wealth concentrates over time through rewards (rich-get-richer), compounding stake
concentration.

### 5.15 Validator collusion

Validators may collude to censor transactions, reorg history, or extract value. Collusion
resistance requires economic cost and geographic/infrastructure diversity.

### 5.16 Governance capture

If governance is stake-weighted, wealthy stakeholders control protocol upgrades, potentially
capturing the network.

### 5.17 Geographic and infrastructure concentration

Validators concentrated in few jurisdictions or cloud providers create single points of
control and failure.

### 5.18 Participation role distinctions

FST must distinguish:

- **Running a full node:** validates all state independently; does not necessarily
  participate in consensus.
- **Validating transactions:** checks transaction validity locally.
- **Participating in consensus:** votes on block ordering/finality.
- **Proposing blocks:** selects transactions and orders them.
- **Producing blocks:** creates and signs block headers (often same as proposing).
- **Delegating economic security:** token holders delegate stake to validators without
  running nodes.

A public blockchain does not automatically have genuinely decentralised validation.
Actual participation barriers (hardware, bandwidth, stake amount) determine real
decentralisation.

## 6. Validator economics

### 6.1 Initial token allocation

Initial distribution affects early centralisation. Fair launch, pre-mine, and ICO models
have different concentration profiles.

### 6.2 Validator rewards

Rewards incentivise participation but increase inflation. Reward design affects
centralisation (e.g., economies of scale favour large validators).

### 6.3 Transaction fees

Fees pay for block space and can prioritise transactions. Fee markets may centralise
block production (MEV).

### 6.4 Inflation and issuance incentives

Inflation funds security but dilutes holders. The relationship between token value and
security is circular for a new network.

### 6.5 Proof-of-stake economic security

Economic security = cost to attack = value of stake at stake. Requires token value to be
high enough to make attacks uneconomic.

### 6.6 Proof-of-work operating costs

PoW security = cost of hardware + energy. High operating costs are a security feature
(wasteful by design) but create environmental and centralisation concerns.

### 6.7 Slashing and punishment

Slashing deters misbehaviour but requires accurate detection and fair application.

### 6.8 Delegation and pooled validation

Pools reduce entry barriers but concentrate voting power.

### 6.9 Validator profitability

Profitability determines participation. Low profitability drives validators out, reducing
security.

### 6.10 Attack costs

The economic cost of attacking the network must exceed the potential gain. This depends
on total stake/hash rate and token value.

### 6.11 Censorship incentives

Validators may censor transactions for profit or coercion. Mempool privacy and
proposer-separator designs address this.

### 6.12 Extractable value and transaction ordering

MEV (Maximal Extractable Value) arises from controlling transaction ordering. It
centralises block production and harms users.

### 6.13 Long-term network sustainability

Security budget must remain adequate as rewards decrease (if issuance is capped). Fee
markets must sustain validator income.

### 6.14 Monetary-consensus interdependence

Monetary policy (supply, issuance, rewards) and consensus (Sybil resistance, security
budget) are interdependent:
- Consensus determines what resource secures the network (work, stake, etc.).
- Monetary policy determines the value and issuance of that resource.
- Neither can be designed in isolation.

FST monetary policy is unresolved (decision D-015). Consensus selection should not
presuppose a monetary policy.

## 7. Network partition and communication resilience

### 7.1 Consensus message complexity

PBFT/Tendermint: O(n²) per block. HotStuff: O(n) per view, O(n²) per view change.
Nakamoto: O(n) block propagation.

### 7.2 Bandwidth requirements

BFT protocols require validators to exchange votes on every block. High validator counts
increase bandwidth quadratically (PBFT) or linearly per view (HotStuff).

### 7.3 Latency sensitivity

BFT finality requires multiple communication rounds (typically 2-4). Latency scales with
round count and network delay.

### 7.4 Validator communication patterns

All-to-all (PBFT), leader-centric (Tendermint/HotStuff), gossip (Avalanche), DAG
broadcast (Narwhal).

### 7.5 Quorum availability

During partitions, quorums may be unreachable. Liveness is sacrificed until reconnection.

### 7.6 Synchronization and state transfer

New or rejoining validators must synchronise state. Snapshots and state proofs reduce
sync time. FeinStatic's snapshot-transfer engineering (v2.0.0/v2.1.0) is relevant
reference material.

### 7.7 Recovery after partitions

View changes, leader rotation, and state sync enable recovery. Recovery time depends on
validator count and state size.

### 7.8 Validator geographic distribution

Geographic diversity improves partition resilience but increases latency (speed of light).

### 7.9 Performance degradation under packet loss

Packet loss increases timeouts and retransmissions, degrading liveness.

### 7.10 Multi-transport compatibility

FST requires internet, mesh, radio, satellite, and delay-tolerant networks (decision
D-019). BFT protocols assume reliable point-to-point channels; adaptation to store-and-forward
or intermittent links is open research. No consensus protocol can guarantee global progress
during arbitrary prolonged partitions while preserving safety (CAP/FLP).

## 8. Security assumptions and attack analysis

### 8.1 Adversary model

FST must define: number of Byzantine validators, network synchrony, adversary resources
(hash rate, stake, bandwidth), and adversary goals (double-spend, censorship, liveness
denial).

### 8.2 Byzantine fault threshold

Classical BFT: f of 3f+1 (~33%). Nakamoto PoW: <50% hash rate. PoS: honest stake
majority. These thresholds assume the Sybil-resistance mechanism holds.

### 8.3 Safety guarantees

BFT: deterministic safety under quorum intersection. Nakamoto: probabilistic safety
(Reorg depth k).

### 8.4 Liveness assumptions

BFT: partial synchrony, quorum connectivity. Nakamoto: honest majority hash rate.

### 8.5 Sybil-resistance assumptions

PoW: adversary cannot acquire >50% hash rate. PoS: adversary cannot acquire >50% stake.
Both require the underlying resource to be costly and decentralised.

### 8.6 Cryptographic assumptions

Digital signatures (Ed25519, BLS), hash functions (SHA-256), VRFs, threshold signatures.
FeinStatic uses Ed25519 and X25519/HKDF/AES-GCM; reuse requires cryptographic review
(decision D-023).

### 8.7 Economic security assumptions

Attack cost > attack gain. Depends on token value, total stake/hash rate, and slashing.

### 8.8 Network synchrony assumptions

Partial synchrony for most BFT. Asynchrony sacrifices liveness guarantees.

### 8.9 Known attacks

- Double-spend (Nakamoto reorg, BFT equivocation)
- Long-range attack (PoS)
- Nothing-at-stake (PoS)
- Selfish mining (PoW)
- Eclipse attack (P2P layer)
- DDoS on leaders (leader-based BFT)
- Censorship (validator collusion)
- MEV extraction (ordering control)

### 8.10 Formal proofs

Many BFT protocols have safety proofs (PBFT, HotStuff, Tendermint). Nakamoto-style
analysis is more heuristic (simplified models).

### 8.11 Known implementation vulnerabilities

Production blockchain implementations have had consensus bugs (e.g., Tendermint
consensus pauses, Ethereum finality issues). Paper guarantees ≠ implementation security.

### 8.12 Operational attack surfaces

P2P layer, node software, key management, upgrades, governance.

## 9. Formal verification opportunities

- Model checking (TLA+, PlusCal) for consensus safety/liveness.
- Interactive theorem proving (Coq, Isabelle) for protocol correctness.
- Adversarial simulation (network faults, Byzantine behaviour).
- Fuzzing of consensus implementations.
- Reproducible test networks (chaos testing, partition testing).

Formal verification increases confidence but does not eliminate implementation risk.

## 10. Existing implementations

**Examine without copying:**

- **Bitcoin Core:** PoW Nakamoto-style, long deployment, extensive test network.
- **CometBFT (formerly Tendermint):** BFT, deterministic finality, used in Cosmos SDK.
- **Ethereum consensus clients:** Gasper (LMD-GHOST + Casper FFG), multiple independent
  implementations (Prysm, Lighthouse, Teku, Nimbus, Lodestar).
- **Cardano:** Ouroboros family, PoS longest-chain.
- **AlephBFT / Subspace:** DAG-based / asynchronous variants.
- **Avalanche:** Avalanche consensus family.

**What to learn:** protocol specifications, node architecture, validator software, test
networks, test vectors, state synchronisation, upgrade mechanisms, operational requirements,
known limitations.

**Protocol specifications must remain independent of any particular implementation.**

## 11. Detailed comparison matrix

| Criterion | Classical BFT (PBFT) | Leader BFT (HotStuff/Tendermint) | Nakamoto PoW | PoS Longest-Chain (Ouroboros/Gasper) | DAG-Based (Narwhal/Tusk, AlephBFT) |
|-----------|----------------------|----------------------------------|--------------|--------------------------------------|--------------------------------------|
| Permissionless participation | No (fixed set) | No (fixed set, stake-layer needed) | Yes (any miner) | Yes (any staker) | No (fixed set, Sybil-layer needed) |
| Byzantine resistance | f of 3f+1 (~33%) | f of 3f+1 (~33%) | <50% hash rate | Honest stake majority | f of 3f+1 (~33%) |
| Sybil resistance | None intrinsic | None intrinsic | Proof of Work | Stake | None intrinsic |
| Safety assumptions | Partial synchrony, quorum intersection | Partial synchrony, quorum intersection | Honest majority hash | Honest stake majority, finality gadget | Partial synchrony / asynchrony variants |
| Liveness assumptions | Partial synchrony, quorum connectivity | Partial synchrony, leader rotation | Honest majority hash | Honest stake, network synchrony | Network conditions |
| Network partitions | Safety preserved, liveness lost | Safety preserved, liveness lost | Temporary forks, eventual convergence | Temporary forks, eventual convergence | Safety preserved, liveness lost |
| Finality | Deterministic | Deterministic | Probabilistic | Probabilistic + checkpoint finality | Deterministic / probabilistic variants |
| Throughput | Limited by O(n²) messages | Limited by O(n) per view | Limited by block size/interval | Limited by block parameters | High under good conditions |
| Latency | Multiple rounds (2-4) | Multiple rounds (2-4+ view changes) | Minutes to hours | Seconds to minutes (finality gadget) | Low under good conditions |
| Bandwidth | O(n²) per block | O(n) per view, O(n²) view change | O(n) block propagation | O(n) per block | O(n) per transaction (mempool) |
| Decentralisation | Permissioned, centralised | Permissioned, centralised | Mining pools concentrate | Stake concentration, pools concentrate | Depends on Sybil layer |
| Incentives | None intrinsic | None intrinsic | Block reward + fees | Stake rewards + fees | None intrinsic |
| Attack resistance | Well-studied | Well-studied | Long deployment, known attacks | Long-range, nothing-at-stake | Less deployment history |
| Implementation maturity | Research + prototypes | Production (Cosmos, Diem) | Production (Bitcoin) | Production (Cardano, Ethereum) | Research + early production |
| Independent clients | Limited | Multiple (Cosmos SDK) | Multiple (Bitcoin Core, etc.) | Multiple (Ethereum clients) | Limited |
| Offline compatibility | None | None | None (requires global consensus) | None | None |

*Note: Qualitative assessment only. No quantitative scores invented. Published results vary by
implementation and network conditions.*

## 12. Architecture candidates for FST

Based on FST's requirements (permissionless, Byzantine, multi-transport, offline-capable,
no privileged operator), the following candidate families warrant further investigation:

### Candidate 1: Leader-based BFT with stake Sybil layer
**HotStuff / Tendermint-style consensus with stake-weighted validator set.**
- Advantages: deterministic finality, linear communication (HotStuff), deterministic
  finality, well-understood safety.
- Weaknesses: permissioned consensus core requires stake layer for permissionless entry;
  quadratic view changes (Tendermint); leader DDoS target; stake centralisation.
- Compatible with FST: partially; requires Sybil-resistance research and stake economics.

### Candidate 2: Nakamoto-style Proof of Work
**Bitcoin-style longest-chain with PoW.**
- Advantages: genuinely permissionless, long deployment track record, no stake
  concentration at genesis, no slashing complexity.
- Weaknesses: probabilistic finality (slow), high energy cost, mining centralisation,
  51% attack threshold, slow settlement for financial transactions.
- Compatible with FST: partially; finality and energy concerns unresolved.

### Candidate 3: Proof-of-stake longest-chain with finality gadget
**Ouroboros / Gasper-style.**
- Advantages: deterministic checkpoint finality, permissionless staking, lower energy than
  PoW.
- Weaknesses: stake concentration, long-range attacks, nothing-at-stake, economic circularity
  (token value funds security), complex slashing.
- Compatible with FST: partially; aligns with native asset but centralisation risks unresolved.

### Candidate 4: Hybrid: BFT finality gadget + Nakamoto-style ordering
**Finality gadget (Casper FFG-style) layered on a permissionless ordering protocol.**
- Advantages: probabilistic ordering with deterministic finality checkpoints.
- Weaknesses: complexity, interaction between layers, Sybil-resistance for both layers.
- Compatible with FST: partially; research needed on layer interaction.

### Candidate 5: DAG-based mempool-consensus separation
**Narwhal/Tusk-style.**
- Advantages: high throughput, linear per-transaction bandwidth, mempool/consensus
  separation.
- Weaknesses: complexity, less deployment history, permissioned core requires Sybil layer,
  liveness under asynchrony not guaranteed.
- Compatible with FST: partially; throughput benefits for financial network.

## 13. Unsuitable approaches and reasons

- **Plain Raft:** crash-fault only, permissioned, no Byzantine tolerance (decision D-024).
- **Purely asynchronous BFT without timing assumptions:** cannot guarantee liveness (FLP);
  FST needs liveness under realistic conditions.
- **Avalanche-style probabilistic safety alone:** does not provide deterministic safety
  guarantees expected for financial settlement; requires additional analysis.
- **Centralised or federated consensus:** violates P1, P2, P8 (unilateral control,
  privileged authority).
- **Consensus without Sybil resistance in a permissionless setting:** quorum thresholds
  are meaningless if identities are free.

## 14. Open technical questions

1. Which Sybil-resistance mechanism best suits FST's financial use case (work, stake,
   space, or hybrid)?
2. Deterministic or probabilistic finality? What settlement latency is acceptable?
3. What Byzantine threshold (33%, 50%, other) is appropriate given FST's adversary model?
4. How does FST prevent validator centralisation (stake pools, mining pools, geographic
   concentration)?
5. How does FST handle long-range attacks and nothing-at-stake if PoS is used?
6. What is the relationship between token value and security budget (circularity problem)?
7. How does consensus adapt to multi-transport networks (mesh, radio, satellite,
   delay-tolerant)?
8. How many independent client implementations are needed before mainnet (decision D-035)?
9. What is the offline operation model (bounded spending authority, reconciliation)?
10. What are the upgrade and fork governance mechanisms (decision D-05)?

## 15. Research references

**Verification status:** No external web access was available during this verification
task. References below are recorded from training data and have NOT been independently
verified against original publications. Each reference is marked with its verification
status:

- **LIKELY** — Publication, authors, and venue match known published work from training
  data; DOI/URL may be approximate.
- **UNCERTAIN** — Bibliographic details may be incomplete or imprecise; requires
  independent verification.
- **UNRESOLVED** — Source could not be confirmed; treat as requiring replacement.

| # | Title | Authors | Date | Venue | DOI/URL | Relevance | Assumptions | Limitations | Verification |
|---|-------|---------|------|-------|---------|-----------|-------------|-------------|--------------|
| 1 | The Byzantine Generals Problem | Lamport, Shostak, Pease | 1982 | ACM Transactions on Programming Languages and Systems | doi:10.1145/357172.357176 | Byzantine agreement 3f+1 bound | Synchronous, known participants | Permissioned only | LIKELY |
| 2 | Impossibility of Distributed Consensus with One Faulty Process | Fischer, Lynch, Paterson | 1985 | Journal of the ACM | doi:10.1145/3149.214121 | FLP impossibility | Deterministic, asynchronous | No randomisation | LIKELY |
| 3 | Paxos Made Simple | Lamport | 2001 | ACM SIGACT News | — | Crash-fault consensus | Crash faults only | Not Byzantine | LIKELY |
| 4 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | — | BFT state machine replication | Partial synchrony, known set | O(n²), permissioned | LIKELY |
| 5 | Bitcoin: A Peer-to-Peer Electronic Cash System | Nakamoto | 2008 | bitcoin.org whitepaper | bitcoin.org/bitcoin.pdf | Nakamoto-style PoW consensus | Honest majority hash | Probabilistic finality | LIKELY |
| 6 | Ouroboros: A Provably Secure Proof-of-Stake Blockchain Protocol | Kiayias, Russell, David, Oliynykov | 2017 | IACR ePrint / Crypto 2017 | eprint.iacr.org/2017/573 | PoS longest-chain | Honest stake majority | Stake concentration | LIKELY |
| 7 | HotStuff: BFT Consensus with Linearity | Yin, Malkhi, Reiter, et al. | 2019 | ACM PODC | doi:10.1145/3311374.3335964 | Linear BFT communication | Partial synchrony | View change O(n²) | LIKELY |
| 8 | Tendermint: Consensus without Mining | Buchman | 2016 | Thesis / whitepaper | — | Leader-based BFT | Known validator set | O(n²) voting | UNCERTAIN |
| 9 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2019 | IACR ePrint | eprint.iacr.org/2019/573 | PoS finality gadget | Honest stake | Complexity | LIKELY |
| 10 | Narwhal and Tusk: a DAG-based Mempool and Async BFT Consensus | Danezis, Nissenbaum, et al. | 2022 | USENIX NSDI | — | DAG-based consensus | Network conditions | Complexity | UNCERTAIN |
| 11 | Weak Subjectivity | Buterin | 2016 | ethereum.org blog | ethereum.org | PoS long-range defence | Checkpoints | Social consensus | LIKELY |
| 12 | The Sybil Attack | Douceur | 2002 | IPTPS | — | Sybil attack formalisation | Identity cost | No solution | LIKELY |
| 13 | CAP Theorem | Brewer | 2000 | PODC keynote | — | Consistency/Availability/Partition | Partitions | Trade-off only | LIKELY |
| 14 | Chia Network Proof of Space and Time | Chia Network | UNRESOLVED | UNRESOLVED | UNRESOLVED | Proof of Space/Time Sybil resistance | Storage cost | Source not confirmed | UNRESOLVED |

### 15.5 Source verification limitations

**No external web access was available during this verification task.** The following
limitations apply:

- **References could not be independently verified** against original publications,
  publisher websites, or DOI resolvers. All bibliographic metadata is recorded from
  training data and may contain errors.
- **DOIs marked "—" indicate no DOI was available in training data**, not that the
  publication lacks one. Each such reference requires independent verification.
- **Three references are marked UNCERTAIN or UNRESOLVED** and should be treated as
  placeholders requiring replacement with verified sources:
  - #8 Tendermint: Consensus without Mining (Buchman, 2016) — UNCERTAIN
  - #10 Narwhal and Tusk (Danezis, Nissenbaum, et al., 2022) — UNCERTAIN
  - #14 Chia Network Proof of Space and Time — UNRESOLVED (source not confirmed)
- **Three inline citation keys reference sources not included in the reference table:**
  - DiemBFTWhitepaper2020 — not in table
  - Petermann2020 (AlephBFT) — not in table
  - AvaWhitepaper2019 (Avalanche) — not in table
  These require either addition to the reference table or removal from the text.
- **Claim-to-source traceability:** Theoretical claims (FLP, 3f+1 bound, CAP theorem,
  quorum intersection) are traceable to references #1-4 and #12-13. Protocol-specific
  claims (message complexity, finality type, resilience) are traceable to references
  #5-11 but require independent verification against protocol specifications and
  published analyses. Economic and incentive claims (section 6) are qualitative
  assessments without specific source citations.

## 16. Recommended next investigation

1. **Commission deep research** on 2-3 candidate families (e.g., HotStuff-style BFT,
   Nakamoto PoW, PoS longest-chain) with full source review.
2. **Define the FST adversary model** explicitly (Byzantine threshold, network assumptions,
   adversary resources).
3. **Investigate Sybil-resistance mechanisms** in the context of FST's native asset (FST
   only, scope D-014).
4. **Determine finality requirements** for financial settlement (deterministic vs
   probabilistic, latency bounds).
5. **Study multi-transport consensus adaptation** for mesh, radio, satellite, and
   delay-tolerant networks.
6. **Resolve decision D-035** (independent client implementation count).
7. **Commission independent security review** of any selected consensus design before
   implementation.

All items remain OPEN RESEARCH. No design is approved.
