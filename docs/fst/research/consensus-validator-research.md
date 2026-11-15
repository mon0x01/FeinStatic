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

FST requires a permissionless consensus protocol for a globally distributed financial
network. Whether Byzantine fault tolerance is the appropriate security model is an open
research question under decision D-017; no consensus algorithm or BFT implementation has
been selected. The existing FeinStatic implementation uses Raft, which is crash-fault
tolerant and permissioned; it cannot be incrementally extended to meet FST's
requirements (principle P9, decision D-017, decision D-024). This research surveys
consensus theory, protocol families, validator membership, economics, network resilience,
security, formal verification, existing implementations, and a comparative evaluation.

A key conceptual contribution of this document is a **nine-component consensus taxonomy**
(Section 2): Sybil resistance, validator eligibility, validator selection, proposer/leader
selection, transaction dissemination, agreement and ordering, fork choice, finality, and
state-machine execution. These components may be integrated into one protocol or composed
from separate mechanisms. Blockchain family names (e.g., "Ethereum", "Cardano") conflate
layers that should be evaluated independently — for example, Proof of Work and Proof of Stake
are Sybil-resistance mechanisms and block-author selectors, not fork-choice algorithms
[EthereumConsensusDocs2024].

The strongest candidate architecture families for FST are classical BFT, leader-based BFT
(HotStuff-style), proof-of-work Nakamoto-style, proof-of-stake longest-chain, and DAG-based
mempool-consensus separation. Each involves trade-offs among permissionless entry, Byzantine
fault tolerance, finality type, bandwidth, latency, and decentralisation. No single family
satisfies all FST requirements without unresolved research in validator economics, Sybil
resistance, and partition tolerance.

## 2. Consensus architecture taxonomy

This section establishes a nine-component taxonomy for analyzing consensus architectures.
Each component addresses a distinct concern; a protocol may integrate several components or
delegate them to separate mechanisms.

### 2.1 Sybil resistance

**Question:** What prevents unlimited identities from controlling participation?

In a permissionless network, identity is cheap to create, so some costly or scarce resource
must gate participation [Douceur2002]. Candidates:

- **Proof of Work:** computational cost per block/identity [Nakamoto2008].
- **Proof of Stake:** token lockup as economic cost [Ouroboros2017; OuroborosPraos2017;
  Gasper2020].
- **Proof of Space/Time:** storage cost [ChiaProofOfSpaceAndTime — UNRESOLVED; source not
  confirmed; removed from factual claims pending verification].
- **Social graph / reputation:** not well-established for open financial networks.

Each mechanism has distinct centralisation and entry-barrier characteristics.

### 2.2 Validator eligibility

**Question:** Who is eligible to participate?

Eligibility defines the conditions under which an identity may become a validator (e.g.,
minimum stake, hardware requirements, registration mechanism). Eligibility is distinct from
selection: an eligible validator is not necessarily selected to propose or vote in any
given round.

### 2.3 Validator selection

**Question:** How is the active validator set or committee selected?

Selection determines which eligible validators participate in a given consensus instance.
Options include random sampling (sortition) from the eligible set, round-robin rotation,
stake-weighted selection, or hash-rate weighting (PoW). Random sampling requires a source
of randomness (e.g., verifiable random functions, beacon chains); the security assumptions
of the randomness mechanism require separate research.

### 2.4 Proposer / leader selection

**Question:** Who proposes the next block or consensus decision?

Proposer selection may coincide with validator selection (e.g., round-robin among the
active committee) or be separate (e.g., a PoW miner who solved the puzzle proposes the
next block). This component is distinct from agreement: a proposer orders transactions,
but agreement is still required among validators.

### 2.5 Transaction dissemination

**Question:** How do transactions and proposals reach participants?

Dissemination concerns how transactions propagate through the network before being
ordered. This is distinct from agreement: Narwhal, for example, handles dissemination via
a DAG while Tusk handles consensus separately [NarwhalTusk2022].

### 2.6 Agreement and ordering

**Question:** How do participants agree on a common sequence?

Agreement establishes the canonical order of transactions/blocks among participants. This
is the core consensus problem: BFT voting, Nakamoto longest-chain, HotStuff quorum
certificates, or Tusk asynchronous agreement are all agreement mechanisms.

### 2.7 Fork choice

**Question:** How do nodes select among competing histories?

Fork choice determines which chain or history is canonical when multiple candidates exist.
Examples: Nakamoto longest-chain (cumulative work), LMD-GHOST (GHOST-based weighted
fork choice), PBFT quorum-based single-chain. Fork choice is distinct from Sybil resistance
and from finality [EthereumConsensusDocs2024].

### 2.8 Finality

**Question:** When is a transaction considered irreversible under the protocol's assumptions?

Finality determines when a transaction can be considered settled. Deterministic finality
(PBFT, HotStuff, Casper FFG checkpoints) means a finalised block cannot be reverted under
the protocol's assumptions. Probabilistic finality (Nakamoto longest-chain, Ouroboros Praos)
means reversion becomes exponentially unlikely with depth but is never impossible.
FST's finality model is unresolved (decision D-017).

### 2.9 State-machine execution

**Question:** How are agreed transactions applied consistently?

Execution applies the agreed transaction order to the state machine. This is distinct from
agreement: consensus orders transactions; execution applies them deterministically so all
honest nodes converge on the same state.

### 2.10 Composition vs integration

These nine components may be:

- **Integrated** into a single protocol (e.g., PBFT integrates agreement, ordering,
  fork choice, and finality in one view-based protocol), or
- **Composed** from separate mechanisms (e.g., Narwhal for dissemination + Tusk for
  agreement; LMD-GHOST for fork choice + Casper FFG for finality in Gasper).

A fixed or known validator committee inside a BFT consensus instance does **not**
automatically require the entire network to be permissioned. A separate permissionless
eligibility and committee-selection mechanism may provide open network participation while
the BFT instance runs over a fixed committee for that epoch. This architecture is **not**
selected for FST; it is recorded as a composition option requiring further research.

## 3. Consensus theory fundamentals

### 3.1 Distributed agreement

Distributed agreement requires a set of participants to reach a common decision despite
failures. The fundamental impossibility result is the **FLP impossibility**
[FischerLynchPaterson1985]: in a fully asynchronous message-passing system, no deterministic
consensus protocol can guarantee termination (liveness) with even one crash failure, while
also preserving safety.

**Precise scope of FLP:** The impossibility applies to *deterministic* termination under
*fully asynchronous* timing with *crash* failures. It does **not** imply that randomized
asynchronous consensus is impossible, nor that consensus is impossible under partial
synchrony. FST must therefore rely on one of: (a) timing assumptions (partial synchrony or
synchrony) for liveness, (b) randomization for probabilistic termination under asynchrony,
or (c) accepting that liveness may be temporarily suspended during extended asynchrony.

### 3.2 Byzantine fault tolerance

Byzantine fault tolerance extends crash-fault tolerance to arbitrary (malicious) behaviour.

**The 3f+1 bound:** Classical Byzantine agreement (e.g., PBFT [CastroLiskov1999]) requires
at least 3f+1 replicas to tolerate f Byzantine faults, with quorums of 2f+1
[LamportShostakPease1982; CastroLiskov1999]. This bound assumes a **known, fixed replica
set** and PBFT's specific communication model. It is **not** universal: Nakamoto-style Proof
of Work uses an honest-majority-of-hash-rate assumption (<50% adversarial hash rate), and
Proof of Stake uses honest-majority-of-stake assumptions that vary by protocol. In a
permissionless setting with unknown participant count, the 3f+1 bound cannot be applied
directly; Sybil resistance must be layered on top, which changes the effective quorum model.

**Quorum intersection:** BFT safety is achieved through quorum intersection — any two quorums
share at least one honest participant [LamportShostakPease1982; CastroLiskov1999]. If two
conflicting decisions were made, the shared honest participant would have voted for both,
violating its protocol rules. This mechanism depends on the quorum construction and fault
model of each protocol.

### 3.3 Sybil attacks and open membership

A Sybil attack creates many identities to gain disproportionate influence [Douceur2002].
In a permissionless network, identity is cheap to create, so some costly or scarce resource
must gate participation. Without Sybil resistance, quorum intersection and Byzantine
thresholds are meaningless because an adversary can create unlimited identities to exceed
any threshold.

### 3.4 Safety and liveness

Safety: nothing bad happens (e.g., two conflicting blocks are never both finalised).
Liveness: something good eventually happens (e.g., new blocks are finalised).
FLP implies that under full asynchrony, deterministic safety and termination cannot both be
guaranteed [FischerLynchPaterson1985]. BFT protocols typically guarantee safety
unconditionally (under their fault bound) and liveness under partial synchrony
[DworkLynchStockmeyer1988].

### 3.5 Partial synchrony and synchrony assumptions

Partial synchrony [DworkLynchStockmeyer1988]: there exists an unknown bound on message
delay and processing time, but protocols cannot rely on it for safety. Most BFT protocols
assume partial synchrony for liveness. Pure synchrony assumes known bounds; pure asynchrony
assumes none.

### 3.6 PBFT synchrony assumptions

PBFT [CastroLiskov1999] provides safety under its Byzantine fault bound **without** relying
on eventual synchrony — safety is maintained as long as at most f of 3f+1 replicas are
Byzantine, regardless of message timing. PBFT liveness, however, requires partial
synchrony: if messages are delayed beyond the protocol's timeout bounds, liveness is
suspended until timing recovers. This distinction is specific to PBFT and must not be
generalised to every BFT protocol; each protocol has its own synchrony assumptions for
safety and liveness.

### 3.7 Network partitions and CAP

A partition splits the validator set into disconnected groups. The CAP theorem
[GilbertLynch2002] formally proves Brewer's conjecture: in an asynchronous network subject
to partitions, a system cannot simultaneously guarantee consistency (linearizability) and
availability (every request receives a non-error response).

**Important qualification:** CAP concerns *global linearizable decisions* and *availability
of every request*. It does **not** mean that no useful local operation can continue during
a partition. Bounded local offline financial transactions (pre-authorised spending limits,
reconciled later) operate under a different model than global consensus. FST's scope
distinguishes these modes (Section 3.13). CAP does not imply that local autonomy is
impossible during partitions — only that global linearizability and full availability cannot
both hold.

### 3.8 Message delays, loss and reordering

Networks may delay, drop, or reorder messages. Consensus protocols must handle this via
retransmission, timeouts, sequence numbers, and view changes. Unbounded message delay breaks
liveness under synchrony assumptions.

### 3.9 Byzantine participants and collusion

Byzantine participants may deviate arbitrarily from the protocol, including colluding,
equivocating (sending conflicting messages to different peers), and withholding messages.
Collusion among f Byzantine validators can prevent liveness (by withholding votes) or
attempt safety violations (by equivocating), subject to the protocol's fault bound
(e.g., 3f+1 for classical BFT, honest-majority assumptions for Nakamoto/PoS).

### 3.10 Forks and conflicting histories

A fork occurs when the network diverges into multiple chains. Deterministic finality
prevents forks after finalisation; probabilistic finality (Nakamoto-style) makes forks
increasingly unlikely with depth but never impossible. FST's fork-protection requirements
(decision D-036) require unique network identities, replay protection, and distinct currency
identifiers for any independent fork.

### 3.11 Deterministic and probabilistic finality

Deterministic finality: once a block is finalised, it cannot be reverted (classical BFT and
BFT-style protocols with checkpoint finality). Probabilistic finality: reversion becomes
exponentially unlikely with confirmation depth but is never impossible (Nakamoto-style).
FST's finality model is unresolved (decision D-017). The project owner has **not** approved
deterministic finality as a universal requirement; both models remain evaluation candidates.

### 3.12 Consensus recovery after network disruption

After a partition or disruption, the protocol must recover liveness when a quorum of honest
validators reconnects. Recovery mechanisms include view changes, leader rotation, state
synchronisation, and checkpointing.

### 3.13 Four distinct operation modes

FST's scope distinguishes four modes that must not be conflated:

1. **Global blockchain consensus:** all validators agree on a single canonical chain.
2. **Local regional consensus:** a subset of validators agrees locally during disconnection.
3. **Offline transactions under pre-authorised spending limits:** local spending authority
   with bounded risk, reconciled later.
4. **Global settlement after connectivity returns:** reconciling local operations into the
   global chain.

Local offline continuity is **not** equivalent to global consensus (scope-v0.1.md,
Offline financial operation). CAP and FLP constraints on global consensus do not
automatically preclude bounded local operation during disconnection.

## 4. Permissionless consensus requirements

Based on FST's approved principles (P1, P2, P4, P5, P6, P7, P8, P9, P10) and scope:

- **Permissionless participation:** any independent participant may join validation without
  approval from a central authority (decision D-016).
- **No permanent privileged operator:** no central administrator or master key (P8).
- **Byzantine resilience research direction:** safety under explicitly bounded malicious
  behaviour is an important research direction (decision D-017); P9 states that security
  should primarily emerge from protocol design and independent verification, but does not
  mandate Byzantine fault-tolerant consensus.
- **Upgrade rejection:** participants may reject protocol upgrades (P5).
- **Fork legitimacy:** no fork has exclusive legitimacy (P6, P7).
- **Founder independence:** the network survives without its founding team (P10).
- **Multi-transport communication:** internet, mesh, radio, satellite, delay-tolerant
  (decision D-019).
- **Offline operation:** bounded local spending authority and eventual reconciliation
  (decision D-018).

The exact consensus algorithm, Sybil-resistance mechanism, validator economics, and
finality model remain open research (scope-v0.1.md, Decentralisation).

## 5. Consensus family comparisons

### 5.1 Classical PBFT

**Examples:** PBFT [CastroLiskov1999]. Paxos variants are crash-fault tolerant only
[Lamport2001] and are not Byzantine.

**Validator-set assumptions:** fixed, known replica set; permissioned membership within the
BFT instance. PBFT does not itself provide permissionless network admission.

**Agreement and ordering:** view-based primary-backup protocol with prepare and commit
phases.

**Fork choice:** PBFT maintains a single chain; conflicting views are resolved through the
view-change mechanism, not through competing chain selection.

**Finality:** deterministic under the 3f+1 fault bound [CastroLiskov1999].

**Message complexity:** O(n²) per view [CastroLiskov1999]. This counts the all-to-all voting
communication in the prepare and commit phases: each replica broadcasts its vote to all
others, and replicas collect 2f+1 votes before proceeding. The exact count depends on what
is being counted (messages, signatures, bytes, or rounds).

**Sybil resistance:** none intrinsic; requires external mechanism.

**FST relevance:** PBFT provides strong safety and deterministic finality but requires
permissioned membership within the BFT instance. Adapting it to permissionless entry
requires a Sybil-resistance layer, which changes the quorum model.

### 5.2 Leader-based BFT protocols

#### HotStuff

**Source:** "HotStuff: BFT Consensus with Linearity and Responsiveness" by Yin, Malkhi,
Reiter, et al., PODC 2019 [HotStuff2019].

**Validator-set assumptions:** fixed replica set per view; permissioned within the BFT
instance.

**Agreement and ordering:** leader-driven protocol with quorum certificates (aggregated
signatures) per view.

**Fork choice:** single-chain via view changes; no competing chain selection.

**Finality:** deterministic under the fault bound [HotStuff2019].

**Communication complexity:** O(n) communication per view via quorum certificates
[HotStuff2019]. This counts the communication of aggregated quorum certificates per view,
which is linear in the number of replicas. The O(n) claim assumes quorum certificate
aggregation; without aggregation, the raw message count differs. View-change complexity
claims beyond per-view communication must be verified against the paper and its pipelined
variants (Jolting, Chained-HotStuff) before being stated as fact.

**Sybil resistance:** none intrinsic; requires external stake/token mechanism.

**FST relevance:** Leader-based BFT offers deterministic finality and linear per-view
communication (HotStuff). Permissionless entry requires staking, which introduces economic
centralisation risks.

#### Tendermint / CometBFT

Two distinct sources must be distinguished:

- **Original Tendermint draft:** Jae Kwon, 2014, outdated draft v0.6
  [TendermintDraft2014]. This is a historical draft, not the current specification.
- **Current CometBFT consensus specification:** maintained by the CometBFT project
  [CometBFTConsensusSpec2024]. CometBFT is a fork of Tendermint Core; the specification
  represents the current implementation, not the 2014 draft.

**Validator-set assumptions:** fixed validator set per height with voting power weights;
permissioned within the BFT instance. The CometBFT specification shows a round-based
Propose, Prevote and Precommit process, with voting power and more-than-two-thirds
thresholds [CometBFTConsensusSpec2024]. This specification is **not** a complete
permissionless validator-admission mechanism.

**Agreement and ordering:** round-based leader protocol with prevote/precommit voting.

**Fork choice:** single-chain via lock-change rules; no competing chain selection.

**Finality:** deterministic under more-than-two-thirds voting power [CometBFTConsensusSpec2024].

**Communication complexity:** PBFT-style voting communication per block
[CastroLiskov1999; TendermintDraft2014]. The exact per-block message count in CometBFT's
current implementation requires inspection of [CometBFTConsensusSpec2024] and is not stated
here as a verified deployment measurement.

**Sybil resistance:** none intrinsic; requires external stake/token mechanism.

**FST relevance:** Leader-based BFT with deterministic finality. Communication complexity
claims require source inspection before use in FST evaluation.

#### DiemBFT

**Source:** Diem State Machine Replication / DiemBFT technical paper [DiemBFT2020].

**Validator-set assumptions:** fixed validator set per epoch with epoch changes and
reconfiguration; permissioned within the BFT instance.

**Agreement and ordering:** HotStuff-based protocol with epoch-based reconfiguration
[DiemBFT2020].

**Finality:** deterministic under the fault bound [DiemBFT2020].

**FST relevance:** Provides a reference for HotStuff-based protocols with epoch-based
reconfiguration, which is relevant to permissionless validator entry/exit.

### 5.3 Nakamoto-style Proof of Work

**Examples:** Bitcoin [Nakamoto2008].

**Sybil resistance:** Proof of Work (hash rate cost) [Nakamoto2008].

**Validator eligibility:** any participant with hash rate can mine.

**Validator selection:** the miner who solves the puzzle becomes the proposer.

**Proposer selection:** same as validator selection (the puzzle solver proposes).

**Transaction dissemination:** gossip-based transaction propagation.

**Agreement and ordering:** longest-chain rule with cumulative work.

**Fork choice:** longest chain (cumulative work) [Nakamoto2008].

**Finality:** probabilistic; k-block confirmation depth. Reversion becomes exponentially
unlikely with depth but is never impossible [Nakamoto2008].

**Resilience:** tolerates an adversary controlling <50% of the hash rate (honest-majority
of hash rate assumption) [Nakamoto2008]. This threshold is specific to the PoW model and
does not transfer to BFT or PoS protocols.

**FST relevance:** Nakamoto-style consensus is genuinely permissionless and has a long
deployment track record (Bitcoin). However, probabilistic finality, high energy cost, mining
centralisation risk, and slow settlement (minutes to hours) are significant considerations
for a financial network.

### 5.4 Ouroboros family

The Ouroboros family must be understood as distinct protocols with different assumptions:

#### Ouroboros (CRYPTO 2017)

**Source:** "Ouroboros: A Provably Secure Proof-of-Stake Blockchain Protocol"
[Ouroboros2017].

**Sybil resistance:** stake (token ownership) [Ouroboros2017].

**Agreement and ordering:** stake-weighted slot leader election and chain selection.

**Finality:** probabilistic with settlement delay; specific security guarantees depend on
the protocol variant [Ouroboros2017].

**Synchrony and security assumptions:** semi-synchronous model with specific adversarial
boundaries; exact assumptions require inspection of the full paper.

#### Ouroboros Praos

**Source:** "Ouroboros Praos: An adaptively-secure, semi-synchronous proof-of-stake
blockchain" [OuroborosPraos2017].

**Sybil resistance:** stake (token ownership) [OuroborosPraos2017].

**Agreement and ordering:** stake-weighted slot leader election with VRF-based leader
selection and chain selection.

**Finality:** probabilistic finality with settlement delay [OuroborosPraos2017].

**Synchrony and security assumptions:** adaptively-secure, semi-synchronous model; exact
parameters require inspection of the ePrint record [OuroborosPraos2017].

**FST relevance:** Staking-based protocols align with FST's native asset but introduce
wealth-concentration and centralisation risks. The honest-stake threshold must be
evaluated protocol-by-protocol, not generalized across Ouroboros, Praos, and Gasper.

### 5.5 Gasper-style Proof of Stake

**Source:** "Combining GHOST and Casper" (Gasper) [Gasper2020].

**Complete author list:** Vitalik Buterin, Diego Hernandez, Thor Kamphefner, Khiem Pham,
Zhi Qiao, Danny Ryan, Juhyeok Sin, Ying Wang and Yan X. Zhang [Gasper2020].

**Sybil resistance:** stake (token ownership) via Casper FFG [Gasper2020].

**Validator eligibility:** any participant who deposits stake can become a validator.

**Validator selection:** committee-based selection from staked validators.

**Proposer selection:** proposer assigned per slot from the validator set.

**Transaction dissemination:** gossip-based propagation through the P2P layer.

**Agreement and ordering:** LMD-GHOST fork-choice rule combined with Casper FFG finality
gadget [Gasper2020].

**Fork choice:** LMD-GHOST (GHOST-based weighted fork choice) [Gasper2020].

**Finality:** deterministic checkpoint finality via Casper FFG, qualified by Byzantine-stake
and slashing assumptions [Gasper2020; CasperFFG2017]. Fork choice and checkpoint finality
are distinct mechanisms; finality is **not** unconditional irreversibility — it is qualified
by the protocol's Byzantine-stake threshold, slashing conditions, and weak subjectivity
assumptions [CasperFFG2017; EthereumWeakSubjectivity2024].

**Weak subjectivity:** checkpoints and weak subjectivity provide defence against long-range
attacks [EthereumWeakSubjectivity2024].

**FST relevance:** Gasper separates fork choice (LMD-GHOST) from finality (Casper FFG),
demonstrating the component composition described in Section 2. Staking-based protocols
introduce wealth-concentration and centralisation risks; economic security depends on token
value, which is circular for a new network.

### 5.6 DAG-based and asynchronous consensus

#### Narwhal and Tusk

**Source:** "Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus" by George
Danezis, Lefteris Kokoris-Kogias, Alberto Sonnino, Alexander Spiegelman, EuroSys 2022,
pages 34–50 [NarwhalTusk2022].

Official repository record:
https://discovery.ucl.ac.uk/id/eprint/10172331/

**Three distinct components:**

- **Narwhal:** A transaction dissemination and causal-history DAG. Narwhal separates
  transaction dissemination from consensus, using a DAG where each transaction references
  prior transactions, creating a causal history. Narwhal provides availability and
  dissemination but not consensus itself.
- **Narwhal-HotStuff:** Narwhal combined with partially synchronous HotStuff. This
  combination inherits HotStuff's synchrony assumptions for liveness.
- **Tusk:** An asynchronous BFT consensus protocol designed to work with Narwhal. Tusk
  uses the Narwhal DAG as its underlying data structure but provides consensus under
  asynchronous assumptions. Tusk uses randomized consensus and does **not** lose liveness
  under all asynchronous conditions — it uses randomization to circumvent FLP.

**Communication complexity:** Narwhal disseminates transactions via the DAG; the
per-transaction communication cost depends on the DAG dissemination pattern, not simply
"linear per transaction" in all cases. Tusk's consensus cost is separate from Narwhal's
dissemination cost. This document does not assign a single linear-per-transaction
complexity to the combined system without distinguishing dissemination from consensus
costs [NarwhalTusk2022].

**Liveness assumptions:** Tusk is designed for asynchronous consensus and uses randomization
to achieve probabilistic termination under asynchrony. It does not require partial synchrony
for liveness in the same way that PBFT or HotStuff do. However, randomized termination is
probabilistic, not deterministic.

**Sybil resistance:** none intrinsic; requires external stake/token mechanism.

**FST relevance:** Narwhal/Tusk separates dissemination from consensus, which may be
relevant for multi-transport networks where dissemination and consensus have different
communication requirements. Tusk's asynchronous consensus with randomization addresses
FLP constraints differently than partial-synchrony-based BFT.

#### AlephBFT

**Source status:** UNRESOLVED. No authoritative source record was provided that permits
confirmation of title, authors, date, venue, or specific protocol claims. The citation
[Petermann2020] from the previous draft is **removed** because its source could not be
established. AlephBFT is mentioned only as a protocol family name without factual claims
pending source verification.

#### Avalanche-family

**Source status:** UNRESOLVED. No authoritative source record was provided that permits
confirmation of title, authors, date, venue, or specific protocol claims. The citation
[AvaWhitepaper2019] from the previous draft is **removed** because its source could not be
established.

**Important correction:** The previous draft categorically classified Avalanche-style
probabilistic safety as unsuitable for FST. This classification is **withdrawn**. The project
owner has **not** approved deterministic finality as a universal requirement (decision D-017
remains OPEN RESEARCH). Avalanche-family sources remain UNRESOLVED in RS-013; no technical
properties (probabilistic safety, finality guarantees, or relative guarantees compared with
deterministic BFT checkpoint finality) can be asserted without an authoritative source and
subsequent technical inspection. Avalanche-family protocols remain a name-level research
lead pending source verification.

**FST relevance:** Avalanche-family protocols are recorded as an unresolved research
candidate only. Protocol-specific evaluation requires an authoritative primary source before
any safety, liveness, or finality claims can be made.

## 6. Validator membership and Sybil resistance

### 6.1 Validator identity

Permissionless validators need cryptographic identities (public keys). Identity creation
must be cheap to encourage participation but expensive enough to prevent Sybil attacks
when combined with a Sybil-resistance resource [Douceur2002].

### 6.2 Sybil resistance

Without Sybil resistance, an adversary can create unlimited identities and exceed any
Byzantine threshold [Douceur2002]. Mechanisms:

- **Proof of Work:** computational cost per identity/block [Nakamoto2008].
- **Proof of Stake:** token lockup as economic cost [Ouroboros2017; OuroborosPraos2017;
  Gasper2020]. Each PoS protocol has distinct economic assumptions; no universal threshold.
- **Proof of Space/Time:** storage cost [ChiaProofOfSpaceAndTime — UNRESOLVED; source not
  confirmed; removed from factual claims pending verification].
- **Social graph / reputation:** not well-established for open financial networks.

Each mechanism has distinct centralisation and entry-barrier characteristics.

### 6.3 Validator selection

Selection determines who proposes and votes. Options:
- Random sampling (sortition) from eligible set.
- Round-robin rotation.
- Stake-weighted selection.
- Hash-rate weighted (PoW).

Random sampling with stake weighting reduces centralisation but requires a source of
randomness (verifiable random functions, beacon chains). The specific randomness mechanism
and its security assumptions require separate research.

### 6.4 Stake-based participation

Validators lock tokens as collateral. Misbehaviour can be slashed. Stake concentration
leads to centralisation; delegation to pools amplifies this. Slashing conditions and
detection windows vary by protocol [CasperFFG2017; Gasper2020].

### 6.5 Mining-based participation

Miners invest in hardware and energy. Pool centralisation is a known issue in PoW
networks. This is an observed deployment characteristic, not a protocol guarantee.

### 6.6 Delegation and staking pools

Delegation lowers entry barriers but concentrates power in pool operators. This is a
known centralisation vector in PoS networks. The degree of concentration depends on
protocol design and economic parameters.

### 6.7 Validator entry and exit

Permissionless entry requires on-chain registration or implicit entry (e.g., mining).
Exit requires unbonding periods to prevent stake withdrawal before slashing can be
applied. Long unbonding periods reduce capital efficiency; short periods weaken security.
These are design trade-offs, not protocol-mandated values.

### 6.8 Bonding and unbonding

Bonding locks stake for validation. Unbonding releases it after a delay. The delay must
exceed the slashing detection window [CasperFFG2017; Gasper2020]. Exact values are
protocol-specific.

### 6.9 Slashing

Slashing penalises misbehaviour (double-signing, offline time). It requires evidence
collection and a slashing mechanism [CasperFFG2017; Gasper2020]. Slashing introduces
centralisation risk if a small group decides what counts as misbehaviour; this is a
governance design question, not a protocol certainty.

### 6.10 Validator rotation

Rotating validators limits the window in which a corrupted validator set can cause harm.
Rotation must be frequent enough for security but infrequent enough for stability. These
are design trade-offs.

### 6.11 Long-range attacks

In PoS, an adversary who previously controlled stake can rewrite history from an old
checkpoint ("long-range attack"). Defences include checkpoints, weak subjectivity, and
finality gadgets [EthereumWeakSubjectivity2024; CasperFFG2017].

### 6.12 Nothing-at-stake attacks

In PoS, validators can vote on multiple chains at no direct cost (unlike PoW, where mining
on multiple chains splits hash rate). Finality gadgets and slashing address this
[CasperFFG2017; Gasper2020]. This is a protocol-design consideration, not a universal
PoS vulnerability — some designs mitigate it natively.

### 6.13 Stake concentration

A small number of large stakeholders can dominate validation, reducing decentralisation
and increasing collusion risk. This is an economic centralisation risk, not a protocol
guarantee or failure.

### 6.14 Wealth concentration

Token wealth may concentrate over time through rewards (rich-get-richer dynamics),
compounding stake concentration. [IrresbergerYang2023] studies coin concentration of
Proof-of-Stake blockchains and does **not** find evidence of disproportionate wealth
compounding for large stakes in the systems examined. This finding is study-specific and
does **not** establish a universal tendency for every PoS system. Wealth concentration
remains an economic hypothesis and design concern, not an observed universal law.
(Economic hypothesis)

### 6.15 Validator collusion

Validators may collude to censor transactions, reorg history, or extract value. Collusion
resistance requires economic cost and geographic/infrastructure diversity. This is a
threat-model consideration, not a protocol-measured property.

### 6.16 Governance capture

If governance is stake-weighted, wealthy stakeholders may control protocol upgrades,
potentially capturing the network. This is a governance-design risk.

### 6.17 Geographic and infrastructure concentration

Validators concentrated in few jurisdictions or cloud providers create single points of
control and failure. This is an operational centralisation risk.

### 6.18 Participation role distinctions

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
decentralisation. This is an empirical question requiring deployment data.

## 7. Validator economics

**Evidence classification used in this section:**
- **Proven result:** Established in published research or formal analysis.
- **Observed deployment phenomenon:** Documented in live networks (e.g., Ethereum, Bitcoin).
- **Economic hypothesis:** Reasoned but not empirically validated at scale.
- **Design concern:** Identified risk requiring mitigation, not a measured outcome.

### 7.1 Initial token allocation

Initial distribution affects early centralisation. Fair launch, pre-mine, and ICO models
have different concentration profiles. These are design choices with observed but not
universally quantified effects. (Design concern)

### 7.2 Validator rewards

Rewards incentivise participation but increase inflation. Reward design affects
centralisation (e.g., economies of scale may favour large validators). This is a
design trade-off, not a protocol guarantee. (Economic hypothesis)

### 7.3 Transaction fees

Fees pay for block space and can prioritise transactions. Fee markets may centralise
block production through MEV. This is an observed deployment phenomenon in Ethereum
and similar systems, documented in [DaianEtAl2020]. (Observed deployment phenomenon)

### 7.4 Inflation and issuance incentives

Inflation funds security but dilutes holders. The relationship between token value and
security is circular for a new network: security budget depends on token value, and token
value may depend on perceived security. (Economic hypothesis)

### 7.5 Proof-of-stake economic security

Economic security = cost to attack ≈ value of stake at stake. Requires token value to be
high enough to make attacks uneconomic. This relationship is protocol-specific and
economically circular for a new network [CasperFFG2017; Gasper2020]. (Economic hypothesis)

### 7.6 Proof-of-work operating costs

PoW security ≈ cost of hardware + energy. The security-is-bounded-by-attack-cost model is
described in [Nakamoto2008]. Environmental and centralisation concerns are observed
deployment phenomena from later analysis, not claims in the original paper.
(Proven result — security model from [Nakamoto2008]; environmental/centralisation concerns
are observed deployment phenomena.)

### 7.7 Slashing and punishment

Slashing deters misbehaviour but requires accurate detection and fair application
[CasperFFG2017; Gasper2020]. Inaccurate or politically motivated slashing is a governance
risk. (Design concern)

### 7.8 Delegation and pooled validation

Pools reduce entry barriers but concentrate voting power. The degree of concentration
depends on pool design and delegation mechanics. (Observed deployment phenomenon in PoS
networks)

### 7.9 Validator profitability

Profitability determines participation. Low profitability drives validators out, reducing
security. This is an economic feedback loop, not a protocol guarantee. (Economic hypothesis)

### 7.10 Attack costs

The economic cost of attacking the network must exceed the potential gain. This depends on
total stake/hash rate and token value. Quantifying this requires protocol-specific economic
modeling. (Design concern)

### 7.11 Censorship incentives

Validators may censor transactions for profit or coercion. Mempool privacy and
proposer-separator designs address this. These are design mitigations, not protocol
guarantees. (Design concern)

### 7.12 Extractable value and transaction ordering

MEV (Maximal Extractable Value) arises from controlling transaction ordering. [DaianEtAl2020]
documents frontrunning, transaction reordering, and consensus instability in decentralized
exchanges on Ethereum, including priority gas auctions and MEV-related consensus risks.
These findings are specific to the Ethereum context studied and should not be generalized
to all blockchain systems; the claim that MEV centralises block production is not
universally established. (Observed deployment phenomenon — Ethereum-specific)

### 7.13 Long-term network sustainability

Security budget must remain adequate as rewards decrease (if issuance is capped). Fee
markets must sustain validator income. This is a long-run economic question. (Economic
hypothesis)

### 7.14 Monetary-consensus interdependence

Monetary policy (supply, issuance, rewards) and consensus (Sybil resistance, security
budget) are interdependent:
- Consensus determines what resource secures the network (work, stake, etc.).
- Monetary policy determines the value and issuance of that resource.
- Neither can be designed in isolation.

FST monetary policy is unresolved (decision D-015). Consensus selection should not
presuppose a monetary policy. (Design concern)

### 7.15 Peer-reviewed evidence on PoS concentration

[IrresbergerYang2023] studies coin concentration of Proof-of-Stake blockchains,
documenting observed block-production concentration and validator entry/exit dynamics.
This study provides empirical evidence for specific PoS systems studied; it does **not**
establish a universal law for every PoS system, nor does it determine FST's validator
economics. (Observed deployment phenomenon — study-specific)

## 8. Network partition and communication resilience

### 8.1 Consensus message complexity

PBFT: O(n²) per view [CastroLiskov1999] — counts all-to-all voting in prepare and commit
phases. HotStuff: O(n) communication per view via quorum certificates [HotStuff2019] —
counts quorum certificate communication (aggregated signatures) per view. Nakamoto:
O(n) block propagation [Nakamoto2008]. Tendermint/CometBFT: PBFT-style voting communication
per block [CastroLiskov1999; TendermintDraft2014] (exact constants require current spec
inspection). Narwhal/Tusk: Narwhal's DAG dissemination cost and Tusk's consensus cost are
distinct; this document does not assign a single linear-per-transaction complexity to the
combined system [NarwhalTusk2022] (paper result, deployment-dependent).

**Qualification:** These are protocol-paper results or design characteristics. Measured
deployment throughput, latency, and bandwidth vary by implementation, network conditions,
and validator count. No quantitative scores are claimed. Asymptotic notation does not
infer deployment performance.

### 8.2 Bandwidth requirements

BFT protocols require validators to exchange votes on every block. High validator counts
increase bandwidth quadratically (PBFT) or linearly per view (HotStuff). These are design
characteristics, not deployment measurements. No fixed per-transaction bandwidth figure
is assigned without source-specific derivation.

### 8.3 Latency sensitivity

BFT finality requires multiple communication rounds. Latency scales with round count and
network delay. Exact round counts and latency are protocol- and deployment-specific. No
fixed latency ranges (e.g., "2-4 rounds", "seconds to minutes") are assigned without
source-specific validation.

### 8.4 Validator communication patterns

All-to-all (PBFT), leader-centric (Tendermint/HotStuff), gossip (Avalanche-family —
protocol-specific, unverified), DAG broadcast (Narwhal/Tusk [NarwhalTusk2022]).

### 8.5 Quorum availability

During partitions, quorums may be unreachable. Liveness is sacrificed until reconnection
[DworkLynchStockmeyer1988; CastroLiskov1999]. Safety is preserved under the protocol's
fault bound during partitions.

### 8.6 Synchronization and state transfer

New or rejoining validators must synchronise state. Snapshots and state proofs reduce
sync time. FeinStatic's snapshot-transfer engineering (v2.0.0/v2.1.0) is relevant
reference material for state transfer mechanics, though its consensus model differs from
FST's target.

### 8.7 Recovery after partitions

View changes, leader rotation, and state sync enable recovery. Recovery time depends on
validator count and state size.

### 8.8 Validator geographic distribution

Geographic diversity improves partition resilience but increases latency (speed of light).
This is a physical constraint, not a protocol choice.

### 8.9 Performance degradation under packet loss

Packet loss increases timeouts and retransmissions, degrading liveness. The degree of
degradation is protocol- and implementation-specific.

### 8.10 Multi-transport compatibility

FST requires internet, mesh, radio, satellite, and delay-tolerant networks (decision
D-019). BFT protocols assume reliable point-to-point channels; adaptation to store-and-forward
or intermittent links is open research. No consensus protocol can guarantee global progress
during arbitrary prolonged partitions while preserving safety (CAP [GilbertLynch2002] and
FLP [FischerLynchPaterson1985]).

## 9. Security assumptions and attack analysis

### 9.1 Adversary model

FST must define: number of Byzantine validators, network synchrony, adversary resources
(hash rate, stake, bandwidth), and adversary goals (double-spend, censorship, liveness
denial). The adversary model is unresolved (decision D-017).

### 9.2 Byzantine fault threshold

Classical BFT: f of 3f+1 (~33%) under partial synchrony with known participant set
[CastroLiskov1999; LamportShostakPease1982]. Nakamoto PoW: <50% hash rate
[Nakamoto2008]. PoS: honest-stake-majority assumptions that vary by protocol
[Ouroboros2017; OuroborosPraos2017; Gasper2020]. These thresholds are **not** universal;
each depends on its Sybil-resistance and synchrony assumptions.

### 9.3 Safety guarantees

BFT: deterministic safety under quorum intersection and the protocol's fault bound
[CastroLiskov1999]. Nakamoto: probabilistic safety (reorg depth k) [Nakamoto2008]. Gasper:
deterministic checkpoint finality qualified by Byzantine-stake and slashing assumptions
[Gasper2020; CasperFFG2017].

### 9.4 Liveness assumptions

BFT: partial synchrony, quorum connectivity [DworkLynchStockmeyer1988; CastroLiskov1999].
Nakamoto: honest-majority hash rate [Nakamoto2008]. PoS: protocol-specific honest-stake
and network assumptions [OuroborosPraos2017; Gasper2020]. Tusk: randomized probabilistic
termination under asynchrony [NarwhalTusk2022].

### 9.5 Sybil-resistance assumptions

PoW: adversary cannot acquire >50% hash rate [Nakamoto2008]. PoS: adversary cannot acquire
protocol-specific honest-stake majority [Ouroboros2017; Gasper2020]. Both require the
underlying resource to be costly and decentralised [Douceur2002].

### 9.6 Cryptographic assumptions

Digital signatures (Ed25519, BLS), hash functions (SHA-256), VRFs, threshold signatures.
FeinStatic uses Ed25519 and X25519/HKDF/AES-GCM; reuse requires cryptographic review
(decision D-023). These are implementation considerations, not consensus guarantees.

### 9.7 Economic security assumptions

Attack cost > attack gain. Depends on token value, total stake/hash rate, and slashing.
This is an economic modeling question, not a protocol guarantee.

### 9.8 Network synchrony assumptions

Partial synchrony for most BFT [DworkLynchStockmeyer1988]. Asynchrony sacrifices liveness
guarantees for deterministic protocols [FischerLynchPaterson1985]; randomized protocols
may achieve probabilistic termination under asynchrony [NarwhalTusk2022].

### 9.9 Known attacks

- Double-spend (Nakamoto reorg [Nakamoto2008], BFT equivocation [CastroLiskov1999])
- Long-range attack (PoS [EthereumWeakSubjectivity2024; CasperFFG2017])
- Nothing-at-stake (PoS [CasperFFG2017; Gasper2020])
- Selfish mining (PoW — observed deployment phenomenon)
- Eclipse attack (P2P layer — network-level attack)
- DDoS on leaders (leader-based BFT — operational attack surface)
- Censorship (validator collusion — economic/governance risk)
- MEV extraction (ordering control — observed deployment phenomenon [DaianEtAl2020])

### 9.10 Formal proofs

Many BFT protocols have published safety proofs (PBFT [CastroLiskov1999], HotStuff
[HotStuff2019], Tendermint/CometBFT — spec-dependent). Nakamoto-style analysis is more
heuristic (simplified models) [Nakamoto2008]. Paper guarantees ≠ implementation security.

### 9.11 Known implementation vulnerabilities

Production blockchain implementations have had consensus bugs. Paper guarantees do not
equal implementation security. Specific vulnerabilities require deployment-specific
investigation; this document does not enumerate them.

### 9.12 Operational attack surfaces

P2P layer, node software, key management, upgrades, governance. These are operational
considerations, not consensus properties.

## 10. Formal verification opportunities

- Model checking (TLA+, PlusCal) for consensus safety/liveness.
- Interactive theorem proving (Coq, Isabelle) for protocol correctness.
- Adversarial simulation (network faults, Byzantine behaviour).
- Fuzzing of consensus implementations.
- Reproducible test networks (chaos testing, partition testing).

Formal verification increases confidence but does not eliminate implementation risk.
Verification targets are protocol-specific and require implementation artifacts.

## 11. Existing implementations

**Examine without copying:**

- **Bitcoin Core:** PoW Nakamoto-style, long deployment, extensive test network
  [Nakamoto2008 — implementation reference].
- **CometBFT (formerly Tendermint):** BFT, deterministic finality, used in Cosmos SDK
  [CometBFTConsensusSpec2024; TendermintDraft2014 — historical draft].
- **Ethereum consensus clients:** Gasper (LMD-GHOST + Casper FFG), multiple independent
  implementations (Prysm, Lighthouse, Teku, Nimbus, Lodestar)
  [Gasper2020; CasperFFG2017; EthereumWeakSubjectivity2024].
- **Cardano:** Ouroboros family, PoS longest-chain [Ouroboros2017; OuroborosPraos2017].
- **AlephBFT / Subspace:** DAG-based / asynchronous variants — source status UNRESOLVED.
- **Avalanche:** Avalanche consensus family — source status UNRESOLVED; retained as
  evaluation candidate (see Section 5.6).

**What to learn:** protocol specifications, node architecture, validator software, test
networks, test vectors, state synchronisation, upgrade mechanisms, operational requirements,
known limitations.

**Protocol specifications must remain independent of any particular implementation.**

## 12. Detailed comparison matrix

| Criterion | Classical BFT (PBFT) | Leader BFT (HotStuff) | Leader BFT (Tendermint/CometBFT) | Nakamoto PoW | PoS Longest-Chain (Ouroboros Praos) | PoS Checkpoint Finality (Gasper) | DAG-Based (Narwhal/Tusk) |
|-----------|----------------------|----------------------|----------------------------------|--------------|--------------------------------------|-----------------------------------|--------------------------|
| Sybil resistance | None intrinsic | None intrinsic | None intrinsic | Proof of Work | Stake | Stake | None intrinsic |
| Validator-set assumptions | Fixed replica set | Fixed replica set | Fixed validator set with voting power | Open (any miner) | Stake-weighted eligible set | Stake-weighted eligible set | None intrinsic |
| Permissionless network admission | No | No | No | Yes | Yes | Yes | No |
| Agreement & ordering | View-based prepare/commit | Leader + quorum certificates | Round-based prevote/precommit | Longest-chain | Slot leader + chain selection | LMD-GHOST fork choice | Tusk async agreement |
| Fork choice | Single chain (view changes) | Single chain (view changes) | Single chain (lock-change) | Longest chain (cumulative work) | Chain selection | LMD-GHOST | DAG-based |
| Finality | Deterministic [PBFT] | Deterministic [HotStuff] | Deterministic [CometBFT] | Probabilistic [PoW] | Probabilistic + settlement delay [Praos] | Deterministic checkpoint finality [Gasper] | Randomized probabilistic |
| Safety assumptions | Quorum intersection, no synchrony for safety [PBFT] | Partial synchrony, quorum intersection [HotStuff] | No synchrony for safety, >2/3 voting power [CometBFT] | Honest majority hash [PoW] | Honest stake + semi-synchrony [Praos] | Byzantine stake + slashing + weak subjectivity [Gasper] | Partial synchrony / asynchrony variants |
| Liveness assumptions | Partial synchrony [PBFT] | Partial synchrony [HotStuff] | Partial synchrony, leader rotation | Honest majority hash | Honest stake, network synchrony | Honest stake, network synchrony | Randomized probabilistic termination |
| Communication complexity | O(n²) per view [PBFT] | O(n) per view via QC [HotStuff] | PBFT-style voting [Tendermint] | O(n) block propagation | Protocol-specific [Praos] | Protocol-specific [Gasper] | Dissemination separate from consensus [Narwhal/Tusk] |
| Decentralisation | Permissioned | Permissioned (Sybil-layer needed) | Permissioned (Sybil-layer needed) | Mining pools concentrate | Stake concentration, pools concentrate | Stake concentration, pools concentrate | Depends on Sybil layer |
| Offline compatibility | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) | Not provided by consensus layer; separate bounded offline-payment architecture may be composed (D-018) |

*Note: Qualitative assessment only. No quantitative scores invented. Protocol-specific
assumptions and deployment results vary. Fixed latency ranges are omitted because they
cannot be supported without source-specific validation. Bandwidth figures are omitted
because they cannot be derived from asymptotic notation alone. AlephBFT and Avalanche-family
are excluded from this matrix because their sources could not be verified (UNRESOLVED).*

**Matrix qualification:** This matrix separates Sybil resistance from agreement and
finality, and separates validator-set assumptions from permissionless network admission.
Ouroboros Praos and Gasper are listed separately because they have distinct finality
mechanisms (probabilistic settlement delay vs deterministic checkpoint finality).
Narwhal dissemination and Tusk consensus are not combined into a single complexity claim.
Global consensus protocols do not themselves provide disconnected local payment finality;
that requires a separate offline-payment architecture (decision D-018).

## 13. Architecture candidates for FST

Based on FST's requirements (permissionless, Byzantine, multi-transport, offline-capable,
no privileged operator), the following candidate architecture families warrant further
investigation. None is selected or approved.

### Candidate 1: Leader-based BFT with stake Sybil layer

**HotStuff / Tendermint-style consensus with stake-weighted validator set.**

- Advantages: deterministic finality, linear per-view communication (HotStuff),
  well-understood safety [HotStuff2019; CastroLiskov1999].
- Weaknesses: permissioned consensus core requires stake layer for permissionless entry;
  leader DDoS target; stake centralisation; Tendermint/CometBFT communication complexity
  requires current spec inspection.
- Compatible with FST: partially; requires Sybil-resistance research and stake economics.

### Candidate 2: Nakamoto-style Proof of Work

**Bitcoin-style longest-chain with PoW.**

- Advantages: genuinely permissionless, long deployment track record, no stake
  concentration at genesis, no slashing complexity [Nakamoto2008].
- Weaknesses: probabilistic finality (slow), high energy cost, mining centralisation,
  51% attack threshold, slow settlement [Nakamoto2008].
- Compatible with FST: partially; finality and energy concerns unresolved.

### Candidate 3: Proof-of-stake longest-chain with finality gadget

**Ouroboros / Gasper-style.**

- Advantages: deterministic checkpoint finality (Gasper), permissionless staking, lower
  energy than PoW [Gasper2020; Ouroboros2017].
- Weaknesses: stake concentration, long-range attacks, nothing-at-stake, economic circularity
  (token value funds security), complex slashing [CasperFFG2017; Gasper2020].
- Compatible with FST: partially; aligns with native asset but centralisation risks unresolved.

### Candidate 4: Hybrid: BFT finality gadget + Nakamoto-style ordering

**Finality gadget (Casper FFG-style) layered on a permissionless ordering protocol.**

- Advantages: probabilistic ordering with deterministic finality checkpoints
  [CasperFFG2017].
- Weaknesses: complexity, interaction between layers, Sybil-resistance for both layers.
- Compatible with FST: partially; research needed on layer interaction.

### Candidate 5: DAG-based mempool-consensus separation

**Narwhal/Tusk-style.**

- Advantages: dissemination and consensus costs are distinct [NarwhalTusk2022].
- Weaknesses: complexity, less deployment history, permissioned core requires Sybil layer,
  Tusk uses randomized probabilistic termination under asynchrony.
- Compatible with FST: partially; throughput benefits for financial network.

### Candidate 6: Avalanche-family (evaluation candidate)

**Retained pending source verification.** The previous categorical exclusion is withdrawn
(see Section 5.6). Protocol-specific evaluation requires an authoritative primary source
and subsequent technical inspection before any safety, liveness, or finality claims can be
made.

## 14. Unsuitable approaches and reasons

- **Plain Raft:** crash-fault only, permissioned, no Byzantine tolerance (decision D-024).
- **Purely asynchronous BFT without timing assumptions:** cannot guarantee deterministic
  liveness under full asynchrony (FLP [FischerLynchPaterson1985]); randomized or
  partial-synchrony variants required.
- **Centralised or federated consensus:** violates P1, P2, P8 (unilateral control,
  privileged authority).
- **Consensus without Sybil resistance in a permissionless setting:** quorum thresholds
  are meaningless if identities are free [Douceur2002].
- **Deterministic finality as a universal requirement:** not approved by the project owner
  (decision D-017 remains OPEN RESEARCH); probabilistic finality remains a candidate.

## 15. Open technical questions

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
10. What are the upgrade and fork governance mechanisms?

## 16. Research references

**Verification date:** 2026-11-15. **Verification model:** This document distinguishes
two levels of verification:

- **Source identity / bibliographic verification:** Confirming that a publication exists,
  its title, authors, date, venue, and DOI/URL. Based on bibliographic records provided
  in the task description and prior correction rounds.
- **Technical claim verification:** Confirming that a specific technical claim (e.g.,
  message complexity, finality type, fault bound) is actually established in the source.
  Recording the level of material inspected: metadata only, abstract, official
  specification, relevant sections of full paper, or full paper.

**No external web access was available** to inspect full papers, publisher pages, or DOI
resolvers. Full-paper review was **not** performed by Kilo. All entries below are based
on metadata records and task-description source lists.

**Independent reviewer-supplied verification:** The task description indicates that an
independent project reviewer has externally examined:
- PBFT system model and service-properties sections [CastroLiskov1999]
- Narwhal/Tusk publication abstract and metadata [NarwhalTusk2022]
- CometBFT's current consensus specification [CometBFTConsensusSpec2024]
- Gasper's arXiv record and abstract [Gasper2020]
- Praos's IACR record and abstract [OuroborosPraos2017]
- Cited economic research records [IrresbergerYang2023; DaianEtAl2020]

These are treated as bibliographic metadata and section-level inspection, **not** as
full-paper technical claim verification by Kilo.

Verification status definitions:
- **VERIFIED:** Exact source and bibliographic record inspected; technical claims
  confirmed against full paper or official specification. (None achieved in this session.)
- **PARTIALLY VERIFIED:** Source existence and bibliographic metadata recorded from
  task description; technical claims not independently inspected against full paper.
- **UNRESOLVED:** Source or attributed claim has not been established.

| # | Title | Authors | Date | Venue | DOI/URL | Verification | Material inspected | Relevance to FST | What it establishes | What it does NOT establish |
|---|-------|---------|------|-------|---------|--------------|-------------------|------------------|---------------------|---------------------------|
| 1 | The Byzantine Generals Problem | Lamport, Shostak, Pease | 1982 | ACM TOPLAS | doi:10.1145/357172.357176 | PARTIALLY VERIFIED | Metadata only | Byzantine agreement 3f+1 bound | 3f+1 bound for Byzantine agreement | Permissioned setting; synchrony assumptions |
| 2 | Impossibility of Distributed Consensus with One Faulty Process | Fischer, Lynch, Paterson | 1985 | J. ACM | doi:10.1145/3149.214121 | PARTIALLY VERIFIED | Metadata only | FLP impossibility | Deterministic termination impossible under async + 1 crash | Randomized async consensus; partial-synchrony liveness |
| 3 | Consensus in the Presence of Partial Synchrony | Dwork, Lynch, Stockmeyer | 1988 | J. ACM, 35(2), 288–323 | doi:10.1145/42282.42283 | PARTIALLY VERIFIED | Metadata only | Partial synchrony model | Unknown message delay bound model | Protocol-specific liveness guarantees |
| 4 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | https://www.usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Metadata only; independent reviewer inspected system model and service-properties sections | BFT state machine replication | 3f+1 bound, O(n²) voting, safety without synchrony, liveness under partial synchrony | Fixed participant set; implementation specifics |
| 5 | The Sybil Attack | Douceur | 2002 | IPTPS | doi:10.1007/3-540-45748-8_24 | PARTIALLY VERIFIED | Metadata only | Sybil attack formalisation | Identity cost; Sybil attack definition | No specific Sybil-resistance solution |
| 6 | Brewer's Conjecture and the Feasibility of Consistent, Available, Partition-Tolerant Web Services | Gilbert, Lynch | 2002 | ACM SIGACT News, 33(2), 51–59 | doi:10.1145/564585.564601 | PARTIALLY VERIFIED | Metadata only | CAP theorem | Consistency/availability/partition trade-off (linearizability) | Local bounded operation during partitions |
| 7 | Bitcoin: A Peer-to-Peer Electronic Cash System | Nakamoto | 2008 | bitcoin.org whitepaper | https://bitcoin.org/bitcoin.pdf | PARTIALLY VERIFIED | Metadata only | Nakamoto-style PoW consensus | PoW, longest-chain, probabilistic finality | Deployment measurements; energy costs |
| 8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, et al. | 2019 | ACM PODC | doi:10.1145/3293611.3331591 | PARTIALLY VERIFIED | Metadata only | Linear BFT communication | O(n) per view via quorum certificates | View-change complexity; pipelined variants |
| 9 | Original Tendermint draft (v0.6) | Jae Kwon | 2014 | tendermint.com | https://tendermint.com/static/docs/tendermint.pdf | PARTIALLY VERIFIED | Metadata only | Historical Tendermint design | Leader-based BFT, round-robin proposers | Outdated draft; not current spec |
| 10 | CometBFT consensus specification | CometBFT project | Current (main branch) | GitHub | https://github.com/cometbft/cometbft/blob/main/spec/consensus/consensus.md | PARTIALLY VERIFIED | Metadata only; independent reviewer inspected current specification | Current BFT specification | Round-based Propose/Prevote/Precommit; voting power; >2/3 thresholds | Paper results vs implementation behaviour; not a permissionless admission mechanism |
| 11 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys, pages 34–50 | doi:10.1145/3492321.3519594 | PARTIALLY VERIFIED | Metadata only; independent reviewer inspected abstract and metadata | DAG-based consensus | Mempool/consensus separation; Narwhal DAG; Tusk async BFT | Deployment throughput; exact per-transaction complexity |
| 12 | Ouroboros: A Provably Secure Proof-of-Stake Blockchain Protocol | Kiayias, Russell, et al. | 2017 | CRYPTO | doi:10.1007/978-3-319-63688-7_12 | PARTIALLY VERIFIED | Metadata only | PoS longest-chain | Stake-weighted block proposal | Praos-specific improvements |
| 13 | Ouroboros Praos: An adaptively-secure, semi-synchronous proof-of-stake blockchain | David, Gaži, Kiayias, Russell | 2017 | IACR ePrint | https://eprint.iacr.org/2017/573 | PARTIALLY VERIFIED | Metadata only; independent reviewer inspected IACR record and abstract | PoS with settlement delay | Probabilistic finality with settlement delay | Exact security claims; protocol parameters |
| 14 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arxiv | https://arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Metadata only | PoS finality gadget | Checkpoint finality, slashing | Gasper combined protocol |
| 15 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arxiv | https://arxiv.org/abs/2003.03052 | PARTIALLY VERIFIED | Metadata only; independent reviewer inspected arXiv record and abstract | Gasper protocol | LMD-GHOST + Casper FFG; complete author list | Exact checkpoint finality parameters |
| 16 | Diem State Machine Replication / DiemBFT | Diem project | — | developers.diem.com | https://developers.diem.com/docs/technical-papers/state-machine-replication-paper/ | PARTIALLY VERIFIED | Metadata only | HotStuff-based BFT with epochs | Epoch changes, reconfiguration | Exact consensus parameters |
| 17 | Ethereum Gasper documentation | Ethereum project | Current | ethereum.org | https://ethereum.org/developers/docs/consensus-mechanisms/pos/gasper/ | PARTIALLY VERIFIED | Metadata only | Gasper implementation | LMD-GHOST + Casper FFG in Ethereum | Protocol specification details |
| 18 | Ethereum Weak Subjectivity documentation | Ethereum project | Current | ethereum.org | https://ethereum.org/developers/docs/consensus-mechanisms/pos/weak-subjectivity/ | PARTIALLY VERIFIED | Metadata only | Weak subjectivity | Checkpoints, long-range defence | Protocol parameter values |
| 19 | Ethereum consensus mechanisms documentation | Ethereum project | Current | ethereum.org | https://ethereum.org/developers/docs/consensus-mechanisms/ | PARTIALLY VERIFIED | Metadata only | Distinguishes Sybil-resistance/block-author selection from fork choice | PoW and PoS as Sybil-resistance and block-author selectors | Fork-choice algorithm details |
| 20 | Paxos Made Simple | Lamport | 2001 | ACM SIGACT News | doi:10.1145/568425.568433 | PARTIALLY VERIFIED | Metadata only | Crash-fault consensus (reference only) | Crash-fault, not Byzantine | Not applicable to Byzantine FST |
| 21 | Coin Concentration of Proof-of-Stake Blockchains | Irresberger, Yang | 2023 | Economics Letters, Volume 229, Article 111219 | doi:10.1016/j.econlet.2023.111219 | PARTIALLY VERIFIED | Metadata only | PoS concentration evidence | Observed block-production concentration and validator entry/exit | Does not establish universal law for every PoS system |
| 22 | Flash Boys 2.0: Frontrunning, Transaction Reordering, and Consensus Instability in Decentralized Exchanges | Daian, Goldfeder, Kell, Li, Zhao, Bentov, Breidenbach, Juels | 2020 | IEEE Symposium on Security and Privacy | doi:10.1109/SP40000.2020.00040 | PARTIALLY VERIFIED | Metadata only | MEV and transaction ordering evidence | Frontrunning, priority gas auctions, MEV-related consensus risks (Ethereum-specific) | Not universal to all blockchain systems |

**Sources removed (UNRESOLVED):** AlephBFT [Petermann2020], Avalanche-family
[AvaWhitepaper2019], and Chia Proof of Space/Time [ChiaProofOfSpaceAndTime] — no
authoritative source record was provided that permits confirmation of bibliographic
metadata or protocol claims. These are not cited as supporting any factual claim in this
document.

**Inline citation resolution:** All inline citation keys now resolve to reference table
entries #1-22. The previously unresolved DiemBFTWhitepaper2020 is now reference #16.

### 16.1 Source verification limitations

**No external web access was available during this verification task.** The following
limitations apply:

- **No reference achieved VERIFIED status.** Full papers, publisher pages, and DOI
  resolvers could not be inspected. All entries are PARTIALLY VERIFIED at best.
- **Verification date:** 2026-11-15. **Material inspected:** Bibliographic records
  (title, authors, date, venue, DOI/URL) provided in the task description only.
- **Kilo's inspection level:** Metadata only for all references. No abstracts, official
  specifications, or full papers were inspected by Kilo in this session.
- **Independent reviewer-supplied verification:** The task description provided source
  records and indicated reviewer inspection of specific sections/abstracts for PBFT,
  Narwhal/Tusk, CometBFT, Gasper, Praos, and economic references [IrresbergerYang2023;
  DaianEtAl2020]. These are treated as bibliographic metadata and section-level inspection,
  **not** as full-paper technical claim verification by Kilo.
- **Claim-to-source traceability:** Theoretical claims (FLP #2, 3f+1 bound #1 and #4,
  CAP theorem #6, quorum intersection #4) trace to their respective references.
  Protocol-specific claims (message complexity, finality type, resilience) trace to
  references #4-22 but require independent inspection against protocol specifications
  and published analyses. Economic and incentive claims (section 7) draw on
  [IrresbergerYang2023] and [DaianEtAl2020] where cited; remaining economic claims are
  qualitative assessments without specific source citations.

## 17. Recommended next investigation

1. **Inspect full papers** for references #1-22 to verify bibliographic metadata and
   extract protocol-specific claims (message complexity, finality type, fault bounds).
2. **Resolve UNRESOLVED sources** (AlephBFT, Avalanche-family, Chia) or formally exclude
   them from FST evaluation.
3. **Commission deep research** on 2-3 candidate families (e.g., HotStuff-style BFT,
   Nakamoto PoW, PoS longest-chain) with full source review.
4. **Define the FST adversary model** explicitly (Byzantine threshold, network assumptions,
   adversary resources).
5. **Investigate Sybil-resistance mechanisms** in the context of FST's native asset (FST
   only, scope D-014).
6. **Determine finality requirements** for financial settlement (deterministic vs
   probabilistic, latency bounds).
7. **Study multi-transport consensus adaptation** for mesh, radio, satellite, and
   delay-tolerant networks.
8. **Resolve decision D-035** (independent client implementation count).
9. **Commission independent security review** of any selected consensus design before
   implementation.

All items remain OPEN RESEARCH. No design is approved.

## 18. Consensus–Sybil composition analysis

Consensus families and Sybil-resistance mechanisms are **not interchangeable**. Putting two established mechanisms together does not automatically create a secure protocol [Section 2.10; Douceur2002]. Each combination below is analysed for: which component provides Sybil resistance, which provides agreement and ordering, how voting power or proposer probability is derived, which adversary assumptions apply, whether the composition has a coherent security argument, how validator entry and exit affect safety, compatibility with FST's decentralisation principles, and remaining research gaps.

### 18.1 Candidate C1: HotStuff-style BFT + stake-based validator eligibility

- **Sybil resistance:** Stake lockup (PoS). Validator eligibility = bonded stake above threshold [S14 Gasper2020].
- **Agreement and ordering:** HotStuff view-based BFT with quorum certificates; leader-driven, deterministic finality under partial synchrony [S8 HotStuff2019].
- **Voting power / proposer probability:** Stake-weighted voting power; proposer selected round-robin or stake-weighted from active committee [S8 HotStuff2019; S16 DiemBFT2020].
- **Adversary assumptions:** <1/3 Byzantine voting power (3f+1 bound); partial synchrony for liveness; stake not concentrated beyond the fault bound [S1 CastroLiskov1999; S8 HotStuff2019].
- **Coherent security argument?** Conditionally — requires the stake layer to map economic cost to voting power, and HotStuff's 3f+1 bound to hold over the stake-weighted set. Coherence depends on stake distribution respecting the fault bound; concentration breaks it [S14 Gasper2020; S39 IrresbergerYang2023].
- **Validator entry/exit effect on safety:** Entry changes committee composition; exit (unbonding) must exceed the slashing detection window to preserve accountable safety [S13 CasperFFG2017; S14 Gasper2020]. Rapid churn can temporarily concentrate voting power.
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016, but leader DDoS target and stake centralisation challenge P1/P4. Epoch-based reconfiguration (DiemBFT) helps entry/exit [S16 DiemBFT2020].
- **Research gaps:** Stake economics (D-015); unbonding/slashing parameters; leader DoS mitigation; committee rotation frequency.

### 18.2 Candidate C2: Tendermint/CometBFT-style BFT + stake-weighted voting

- **Sybil resistance:** Stake lockup (PoS) [S14 Gasper2020].
- **Agreement and ordering:** Round-based Propose/Prevote/Precommit; deterministic finality under >2/3 voting power [S9 CometBFT spec].
- **Voting power / proposer probability:** Stake-weighted voting power; proposer selected round-robin proportional to voting power [S9 CometBFT spec].
- **Adversary assumptions:** <1/3 Byzantine voting power; partial synchrony for liveness [S9 CometBFT spec; S3 DLS1988].
- **Coherent security argument?** Conditionally — CometBFT's spec confirms the BFT instance; stake-weighted voting power is external. Same coherence caveat as C1: stake concentration breaks the >2/3 assumption [S9 CometBFT spec; S14 Gasper2020].
- **Validator entry/exit effect on safety:** Epoch/height-based validator set changes; voting-power weight changes require reconfiguration. Unbonding > slashing window [S13 CasperFFG2017].
- **FST decentralisation compatibility:** Partially — deterministic finality and predictable proposer rotation aid liveness analysis, but leader DDoS surface [S46 AsiaCCS2024] and stake concentration challenge P1/P4.
- **Research gaps:** Communication complexity at scale (PBFT-style voting); leader DoS mitigation; stake-weight reconfiguration safety.

### 18.3 Candidate C3: Nakamoto consensus + computational-work admission

- **Sybil resistance:** Proof of Work (hashrate cost) [S7 Nakamoto2008].
- **Agreement and ordering:** Longest-chain (cumulative work) fork choice; probabilistic finality [S7 Nakamoto2008].
- **Voting power / proposer probability:** Hashrate-weighted; puzzle solver proposes [S7 Nakamoto2008].
- **Adversary assumptions:** <50% adversarial hashrate (honest-majority-of-work); network-delay-dependent margin [S42 PassEtAl2019; S7 Nakamoto2008].
- **Coherent security argument?** Yes — PoW is both Sybil mechanism and block-author selector; the honest-majority-of-work assumption is self-consistent [S7 Nakamoto2008; S42 PassEtAl2019]. Well-understood deployment track record (Bitcoin).
- **Validator entry/exit effect on safety:** Open entry/exit; hashrate can enter/leave freely. Safety degrades gracefully with hashrate share; no fixed committee to reconfigure.
- **FST decentralisation compatibility:** Partially — genuinely permissionless (D-016), no stake concentration at genesis, but mining-pool concentration and energy cost challenge P1/P4; probabilistic finality challenges high-value settlement (scope-v0.1.md).
- **Research gaps:** Finality latency for settlement; energy cost; pool centralisation mitigation; 51% rental-attack economics for smaller networks [S30 MITDCI2020].

### 18.4 Candidate C4: PoS longest-chain + stake-weighted proposer selection and fork choice

- **Sybil resistance:** Stake lockup (PoS) [S12 OuroborosPraos2017].
- **Agreement and ordering:** Stake-weighted slot-leader election (VRF) + chain selection; probabilistic finality with settlement delay [S12 OuroborosPraos2017].
- **Voting power / proposer probability:** Stake-weighted; VRF hides slot leader until they speak [S12 OuroborosPraos2017].
- **Adversary assumptions:** Honest majority of stake; semi-synchronous (unknown delay bound); adaptive corruption [S12 OuroborosPraos2017].
- **Coherent security argument?** Yes — Praos provides provably secure PoS longest-chain with adaptive security under honest-stake-majority [S12 OuroborosPraos2017]. Finality is probabilistic, not deterministic.
- **Validator entry/exit effect on safety:** Stake-weighted eligibility changes per epoch; unbonding/slashing windows apply [S12 OuroborosPraos2017; S13 CasperFFG2017].
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016 and native asset (D-014), but stake concentration, long-range attacks, nothing-at-stake, and weak subjectivity challenge P1/P9/P10 [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].
- **Research gaps:** Weak-subjectivity checkpointing for independent verification (P9); long-range-attack defence; settlement-latency requirements for financial settlement.

### 18.5 Candidate C5: PoS + separate finality gadget (Gasper-style)

- **Sybil resistance:** Stake lockup via Casper FFG [S14 Gasper2020].
- **Agreement and ordering:** LMD-GHOST fork choice + Casper FFG finality gadget [S14 Gasper2020].
- **Voting power / proposer probability:** Stake-weighted; proposer assigned per slot [S14 Gasper2020].
- **Adversary assumptions:** ≥2/3 honest stake for finality; accountable safety (≥1/3 slashable on conflicting finality); partial synchrony for liveness [S14 Gasper2020; S13 CasperFFG2017].
- **Coherent security argument?** Yes — Gasper formally combines fork choice and finality; accountable safety theorem [S13 CasperFFG2017; S14 Gasper2020]. Finality is deterministic checkpoint finality qualified by Byzantine-stake and weak-subjectivity assumptions [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].
- **Validator entry/exit effect on safety:** Validator set changes per epoch; unbonding > weak-subjectivity period required to prevent long-range attacks after exit [S41 BabylonBlog2023; S40 EthereumWeakSubjectivity2024].
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016, but stake concentration, custodial/LST centralisation, and weak-subjectivity trust challenge P1/P9/P10 [S35 a16zSlashing2023; S40 EthereumWeakSubjectivity2024].
- **Research gaps:** Weak-subjectivity period vs unbonding; slashing accuracy; inactivity-leak recovery; stake economics (D-015).

### 18.6 Candidate C6: DAG-based dissemination + separate agreement/finality (Narwhal/Tusk-style)

- **Sybil resistance:** External stake/work layer required (none intrinsic to Narwhal/Tusk) [S10 NarwhalTusk2022].
- **Agreement and ordering:** Narwhal DAG mempool for dissemination (asynchronous, tolerates asynchrony); Tusk async BFT consensus or Narwhal-HotStuff for ordering [S10 NarwhalTusk2022].
- **Voting power / proposer probability:** Depends on composed consensus (HotStuff/Tusk) + external Sybil layer [S10 NarwhalTusk2022].
- **Adversary assumptions:** Tusk tolerates asynchrony with randomized probabilistic termination; Narwhal-HotStuff inherits partial-synchrony liveness [S10 NarwhalTusk2022].
- **Coherent security argument?** Partially — separation is well-defined empirically (130k+ tx/s WAN reported), but the combined system's formal security argument depends on the chosen consensus layer and Sybil layer; no single unified proof [S10 NarwhalTusk2022].
- **Validator entry/exit effect on safety:** Depends on composed consensus; dissemination layer is independent of validator set [S10 NarwhalTusk2022].
- **FST decentralisation compatibility:** Partially — separation may suit multi-transport networks (dissemination and consensus have different communication requirements) [D-019; Section 8.10 of this document], but complexity and less deployment history are concerns.
- **Research gaps:** Formal security of composition; Sybil layer integration; deployment track record; multi-transport adaptation.

### 18.7 Candidate C7: Hybrid PoW/PoS (both hash power and stake required)

- **Sybil resistance:** Both PoW and PoS — attacker must control majority of both resources simultaneously [S52 ZanoConsensus2024; S59 Synergeia2025].
- **Agreement and ordering:** Protocol-specific (e.g., PoW finds blocks, PoS tickets vote to finalize; PoW + PoS chain for finality) [S52 ZanoConsensus2024; S55 ConfluxPoS2024].
- **Voting power / proposer probability:** Combined hashrate + stake weight [S52 ZanoConsensus2024].
- **Adversary assumptions:** Honest majority of both resources; higher attack cost than either alone [S52 ZanoConsensus2024].
- **Coherent security argument?** Conditionally — higher attack cost but added complexity and latency; security budget split across two mechanisms [S59 Synergeia2025; S54 InterleavingHybrid2019].
- **Validator entry/exit effect on safety:** Both layers' entry/exit interact; complexity increases with two Sybil layers [S52 ZanoConsensus2024].
- **FST decentralisation compatibility:** Medium — permissionless on both layers but operational complexity high.
- **Research gaps:** Layer-interaction safety; economic security budget split; implementation complexity.

### 18.8 Composition principle

For every composition: identify which component provides Sybil resistance and which provides agreement/ordering; verify that the Sybil mechanism's output (voting-power distribution) respects the consensus fault bound; confirm the security argument does not silently assume away the interaction between layers [Douceur2002; Section 2.10]. No composition is selected for FST.

## 19. Network and financial requirements

Evaluated against FST's intended operating conditions (scope-v0.1.md):

**Globally distributed participants:** All candidate compositions assume geographically distributed validators. Geographic diversity improves partition resilience but increases latency (speed of light) — a physical constraint, not a protocol choice [Section 8.8].

**Internet connectivity with varying latency and reliability:** BFT protocols (C1, C2, C5) assume reliable point-to-point channels and partial synchrony for liveness; variable latency increases timeouts and degrades liveness [Sections 8.3, 8.9]. PoW/PoS longest-chain (C3, C4) are more tolerant of variable delay but probabilistic finality needs longer confirmation under high delay [S7 Nakamoto2008; S42 PassEtAl2019].

**Future mesh, radio, and satellite connectivity:** These introduce intermittent, high-delay links resembling asynchrony. FLP rules out deterministic consensus with guaranteed liveness under pure asynchrony [S2 FLP1985]. DLS shows safety-always/liveness-after-GST is achievable under partial synchrony [S3 DLS1988]. FST consensus must separate safety (always preserved) from liveness (conditional on connectivity recovery) [Section 3.1].

**Delay-tolerant or store-and-forward communication (future):** DTN (RFC 4838, RFC 9171) provides eventual delivery with unbounded delay — maps to asynchrony; consensus safety preserved, liveness suspended until contact returns [S17 RFC4838; S18 RFC9171]. Narwhal's asynchronous mempool demonstrates dissemination can tolerate asynchrony while a consensus layer provides ordering [S10 NarwhalTusk2022] — a relevant pattern for multi-transport FST, but consensus over DTN is open research [Section 8.10].

**Native FST as the initial asset:** Sybil mechanisms using stake (C1, C2, C4, C5, C7) require FST to have market value to fund security — economic circularity for a new network [S13 CasperFFG2017; S14 Gasper2020; Section 7.14]. PoW (C3) requires hardware/energy expenditure independent of FST value initially, but security budget still scales with token value [S7 Nakamoto2008].

**Person-to-person, merchant and institutional payments:** All compositions can carry payments; finality model determines settlement risk. Deterministic finality (C1, C2, C5) suits high-value settlement; probabilistic finality (C3, C4) requires confirmation-depth risk management [S7 Nakamoto2008; S12 OuroborosPraos2017].

**High-value settlement requirements:** Deterministic finality preferred for high-value settlement (no reversion under assumptions); probabilistic finality requires economic finality (reversion cost exceeds transaction value) — unresolved for FST [D-017; Section 3.11].

**Independent implementation teams:** Protocol specification must be implementation-independent [Section 11]. Formal verification opportunities (model checking, theorem proving) increase confidence for independent implementations [Section 10].

**Network forks and replay protection:** D-036 (APPROVED) requires unique network identity, replay protection, distinct currency identifiers for independent forks. Fork choice and finality model affect fork behaviour; all compositions need fork-protection mechanisms at the transaction layer (D-036 mechanisms OPEN RESEARCH) [scope-v0.1.md; transaction-model-research.md Section 5.4].

**Bounded offline operation (future):** CAP — during a partition, unrestricted local spending and global double-spend prevention cannot both be guaranteed [S6 GilbertLynch2002; scope-v0.1.md Offline financial operation]. Bounded offline spending authority (AP-style: local availability during disconnection, reconciliation on reconnect) is the FST direction (D-018 PROVISIONAL). Consensus layer does not provide offline finality; a separate offline-payment architecture must be composed [Section 3.13].

**Do not claim:** All regions can independently achieve unrestricted spending and global finality while disconnected — this is impossible under CAP/FLP [S6 GilbertLynch2002; S2 FLP1985].

**Initial internet-connected test network vs future multi-transport:** Initial requirements assume internet connectivity with partial synchrony; multi-transport (mesh/radio/satellite/DTN) is a future ambition requiring consensus adaptation to unbounded delay [D-019 PROVISIONAL; Section 8.10].

## 20. Monetary and economic dependencies

Analyzed per the mission; sensitivity analysis used rather than invented monetary parameters. D-015 remains OPEN RESEARCH; no issuance, supply, inflation, validator rewards, or final economic parameters are chosen.

**Token supply:** Determines the scarce resource for stake-based Sybil mechanisms. For a new network, initial supply and distribution affect early centralisation [Section 7.1 of this document; design concern].

**Validator rewards:** Incentivise participation but increase inflation. Reward design affects centralisation (economies of scale may favour large validators) [Section 7.2; economic hypothesis].

**Transaction fees:** Pay for block space and can prioritise transactions. Fee markets may centralise block production through MEV — observed deployment phenomenon in Ethereum [S21 DaianEtAl2020; Section 7.12]. Ethereum-specific findings; not generalized to all systems.

**Economic security budgets:** Security budget ≈ cost to attack ≈ value of stake/hashrate at stake. Requires token value high enough to make attacks uneconomic. Protocol-specific and economically circular for a new network [S13 CasperFFG2017; S14 Gasper2020; Section 7.5].

**Staking and bonding:** Validators lock tokens as collateral; misbehaviour can be slashed. Stake concentration leads to centralisation; delegation to pools amplifies this [S13 CasperFFG2017; S14 Gasper2020; Section 6.4].

**Mining expenditure:** PoW security ≈ cost of hardware + energy [S7 Nakamoto2008]. Environmental and centralisation concerns are observed deployment phenomena from later analysis, not claims in the original paper [Section 7.6].

**Slashing:** Penalises misbehaviour (double-signing, offline time). Requires evidence collection and slashing mechanism [S13 CasperFFG2017; S14 Gasper2020; Section 6.9]. Slashing introduces centralisation risk if a small group decides what counts as misbehaviour — a governance design question, not a protocol certainty.

**Delegation:** Lowers entry barriers but concentrates power in pool operators — known centralisation vector in PoS networks; degree depends on protocol design and economic parameters [Section 6.6; observed deployment phenomenon].

**Attack costs:** Economic cost of attacking must exceed potential gain. Depends on total stake/hashrate and token value. Quantifying requires protocol-specific economic modeling [Section 7.10; design concern].

**Long-term validator participation:** Profitability determines participation; low profitability drives validators out, reducing security — an economic feedback loop, not a protocol guarantee [Section 7.9; economic hypothesis].

**Monetary-consensus interdependence:** Consensus determines what resource secures the network (work, stake, etc.); monetary policy determines the value and issuance of that resource. Neither can be designed in isolation [Section 7.14]. FST monetary policy is unresolved (D-015); consensus selection should not presuppose a monetary policy.

**Use sensitivity analysis:** Because D-015 is OPEN RESEARCH, all economic claims in this section are qualitative design concerns, not quantified parameters. No attack cost, reward rate, issuance schedule, or supply figure is asserted for FST.

## 21. Transaction-model dependencies

RS-015 (transaction-model-research.md) is complete as a research deliverable but remains OPEN RESEARCH pending owner selection (D-015 OPEN RESEARCH; RS-015 status OPEN). Both credible RS-015 candidate models (traditional UTXO and account-based) are used as comparison cases; no transaction model is approved or selected.

Evaluated whether candidate consensus families impose materially different requirements on:

**UTXO transaction processing:** Stateless, local validation; double-spend prevention native (each output spent once); reorg re-unspends outputs and transactions return to mempool [transaction-model-research.md Sections 3.1, 5.14]. Compatible with all consensus families; fork choice determines which conflicting transaction wins.

**Account-based nonce ordering:** State-dependent validation; nonce prevents double-spend; reorg reverts nonce/balance changes and creates replay risk [transaction-model-research.md Sections 3.2, 5.14]. Consensus finality model affects reorg exposure; deterministic finality reduces replay risk vs probabilistic.

**Parallel validation:** UTXO/eUTXO allow high parallelism for non-conflicting inputs/cells; account-based nonce ordering forces per-account serialization [transaction-model-research.md Section 4]. Consensus ordering must serialize conflicting transactions regardless of model.

**Transaction dissemination:** Gossip-based in all models; propagation scope (gossip vs direct to proposers) unresolved [transaction-model-research.md Section 5.13; RS-013 Section 2.5]. Narwhal-style DAG dissemination separates this from consensus [S10 NarwhalTusk2022].

**Conflict handling:** UTXO — same-input conflict resolved by consensus ordering; account-based — same-nonce replacement; eUTXO — same-cell conflict [transaction-model-research.md Section 5.7]. Consensus fork choice determines the canonical winner.

**State-machine execution:** Consensus orders transactions; execution applies them deterministically so all honest nodes converge [RS-013 Section 2.9]. Execution model (UTXO vs account vs eUTXO) affects validation complexity and determinism [transaction-model-research.md Section 4].

**Transaction finality:** Deterministic finality (C1, C2, C5) makes confirmation irreversible under assumptions; probabilistic finality (C3, C4) makes reversion exponentially unlikely with depth but never impossible [RS-013 Section 3.11]. UTXO re-spend on reorg vs account nonce replay risk on reorg differ by model [transaction-model-research.md Section 5.14].

**Comparison across the two RS-015 candidates:** UTXO's stateless validation and native double-spend prevention simplify integration with probabilistic finality (reorgs re-unspend outputs cleanly). Account-based nonce ordering couples transaction validity to chain position, making reorgs and replay protection more complex under probabilistic finality. Neither model is selected; both remain sensitivity cases for consensus research per the mission.

## 22. Verified source records

Each reference below records bibliographic metadata, material inspected, exactly what was verified, what was not verified, limitations, and verification status. Metadata-only inspection is **not** labelled as full technical verification.

| # | Title | Authors | Date | Venue | DOI/URL | Status | Material inspected | What it establishes | What it does NOT establish |
|---|---|---|---|---|---|---|---|---|---|
| #1 | The Byzantine Generals Problem | Lamport, Shostak, Pease | 1982 | ACM TOPLAS 4(3) | doi:10.1145/357172.357176 | VERIFIED | Abstract + §§2–3 via lamport.org/pubs/byz.pdf | 3f+1 oral-message bound; signed messages lift bound; interactive consistency | Permissioned setting; synchrony assumptions |
| #2 | Impossibility of Distributed Consensus with One Faulty Process | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| #3 | Consensus in the Presence of Partial Synchrony | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST; Byzantine N≥3t+1 | Protocol-specific liveness guarantees |
| #4 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Full text (Introduction, Service Properties) via HTML mirrors | 3f+1 bound; safety without synchrony; liveness under partial synchrony (weak synchrony); optimal resiliency | O(n²)/view; quorum intersection (not stated in inspected text) |
| #5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil attack = one entity, many identities; impossible without central authority except under unrealistic resource parity/coordination | No specific Sybil-resistance solution endorsed |
| #6 | Brewer's Conjecture and the Feasibility of Consistent, Available, Partition-Tolerant Web Services | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text | C+A+P impossible in async; consistency = atomic/linearizable; trade-off during partitions | Local bounded operation during partitions |
| #7 | Bitcoin: A Peer-to-Peer Electronic Cash System | Nakamoto | 2008 | bitcoin.org whitepaper | bitcoin.org/bitcoin.pdf | VERIFIED | Full text | PoW = one-CPU-one-vote; longest chain; honest CPU majority → outpace attackers; probabilistic finality, exponential catch-up decay; <50% hash rate | Deployment measurements; energy costs |
| #8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | PODC | doi:10.1145/3293611.3331591; arXiv 1803.05069 | VERIFIED | Abstract + full text (QC, complexity) | O(n) per view (correct leader); QC = n−f = 2f+1; partial synchrony, safety always, liveness after GST | Worst case O(n²) under cascading leader failures |
| #9 | Original Tendermint draft (v0.6) | Jae Kwon | 2014 | tendermint.com | tendermint.com/static/docs/tendermint.pdf | PARTIALLY VERIFIED | Metadata only | Historical Tendermint design | Outdated draft; not current spec |
| #10 | CometBFT consensus specification | CometBFT project | Current (main) | GitHub | github.com/cometbft/cometbft/blob/main/spec/consensus/consensus.md | VERIFIED | Full spec text | Round-based Propose/Prevote/Precommit; >2/3 thresholds; voting power; round-robin proposer; safety <1/3 byzantine voting power | Not a permissionless admission mechanism |
| #11 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys pp. 34–50 | doi:10.1145/3492321.3519594; arXiv 2105.11827 | VERIFIED | Abstract + full text (§§1, 2, 3.1, 5) | Mempool/consensus separation; Narwhal DAG; Tusk async BFT, zero extra communication, wait-free | Deployment throughput; exact per-transaction complexity |
| #12 | Ouroboros: A Provably Secure Proof-of-Stake Blockchain Protocol | Kiayias, Russell, David, Oliynykov | 2017 | CRYPTO | doi:10.1007/978-3-319-63688-7_12; ePrint 2016/889 | PARTIALLY VERIFIED | Abstract + Introduction + model | Stake-weighted slot-leader election; epochs/slots; honest majority; synchronous model | VRF leader selection (belongs to Praos); settlement delay |
| #13 | Ouroboros Praos: An adaptively-secure, semi-synchronous proof-of-stake protocol | David, Gaži, Kiayias, Russell | 2017 | IACR ePrint 2017/573; EUROCRYPT 2018 | eprint.iacr.org/2017/573 | VERIFIED | Abstract + §1 + §2.2 + leader selection | VRF leader selection; fully adaptive security; semi-synchronous/unknown delay; honest majority of stake; probabilistic finality with settlement delay | Exact protocol parameters |
| #14 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arXiv | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems | Checkpoint finality; supermajority 2/3; two slashing conditions; accountable safety (≥1/3 slash); plausible liveness (≥2/3) | LMD-GHOST and weak subjectivity (Gasper/Ethereum docs, not FFG paper) |
| #15 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arXiv | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 | LMD-GHOST + Casper FFG; checkpoint finality; slashing (S1/S2); safety/plausible/probabilistic liveness | Exact checkpoint finality parameters |
| #16 | Diem State Machine Replication / DiemBFT | Diem project | — | developers.diem.com | developers.diem.com/docs/technical-papers/state-machine-replication-paper/ | PARTIALLY VERIFIED | Metadata only | HotStuff-based BFT with epochs | Exact consensus parameters |
| #17 | Ethereum Gasper documentation | Ethereum project | Current | ethereum.org | ethereum.org/developers/docs/consensus-mechanisms/pos/gasper/ | PARTIALLY VERIFIED | Metadata only | Gasper implementation | Protocol specification details |
| #18 | Ethereum Weak Subjectivity documentation | Ethereum project | Current | ethereum.org | ethereum.org/developers/docs/consensus-mechanisms/pos/weak-subjectivity/ | PARTIALLY VERIFIED | Metadata only | Weak subjectivity; checkpoints | Protocol parameter values |
| #19 | Ethereum consensus mechanisms documentation | Ethereum project | Current | ethereum.org | ethereum.org/developers/docs/consensus-mechanisms/ | PARTIALLY VERIFIED | Metadata only | Distinguishes Sybil-resistance/block-author selection from fork choice | Fork-choice algorithm details |
| #20 | Paxos Made Simple | Lamport | 2001 | ACM SIGACT News | doi:10.1145/568425.568433 | PARTIALLY VERIFIED | Metadata only | Crash-fault consensus (reference only) | Not applicable to Byzantine FST |
| #21 | Coin Concentration of Proof-of-Stake Blockchains | Irresberger, Yang | 2023 | Economics Letters 229:111219 | doi:10.1016/j.econlet.2023.111219 | PARTIALLY VERIFIED | Metadata only | Observed block-production concentration and validator entry/exit | Does not establish universal law for every PoS system |
| #22 | Flash Boys 2.0: Frontrunning, Transaction Reordering, and Consensus Instability in Decentralized Exchanges | Daian, Goldfeder, Kell, Li, Zhao, Bentov, Breidenbach, Juels | 2020 | IEEE S&P | doi:10.1109/SP40000.2020.00040 | PARTIALLY VERIFIED | Metadata only | MEV and transaction ordering evidence (Ethereum-specific) | Not universal to all blockchain systems |

**Additional verified sources consulted for RS-013 (not in original #1–#22):**
| # | Title | Authors | Date | Venue | DOI/URL | Status | Material inspected | What it establishes |
|---|---|---|---|---|---|---|---|---|
| #23 | Eclipse Attacks on Bitcoin's Peer-to-Peer Network | Heilman, Kendler, Zohar, Goldberg | 2015 | USENIX Security | eprint.iacr.org/2015/263 | VERIFIED | Abstract + paper | Eclipse attack feasibility; enables double-spend/selfish mining/adversarial forks |
| #24 | An Empirical Study of Consensus Protocols' DoS Resilience | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper | Leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size |
| #25 | Delay-Tolerant Networking Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text | Store-carry-and-forward; intermittent contacts; no end-to-end contemporaneous path |
| #26 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc/rfc9171 | VERIFIED | Full text | Bundle protocol for DTN; persistent storage; asynchronous message primitives |
| #27 | Analysis of Nakamoto Consensus | Pass, Seeman, shelat | 2019 | IACR ePrint 2019/943 | eprint.iacr.org/2019/943 | VERIFIED | Abstract + main theorem | Honest-majority condition with network-delay margin |
| #28 | Aleph: A Leaderless, Asynchronous, Byzantine Fault Tolerant Consensus Protocol | Ga̧gol, Lesniak, Straszak, Świętek | 2018 | arXiv 1810.05256; AFT 2019 | arxiv.org/abs/1810.05256 | PARTIALLY VERIFIED | Abstract + AlephBFT docs | Leaderless async BFT; DAG/poset; trustless beacon; <1/3 malicious; async safety | Async liveness (implementation lacks it); prespecified committee |
| #29 | Scalable and Probabilistic Leaderless BFT Consensus through Metastability | Yin, Sekniqi, van Renesse, Sirer | 2019 | arXiv 1906.08936 | arxiv.org/abs/1906.08936 | VERIFIED | Abstract + full text | Snow family (Slush/Snowflake/Snowball/Avalanche); leaderless metastable subsampling; probabilistic safety; O(kn)–O(kn log n) | "Team Rocket" pseudonymous (2018 IPFS precursor); named authors in 2019 paper |
| #30 | Chia Business Whitepaper v2.0 / Sustainable Nakamoto Consensus using Proof of Space and Time | Chia Network | 2022-02 | chia.net / docs.chia.net | docs.chia.net | VERIFIED | Whitepaper + docs | Proof of Space (plotting/farming); Proof of Time (VDF/timelords); Nakamoto longest-chain; farmer win ∝ space share | Deployment measurements |

**Verification status definitions (RS-013):**
- **VERIFIED:** Exact source and bibliographic record inspected; technical claims confirmed against full paper, official specification, or authoritative documentation.
- **PARTIALLY VERIFIED:** Source existence and bibliographic metadata confirmed; technical claims based on abstract, documentation, or secondary summary, not full primary inspection.
- **UNRESOLVED:** Source or attributed claim has not been established. (No RS-013 reference is currently UNRESOLVED at the metadata level.)

**No external web access limitation resolved:** Primary-source inspection was performed via web retrieval during this session. Bibliographic metadata is verified for all 30 sources. Technical claims are VERIFIED for 19 sources and PARTIALLY VERIFIED for 11 sources. Metadata-only inspection is not labelled as full technical verification.

**Claim-to-source traceability:** Theoretical claims (FLP #2, 3f+1 bound #1/#3/#4, CAP theorem #6, quorum intersection #4) trace to their respective references. Protocol-specific claims (message complexity, finality type, fault bounds) trace to references #4–#15 but require independent inspection against protocol specifications and published analyses where marked PARTIALLY VERIFIED. Economic and incentive claims (Section 7) draw on #21, #22, #39, #40 where cited; remaining economic claims are qualitative assessments without specific source citations.

## 23. Recommended next investigation

1. **Define the FST adversary model** explicitly (Byzantine threshold, network assumptions, adversary resources) — prerequisite for any threshold claim.
2. **Deep-research 2–3 candidate compositions** (e.g., C1 HotStuff+stake, C3 Nakamoto PoW, C5 Gasper) with full source review and formal-security assessment.
3. **Resolve stake economics (D-015)** in parallel, since Sybil-resistance security budgets depend on token value.
4. **Determine finality requirements** for financial settlement (deterministic vs probabilistic, latency bounds) — D-017 OPEN RESEARCH.
5. **Study multi-transport consensus adaptation** for mesh, radio, satellite, and delay-tolerant networks — D-019 PROVISIONAL.
6. **Resolve D-035** (independent client implementation count) before mainnet.
7. **Commission independent security review** of any selected consensus design before implementation.

All items remain OPEN RESEARCH. No design is approved.

