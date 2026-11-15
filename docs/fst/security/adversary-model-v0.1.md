# FST Adversary Model — v0.1

**Status:** DRAFT — research document, not an approved security design.
**Classification:** OPEN RESEARCH (adversary assumptions are unresolved; decision D-017 OPEN RESEARCH, D-016 PROVISIONAL).
**Related decisions:** D-015 (OPEN RESEARCH), D-016 (PROVISIONAL), D-017 (OPEN RESEARCH), D-018 (PROVISIONAL), D-019 (PROVISIONAL), D-024 (PROVISIONAL), D-036 (APPROVED).
**Related research:** RS-001 (consensus), RS-002 (Sybil resistance), RS-013 (consensus–validator), RS-015 (transaction model).
**Related specifications:** `docs/fst/security/threat-model-v0.1.md` (network-layer threat model T-01..T-013), `docs/fst/architecture/node-roles.md`, `docs/fst/spec/core-protocol-specification-v0.1.md`.

This document defines an explicit FST adversary model. It is a **research document, not a security specification**. No FST Byzantine threshold, no honest-majority constant, and no security parameter is approved or selected here. All thresholds are expressed as **parameters** tied to candidate architectures, not as FST constants.

The adversary model is intentionally conservative: it assumes adversaries are rational, adaptive, and resource-constrained but not omnipotent. It does not assume a universal honest-majority model; each candidate architecture must state the assumptions under which its security properties hold.

## 1. Security properties

The model distinguishes four security properties that are frequently conflated. A candidate architecture must state which of these it targets, under which assumptions, and for which threat classes.

| Property | Meaning |
|----------|---------|
| **Safety** | Nothing bad happens: no two conflicting transactions/blocks are both finalised; no double-spend is settled; no invalid state transition is accepted. |
| **Liveness** | Something good eventually happens: new transactions are included; new blocks are finalised; the chain makes progress. Liveness is conditional on network and participation assumptions (FLP/CAP constraints apply). |
| **Censorship resistance** | No coalition of participants can durably prevent a valid transaction from being included or a valid block from being extended, beyond the protocol's fault bound. |
| **Availability** | Honest participants can access the network, retrieve state, and synchronize after disruption. Availability may be sacrificed during partitions to preserve safety (CAP). |

Safety and liveness are the classical consensus properties. Cancellation-resistance (censorship resistance) and availability are operational properties that depend on both consensus and network-layer behaviour. They are evaluated separately because a protocol can be safe and live under its fault bound yet still permit temporary censorship or reduced availability during partitions.

## 2. Methodology and verification conventions

Each threat class below is documented with a consistent structure:

- **Attacker capabilities** — what the adversary can do.
- **Attacker objectives** — what the adversary is trying to achieve.
- **Valuable assets** — what the adversary seeks to compromise or extract.
- **Relevant security property** — which of safety / liveness / censorship resistance / availability is primarily at stake.
- **Required honest-participant assumptions** — assumptions about honest behaviour under which the property holds.
- **Required network assumptions** — timing/connectivity assumptions under which the property holds.
- **Candidate mitigations** — protocol, economic, or operational mitigations (none selected; all OPEN RESEARCH).
- **Residual risk** — risk that remains after mitigations.
- **Evidence and references** — exact title, author, date, venue, DOI/URL, verification status, material inspected.

**Verification status conventions** (consistent with RS-002 §8 and RS-013 §16):

- **VERIFIED** — bibliographic record and technical claim confirmed against primary paper, official specification, or authoritative documentation; material inspected recorded.
- **PARTIALLY VERIFIED** — bibliographic metadata confirmed; technical claim based on abstract, documentation, or secondary summary, not full primary inspection.
- **UNRESOLVED** — source or attributed claim not established.

No external web access was used to inspect full papers beyond what is recorded in RS-002 §8 and RS-013 §16. Claims marked VERIFIED inherit the caveat in RS-013 §16.1 that Kilo's inspection level was metadata-only for several references; where the baseline records fuller inspection, that is noted.

All sources below are drawn from the independently inspected records in RS-002 §8 and RS-013 §16 unless otherwise noted. No source is treated as an implementation endorsement.

## 3. Parameterized thresholds (not FST constants)

The model does not approve any FST Byzantine threshold. Instead, it introduces **architecture-specific parameters** that each candidate architecture must instantiate (and the owner must approve) before implementation:

| Parameter | Meaning | Candidate architecture where applicable |
|-----------|---------|-----------------------------------------|
| `f_bft` | Maximum Byzantine voting-power fraction tolerated by a classical/leader-based BFT instance (classically <1/3, i.e. 3f+1 replicas for f faults) | HotStuff-style BFT + stake (C1); Tendermint/CometBFT-style BFT + stake (C2) |
| `q_finality` | Fraction of voting power required to finalise/checkpoint (e.g. ≥2/3 supermajority) | PoS + finality gadget / Gasper-style (C5) |
| `h_stake` | Minimum honest-stake fraction required for safety/liveness | PoS longest-chain / checkpoint finality (C4, C5) |
| `h_pow` | Minimum honest-hashrate fraction required (honest-majority-of-work, <50% adversarial) | Nakamoto PoW (C3) |
| `f_async` | Fault tolerance under asynchrony for randomized consensus (probabilistic termination) | DAG-based / Tusk-style (C6) |

Each parameter is a **candidate-specific value**, not an FST-wide constant. A composition is secure only if its Sybil-resistance layer produces a voting-power distribution that respects the consensus fault bound (e.g., Byzantine voting power < `f_bft`, honest stake ≥ `h_stake`, honest hashrate ≥ `h_pow`). Concentration of the scarce resource breaks the assumption regardless of how well the Sybil mechanism itself functions [Douceur2002; RS-013 §3.2, §9.2].

The relationship between parameters is architecture-dependent and must not be generalized across families: the 3f+1 bound assumes a known fixed replica set and does not transfer to permissionless Nakamoto or PoS settings [RS-013 §3.2].

## 4. Adversary assumptions (cross-cutting)

The following assumptions apply across threat classes unless overridden:

- **Rational, bounded adversary:** The adversary seeks to maximize gain (double-spend, censorship, liveness denial, value extraction) subject to resource and economic constraints. The adversary is not assumed to have unlimited computational power, unlimited stake, or the ability to break cryptographic primitives.
- **Adaptive corruption:** The adversary may corrupt participants over time, subject to the candidate architecture's adaptive-security assumptions (where such assumptions exist).
- **Network control short of total partition:** The adversary may delay, drop, reorder, or selectively withhold messages, and may partition the network, but cannot indefinitely prevent all communication between honest nodes (partial synchrony or eventual reconnection is assumed for liveness).
- **Cryptographic assumptions:** Digital signatures, hash functions, and (where used) VRFs/threshold signatures are unforgeable under the candidate's assumptions. Cryptographic review of FeinStatic primitives is required before reuse (decision D-023 PROVISIONAL).
- **No universal honest majority:** No honest-majority assumption is approved as an FST-wide constant. Each candidate architecture states its own threshold as a parameter.

## 5. Threat classes

### 5.1 Independent malicious node operators

**Attacker capabilities:** Operates one or more nodes that follow the network identity and session protocols but behave maliciously after session establishment: sends invalid messages, withholds messages, sends conflicting information to different peers (equivocation), advertises false capabilities or endpoints, or attempts resource exhaustion through high message volume.

**Attacker objectives:** Disrupt synchronization, partition victims, consume node resources, or influence consensus/quorum formation if the operator is also a validator.

**Valuable assets:** Node availability, peer-store integrity, message-delivery reliability, local CPU/memory/bandwidth/file-descriptor resources.

**Relevant security property:** Availability (primary); safety (secondary, if the operator is a validator).

**Required honest-participant assumptions:** Honest nodes do not trust peers for state correctness; peers are authenticated but not trusted (principle P9). At least some honest peers are available for synchronization.

**Required network assumptions:** Authenticated peer sessions (TLS 1.3 with node-record binding); bounded per-peer and total connections; per-type payload size limits; wrong-network rejection.

**Candidate mitigations:** Connection limits (max 64 total, 32 inbound, 32 outbound, 2 per peer), bounded message payloads, peer lifecycle circuit breaker with capped exponential backoff, rate limiting, misbehaviour scoring, peer banning/disconnection (none implemented beyond baseline; OPEN RESEARCH).

**Residual risk:** A malicious peer can still consume connection slots, send valid but useless messages, or selectively withhold messages to disrupt synchronization.

**Evidence:** Threat model T-01 (malicious peers) — partially mitigated at network layer; RS-013 §9.12 (operational attack surfaces); Douceur2002 [S5, VERIFIED, full text: abstract, model, conclusions] (identity multiplication); CastroLiskov1999 [S4, PARTIALLY VERIFIED] (Byzantine behaviour model). Verification status: VERIFIED for Douceur2002; PARTIALLY VERIFIED for PBFT behavioural model; baseline mitigations are established implementation (engineering, not research).

### 5.2 Sybil identity operators

**Attacker capabilities:** Creates a large number of identities (node IDs, key pairs, signed node records) to gain disproportionate influence over peer discovery, message dissemination, or (if consensus is implemented) validator participation. Identity creation is cheap and unbounded; a public key or node ID authenticates a participant but does not limit how many identities an adversary can create.

**Attacker objectives:** Dominate peer discovery, eclipse victims, bias quorum formation, or exceed Byzantine thresholds by manufacturing identities.

**Valuable assets:** Network topology integrity, peer-discovery reliability, consensus quorum integrity (future), partition resilience.

**Relevant security property:** Safety (quorum integrity) and availability (discovery/dissemination).

**Required honest-participant assumptions:** Honest participants do not assume identity count bounds participation; Sybil resistance must come from a costly resource, not identity count.

**Required network assumptions:** Peer authentication exists (established implementation) but is not Sybil resistance; bootstrap sources are not trust roots; multiple independent bootstrap sources.

**Candidate mitigations:** Proof of Work, Proof of Stake, Proof of Space, or hybrid Sybil-resistance layer gating validator eligibility by a costly resource (work, stake, space). None selected; D-016 PROVISIONAL, D-017 OPEN RESEARCH, RS-001/RS-002 OPEN RESEARCH.

**Residual risk:** HIGH. Without Sybil resistance, an adversary can create unlimited identities and potentially dominate peer discovery, eclipse victims, or (in future) consensus participation. No Sybil-resistance mechanism is selected.

**Evidence:** Douceur2002 [S5, VERIFIED, full text] — Sybil attack = one entity, many identities; impossible to resist without central authority except under unrealistic resource parity/coordination; no specific solution endorsed. RS-002 §2 (concept distinctions), §3 (mechanism analysis), §4 (attack analysis); threat model T-02 (Sybil attacks, not mitigated, OPEN RESEARCH). Verification status: VERIFIED for Douceur2002; OPEN RESEARCH for all mitigations.

### 5.3 Malicious validator coalitions

**Attacker capabilities:** A coalition of validators (or would-be validators) controlling a fraction of voting power deviates arbitrarily: equivocates (double-signs/votes on conflicting views), withholds votes, censors transactions, colludes to reorg history, or attempts safety violations. Coordination may be off-chain and therefore not directly observable.

**Attacker objectives:** Double-spend, finalize conflicting history, censor transactions, deny liveness, extract value via ordering control (MEV).

**Valuable assets:** Consensus safety, finality, settlement finality, transaction ordering, censorship resistance.

**Relevant security property:** Safety (primary); censorship resistance and liveness (secondary).

**Required honest-participant assumptions:** Honest validators follow the protocol; the Byzantine voting-power fraction stays below the candidate's fault bound parameter (`f_bft` for BFT, `h_stake` for PoS, `h_pow` for PoW); quorum intersection holds over the voting-power distribution.

**Required network assumptions:** Partial synchrony for BFT liveness (DLS1988; HotStuff2019); honest-majority-of-work for Nakamoto liveness (Nakamoto2008); protocol-specific honest-stake and network assumptions for PoS (OuroborosPraos2017; Gasper2020).

**Candidate mitigations:** Quorum intersection and fault-bound parameters; slashing for equivocation (PoS finality gadgets); large, diverse validator set; inclusion lists; random leader/proposer rotation; epoch-based committee rotation.

**Residual risk:** Depends on the candidate architecture:
- BFT + stake (C1, C2): safety fails if Byzantine voting power ≥ `f_bft`; liveness fails under asynchrony or leader unavailability.
- Nakamoto PoW (C3): safety is probabilistic; <50% attacks (selfish mining, piggyback mining) are feasible below the 50% threshold under certain payoff structures; no slashing.
- PoS + finality gadget (C5): accountable safety (≥1/3 slashable on conflicting finality) qualified by ≥2/3 honest stake, slashing enforcement, weak subjectivity.

**Evidence:** CastroLiskov1999 [S4, PARTIALLY VERIFIED], HotStuff2019 [S8, VERIFIED abstract+full text QC/complexity], Nakamoto2008 [S7, VERIFIED per table], OuroborosPraos2017 [S13, VERIFIED abstract+§1+§2.2], Gasper2020 [S15, VERIFIED abstract+§§1–4], CasperFFG2017 [S14, PARTIALLY VERIFIED abstract+§§1–2+theorems], MajorityIsNotNeeded2023 [S32, VERIFIED abstract+paper], HanEtAl2021 [S31, VERIFIED abstract+paper], DaianEtAl2020 [S22, PARTIALLY VERIFIED metadata only, Ethereum-specific MEV], Bribers2025 [S37, VERIFIED abstract+paper], STAKESURE2024 [S34, VERIFIED abstract+paper]. Verification status varies by source as recorded in RS-013 §16 / RS-002 §8.

### 5.4 Economically powerful participants

**Attacker capabilities:** Acquires a large fraction of the scarce participation resource (stake, hashrate, or space) through purchase, accumulation, or early allocation, and uses it to influence voting power, proposer selection, or governance.

**Attacker objectives:** Governance capture, validator-set control, censorship, extraction of value, centralization of economic security.

**Valuable assets:** Voting power distribution, token supply/issuance, governance outcomes, economic security budget.

**Relevant security property:** Censorship resistance (primary); safety (secondary, if concentration breaks the fault bound); availability (secondary).

**Required honest-participant assumptions:** No single participant or coalition accumulates voting power beyond the fault bound parameter; stake/hashrate distribution remains sufficiently decentralized.

**Required network assumptions:** None specific; this is primarily an economic assumption.

**Candidate mitigations:** Non-linear reward curves, delegation caps, liquid-staking limits, stake distribution mechanisms, anti-whale entry design, governance design that resists stake-weighted capture (quadratic/liquid voting — unresolved).

**Residual risk:** Wealth concentration and stake concentration are economic hypotheses and design concerns, not protocol guarantees [IrresbergerYang2023 S39, PARTIALLY VERIFIED metadata only; RS-013 §6.13-6.14, §7.2]. For a new network, token value may be too low to fund adequate security budget (economic circularity) [CasperFFG2017 S14; Gasper2020 S15; RS-013 §7.14, D-015 OPEN RESEARCH].

**Evidence:** IrresbergerYang2023 [S39, PARTIALLY VERIFIED metadata only], CasperFFG2017 [S14, PARTIALLY VERIFIED], Gasper2020 [S15, VERIFIED], RS-013 §7 (validator economics), RS-002 §3.2 (PoS), §4 (wealth concentration attack row). Verification status: PARTIALLY VERIFIED for IrresbergerYang2023 (metadata only, does not establish universal law); VERIFIED for Gasper2020; economic-circularity claim is a design concern (RS-013 §20), not a measured result.

### 5.5 Mining and staking pool operators

**Attacker capabilities:** Operates a pool that aggregates hashrate (PoW) or delegated stake (PoS) from many participants. Directs the pooled resource toward block production, censorship, or attack strategies. Pool operators may not own the underlying resource (miners/delegators can exit), but pool concentration creates a de facto choke point.

**Attacker objectives:** Direct block production, censor transactions, execute selfish-mining or piggyback-mining strategies, extract MEV, influence governance via pooled votes.

**Valuable assets:** Hashrate/stake concentration, block-production control, censorship resistance, reward distribution.

**Relevant security property:** Censorship resistance (primary); safety and liveness (secondary).

**Required honest-participant assumptions:** Miners/delegators can switch pools freely; no single pool or pool coalition controls voting power beyond the fault bound parameter.

**Required network assumptions:** Pool-to-miner communication and reward distribution channels operational; exit/redirection possible.

**Candidate mitigations:** Permissionless exit (redirect hashrate/re-delegate stake), reward-only pooling (reduces operator control over block content), large number of independent pools, delegation caps, inclusion lists.

**Residual risk:** Pool centralization is the dominant PoW centralization vector [RS-002 §3.1; historically 4–5 pools controlled >51% of Bitcoin hashrate on many days — deployment observation, not independently verified by Kilo]. Delegation concentrates power in pool operators in PoS [RS-002 §3.2; RS-013 §6.6]. 51% rental attacks via hashrate markets reduce attack cost for smaller networks [MITDCI2020 S30, VERIFIED web page].

**Evidence:** MITDCI2020 [S30, VERIFIED web page: hashrate rental markets lower attack cost for small coins; 40+ documented reorgs 2019–2020], HanEtAl2021 [S31], MajorityIsNotNeeded2023 [S32], EyalSirer2014 [cited via secondary sources], RS-002 §3.1 (PoW), §3.2 (PoS pools), §4 (mining-pool concentration, delegation centralisation). Verification status: VERIFIED for MITDCI2020, HanEtAl2021, MajorityIsNotNeeded2023; EyalSirer2014 cited via secondary sources (PARTIALLY VERIFIED as primary); pool-concentration observation is deployment data, not a protocol guarantee.

### 5.6 Network-level attackers

**Attacker capabilities:** Operates at the network layer (ISP, AS-level adversary, or nation-state): delays, drops, reorders, or selectively withholds messages; partitions the network; launches eclipse attacks (controls all/most of a victim's peer connections); performs traffic analysis (metadata exposure: who talks to whom, when, how much).

**Attacker objectives:** Isolate victims, feed false views, disrupt liveness, censor transactions, deanonymize participants, enable double-spend via eclipse.

**Valuable assets:** Network connectivity, synchronization correctness, fork-choice integrity, privacy/metadata, transaction submission.

**Relevant security property:** Availability (primary); liveness (secondary); censorship resistance (secondary); safety (secondary, via eclipse-induced fork-choice manipulation).

**Required honest-participant assumptions:** At least one honest peer available for synchronization; nodes do not trust any single peer for state correctness.

**Required network assumptions:** Partial synchrony for BFT liveness; eventual reconnection after partition; authenticated peer sessions; bootstrap sources not trust roots.

**Candidate mitigations:** Multiple independent bootstrap sources, peer exchange, signed node record validation, diverse peer selection (not implemented), eclipse-detection heuristics (not implemented), multi-transport redundancy (internet + mesh + radio + satellite, D-019 PROVISIONAL; unimplemented beyond TCP/TLS), traffic-analysis resistance (OPEN RESEARCH, RS-003, D-020 PROVISIONAL).

**Residual risk:** A well-positioned adversary controlling bootstrap sources and peer-exchange responses could eclipse a newly joining or infrequently connected node. Traffic metadata is exposed (no privacy mechanism beyond TLS 1.3 encryption). HIGH residual risk for T-09 traffic analysis.

**Evidence:** EclipseAttacks2015 [S23, Heilman/Kendler/Zohar/Goldberg, USENIX Security 2015, VERIFIED abstract+paper: eclipse attack feasibility enables double-spend/selfish mining/adversarial forks], DLS1988 [S3, VERIFIED abstract+full text Table I], GilbertLynch2002 [S6, VERIFIED abstract+Theorem 1+full text: CAP], FLP1985 [S2, VERIFIED abstract+full text], RFC4838 [S17, VERIFIED full text], RFC9171 [S18, VERIFIED full text], RS-013 §8 (network resilience), threat model T-03 (eclipse, partially mitigated), T-04 (partition, partially mitigated), T-09 (traffic analysis, not mitigated). Verification status: VERIFIED for all theory sources; mitigations are engineering/research (OPEN).

### 5.7 Malicious bootstrap or infrastructure providers

**Attacker capabilities:** Operates bootstrap sources or infrastructure (DNS, hosting, cloud, VDF/timelord services, checkpoint services) that serve stale, incorrect, or malicious peer records, or withhold records from targeted nodes. May also provide weak-subjectivity checkpoints.

**Attacker objectives:** Deny initial connectivity, facilitate eclipse, influence joining nodes' view of the network, act as a de facto trust root.

**Valuable assets:** Peer-discovery reliability, initial connectivity, epoch/trusted-checkpoint trust.

**Relevant security property:** Availability (primary); safety (secondary, if checkpoint trust is abused).

**Required honest-participant assumptions:** Bootstrap sources are not trust roots; records are self-certifying and validated independently (signature, sequence, expiry, network ID); multiple independent bootstrap sources are used.

**Required network assumptions:** At least one honest bootstrap source reachable; peer exchange provides ongoing discovery after initial bootstrap.

**Candidate mitigations:** Multiple independent bootstrap sources, signed record validation, bootstrap source cross-validation (not implemented), bootstrap source health/reputation tracking (not implemented), dynamic bootstrap discovery (unresolved).

**Residual risk:** A malicious bootstrap source can withhold records (denial of initial connectivity) or serve records for malicious peers (facilitating eclipse). It cannot forge records for other nodes. If weak-subjectivity checkpoints are used (PoS), checkpoint service providers become trust roots — tension with P9 independent verification.

**Evidence:** Threat model T-08 (malicious bootstrap sources, partially mitigated); RS-002 §6.5 (checkpoint service providers as centralized infrastructure); EthereumWeakSubjectivity2024 [S40, VERIFIED docs]; BabylonBlog2023 [S41, VERIFIED web page]; node-roles.md §7 (bootstrap/discovery services, not trust roots); core-protocol-specification-v0.1.md §2.1 (network identifier, wrong-network rejection). Verification status: VERIFIED for weak-subjectivity sources; baseline bootstrap implementation is established (engineering); dynamic bootstrap discovery and cross-validation are UNRESOLVED.

### 5.8 Colluding merchants or transaction submitters

**Attacker capabilities:** Merchants or transaction submitters collude to double-spend (conflicting transactions to different recipients), front-run, censor via fee bribes, or submit malformed/conflicting transactions to exhaust mempool resources. May collude with validators/proposers.

**Attacker objectives:** Double-spend against merchants, extract value via ordering (MEF/MEV), censor competitors' transactions, exhaust mempool/block space.

**Valuable assets:** Transaction finality, merchant settlement, mempool resources, ordering control.

**Relevant security property:** Safety (double-spend prevention); censorship resistance (secondary); availability (mempool exhaustion).

**Required honest-participant assumptions:** Merchants wait for sufficient confirmation depth before accepting settlement; transaction validity rules are enforced by consensus; conflicting transactions are resolved by consensus ordering.

**Required network assumptions:** Transaction dissemination reaches proposers/validators; mempool conflict handling is deterministic; finality model (deterministic vs probabilistic) determines settlement risk.

**Candidate mitigations:** Confirmation-depth risk management (probabilistic finality), deterministic finality (reduces reorg/replay risk), mempool admission policy, replacement rules (RBF/nonce replacement — unresolved), inclusion limits, merchant settlement policies.

**Residual risk:** Under probabilistic finality (Nakamoto/PoS longest-chain), reorgs can reverse confirmed transactions; merchants must manage confirmation-depth risk. Under account-based nonce models, reorgs create nonce-replay risk [RS-015 §5.14]. MEV from ordering control is an observed deployment phenomenon in Ethereum [DaianEtAl2020 S22, PARTIALLY VERIFIED metadata only, Ethereum-specific] — not generalized to FST.

**Evidence:** Nakamoto2008 [S7, VERIFIED per table: probabilistic finality, exponential catch-up decay], PassEtAl2019 [S27, VERIFIED abstract+main theorem: honest-majority with delay margin], DaianEtAl2020 [S22, PARTIALLY VERIFIED metadata only], RS-015 §5.14 (confirmation/reorg implications by transaction model), RS-013 §9.9 (censorship, MEV). Verification status: VERIFIED for Nakamoto2008, PassEtAl2019; PARTIALLY VERIFIED for DaianEtAl2020 (Ethereum-specific); merchant settlement policy is a design concern (unresolved).

### 5.9 Compromised node software or implementation supply chains

**Attacker capabilities:** An independent implementation is compromised (malicious intent or bugs): censors transactions, selectively forwards, sends invalid state, crashes peers, or diverges in protocol interpretation. Supply-chain compromise of dependencies or build infrastructure.

**Attacker objectives:** Network interoperability failure, state inconsistency, censorship resistance violation, node availability.

**Relevant security property:** Availability (primary); safety (secondary, via state inconsistency); censorship resistance (secondary).

**Required honest-participant assumptions:** Independent implementations are expected but not trusted for correctness (principle P4); protocol specification is implementation-independent; conformance testing and test vectors validate interoperability.

**Required network assumptions:** Protocol messages are validated; incompatible implementations are detected and disconnected; no single implementation is a trust root.

**Candidate mitigations:** Protocol test vectors, conformance testing harness, independent-implementation compatibility matrix, machine-checkable formal specification (all unresolved; RS-012, D-035 OPEN RESEARCH), formal verification opportunities (model checking, theorem proving, adversarial simulation, fuzzing — RS-013 §10).

**Residual risk:** Independent implementations may diverge in protocol interpretation, causing network splits or incompatibility. Malicious implementations can censor or misbehave within protocol bounds. No test vectors or conformance harness exist yet.

**Evidence:** Threat model T-013 (malicious/compromised independent implementations, not mitigated, planned); RS-013 §11 (existing implementations: examine without copying), §10 (formal verification opportunities), §12 (comparison matrix qualification: qualitative only); RS-012 / D-035 (independent client implementation count, OPEN RESEARCH). Verification status: T-013 mitigations are engineering/planned (UNRESOLVED); RS-013 implementation references are bibliographic (PARTIALLY VERIFIED metadata for most).

## 6. Capability-to-threat-class mapping

| Capability | Threat classes where applicable |
|------------|--------------------------------|
| Unlimited peer identities | T-02 (Sybil), T-01 (node operators), T-07 (bootstrap) |
| Concentrating scarce participation resources | T-04 (economic participants), T-05 (pools), T-09 (bootstrap/infrastructure) |
| Adaptive corruption | T-03 (validator coalitions), T-05 (pools) |
| Validator collusion | T-03 |
| Bribery and coercion | T-03 (validator coalitions), T-08 (submitters) |
| Equivocation | T-03 |
| Transaction censorship | T-03, T-05, T-08 |
| Selective message withholding | T-01, T-03, T-06 |
| Network partitioning | T-06 |
| Eclipse attacks | T-06 |
| Message delay, loss, reordering | T-01, T-06 |
| Long-range / weak-subjectivity attacks | T-04 (stake-based), T-07 (checkpoint providers) |
| Nothing-at-stake / stake grinding | T-04 (PoS), T-05 (PoS pools) |
| Mining concentration / selfish mining | T-05 |
| Key compromise | T-01, T-07 (threat model T-07) |
| Infrastructure / geographic concentration | T-05, T-06, T-07 |
| Governance capture | T-04, T-05 |

## 7. Candidate architecture assumptions (summary table)

| Candidate | Sybil mechanism | Agreement/ordering | Fault-bound parameter | Honest assumption | Network assumption for liveness | Finality |
|-----------|----------------|--------------------|----------------------|-------------------|--------------------------------|----------|
| C1: HotStuff-style BFT + stake | Stake lockup | Leader-driven BFT, quorum certificates | `f_bft` (<1/3 Byzantine voting power) | Byzantine voting power < `f_bft` | Partial synchrony | Deterministic under fault bound |
| C3: Nakamoto PoW | Proof of Work (hashrate) | Longest chain, cumulative work | `h_pow` (<50% adversarial hashrate) | Honest hashrate ≥ `h_pow` | Honest-majority hash rate, delay-dependent margin | Probabilistic (k-depth) |
| C5: PoS + finality gadget (Gasper) | Stake lockup (Casper FFG) | LMD-GHOST fork choice + Casper FFG finality | `q_finality` (≥2/3 stake supermajority) | Honest stake ≥ `h_stake` (≥2/3) | Partial synchrony | Deterministic checkpoint (qualified by weak subjectivity/slashing) |
| C6: DAG + separate agreement (considered, not shortlisted) | External layer required | Tusk async BFT or Narwhal-HotStuff | Depends on composed layer | Depends on composed layer | Tusk: none (async randomized); HotStuff: partial synchrony | Deterministic (HotStuff) or randomized-probabilistic (Tusk) |

These parameters are **candidates for owner decision**, not FST-approved constants. No architecture is selected; all remain OPEN RESEARCH (D-016 PROVISIONAL, D-017 OPEN RESEARCH).

## 8. Residual risk statement

FST does not claim production security or full Sybil resistance. The most significant unmitigated threats are Sybil attacks (T-02), Byzantine participants/validator coalitions (T-03), and traffic analysis/metadata exposure (T-06), all of which depend on OPEN RESEARCH decisions (D-016, D-017, D-020). Network-layer threats (T-01 node operators, T-06 network-level, T-07 bootstrap) are partially mitigated by existing implementation (Milestones 2C–2D) but retain residual risks requiring engineering work. Economic threats (T-04 economically powerful participants, T-05 pools) are design concerns, not protocol guarantees, and cannot be resolved without monetary-policy and validator-economics decisions (D-015 OPEN RESEARCH).

## 9. Evidence and source register

| # | Title | Authors | Date | Venue | DOI/URL | Status | Material inspected | Claims supported | Limitations |
|---|-------|---------|------|-------|---------|--------|--------------------|------------------|-------------|
| S1 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Full text (Introduction, Service Properties) via HTML mirrors | 3f+1 bound; safety without synchrony; liveness under partial synchrony | O(n²)/view; quorum intersection not in inspected text |
| S2 | Impossibility of Distributed Consensus with One Faulty Process | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| S3 | Consensus in the Presence of Partial Synchrony | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST; Byzantine N≥3t+1 | Protocol-specific liveness guarantees |
| S4 | The Byzantine Generals Problem | Lamport, Shostak, Pease | 1982 | ACM TOPLAS 4(3) | doi:10.1145/357172.357176 | VERIFIED | Abstract + §§2–3 | 3f+1 oral-message bound; signed messages lift bound | Permissioned setting; synchrony assumptions |
| S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil attack = one entity, many identities; impossible without central authority except under unrealistic resource parity/coordination | No specific solution endorsed |
| S6 | Brewer's Conjecture (CAP) | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text | C+A+P impossible in async; consistency = atomic/linearizable | Local bounded operation during partitions |
| S7 | Bitcoin: A Peer-to-Peer Electronic Cash System | Nakamoto | 2008 | bitcoin.org whitepaper | bitcoin.org/bitcoin.pdf | VERIFIED | Full text | PoW = one-CPU-one-vote; longest chain; honest CPU majority; probabilistic finality; <50% hash | Deployment measurements; energy costs |
| S8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | ACM PODC | doi:10.1145/3293611.3331591; arXiv 1803.05069 | VERIFIED | Abstract + full text (QC, complexity) | O(n) per view (correct leader); QC = n−f = 2f+1; partial synchrony; safety always; liveness after GST | Worst case O(n²) under cascading leader failures |
| S13 | Ouroboros Praos: adaptively-secure semi-synchronous PoS | David, Gaži, Kiayias, Russell | 2017 | IACR ePrint 2017/573; EUROCRYPT 2018 | eprint.iacr.org/2017/573 | VERIFIED | Abstract + §1 + §2.2 + leader selection | VRF leader selection; fully adaptive security; semi-synchronous; honest majority of stake; probabilistic finality | Exact protocol parameters |
| S14 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arXiv | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems | Checkpoint finality; supermajority 2/3; two slashing conditions; accountable safety (≥1/3 slash); plausible liveness (≥2/3) | LMD-GHOST and weak subjectivity (not in FFG paper) |
| S15 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arXiv | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 | LMD-GHOST + Casper FFG; checkpoint finality; slashing; safety/plausible/probabilistic liveness | Exact checkpoint finality parameters |
| S16 | An Empirical Study of Consensus Protocols' DoS Resilience | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper | Leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size | Mitigation deployment experience |
| S17 | Delay-Tolerant Networking Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text | Store-carry-and-forward; intermittent contacts; no end-to-end contemporaneous path | Consensus over DTN specifics |
| S18 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc9171 | VERIFIED | Full text | Bundle protocol for DTN; persistent storage; asynchronous primitives | Consensus over DTN specifics |
| S22 | Flash Boys 2.0 (MEV) | Daian, Goldfeder, Kell, Li, Zhao, Bentov, Breidenbach, Juels | 2020 | IEEE S&P | doi:10.1109/SP40000.2020.00040 | PARTIALLY VERIFIED | Metadata only | MEV and transaction ordering evidence (Ethereum-specific) | Not universal; primary source not inspected by Kilo |
| S23 | Eclipse Attacks on Bitcoin's P2P Network | Heilman, Kendler, Zohar, Goldberg | 2015 | USENIX Security | eprint.iacr.org/2015/263 | VERIFIED | Abstract + paper | Eclipse attack feasibility; enables double-spend/selfish mining/adversarial forks | Mitigation effectiveness at scale |
| S27 | Analysis of Nakamoto Consensus | Pass, Seeman, shelat | 2019 | IACR ePrint 2019/943 | eprint.iacr.org/2019/943 | VERIFIED | Abstract + main theorem | Honest-majority condition with network-delay margin | — |
| S30 | 51% Attacks | MIT Digital Currency Initiative | 2020 | dci.mit.edu | dci.mit.edu/projects/51-percent-attacks | VERIFIED | Web page | Hashrate rental markets lower attack cost for small coins; 40+ reorgs 2019–2020 | Web page, not peer-reviewed |
| S31 | Fact and Fiction: Challenging the Honest Majority Assumption | Han, Sui, Yu, Liu, Chen | 2021 | IACR ePrint 2019/752 | eprint.iacr.org/2019/752.pdf | VERIFIED | Abstract + paper | Rational attack condition: payoff > cost + lost future revenue | — |
| S32 | Majority is not Needed (selfish mining) | — | 2023 | arxiv 2304.06313 | arxiv.org/html/2304.06313v2 | VERIFIED | Abstract + paper | <50% attacks via piggyback/selfish mining | — |
| S34 | STAKESURE: PoS with Strong Cryptoeconomic Safety | — | 2024 | arxiv 2401.05797 | arxiv.org/html/2401.05797 | VERIFIED | Abstract + paper | Cost-of-corruption ≥1/3 stake | — |
| S37 | Bribers, Bribers on The Chain | — | 2025 | arXiv 2509.17185 | arxiv.org/html/2509.17185v2 | VERIFIED | Abstract + paper | Bribery contracts (PayToAttest/PayToExit/PayToBias) | — |
| S39 | Coin Concentration of PoS Blockchains | Irresberger, Yang | 2023 | Economics Letters 229:111219 | doi:10.1016/j.econlet.2023.111219 | PARTIALLY VERIFIED | Metadata only | Observed block-production concentration and validator entry/exit | Does not establish universal law |
| S40 | Weak subjectivity | Ethereum Foundation | Current | ethereum.org | ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity | VERIFIED | Docs | Weak subjectivity; checkpoints; long-range defence; unbonding relationship | Protocol parameter values |
| S41 | Why is Stake Unbonding so Slow? | Babylon Labs | Current | babylonlabs.io | babylonlabs.io/blog/why-is-stake-unbonding-so-slow | VERIFIED | Web page | Unbonding > weak-subjectivity period; long-range defence via unbonding | — |

**FST internal documents (established implementation / research drafts):**
- `docs/fst/security/threat-model-v0.1.md` (T-01..T-013) — DRAFT, PROVISIONAL.
- `docs/fst/architecture/node-roles.md` — DRAFT, PROVISIONAL.
- `docs/fst/spec/core-protocol-specification-v0.1.md` — DRAFT, PROVISIONAL.
- `docs/fst/research/sybil-resistance-research.md` (RS-002) — DRAFT, OPEN RESEARCH.
- `docs/fst/research/consensus-validator-research.md` (RS-013) — RESEARCH DOCUMENT, OPEN RESEARCH.
- `docs/fst/research/transaction-model-research.md` (RS-015) — DRAFT, PROVISIONAL research deliverable, OPEN RESEARCH.

**Verification model note:** VERIFIED = bibliographic record and technical claim confirmed against primary source. PARTIALLY VERIFIED = metadata confirmed; claim based on abstract/docs/secondary. UNRESOLVED = not established. None of the above is an implementation endorsement. Kilo's inspection was metadata-only for several references per RS-013 §16.1.

## 10. Open questions

1. What is FST's explicit Byzantine adversary model (number of Byzantine validators, network synchrony, adversary resources, adversary goals)? (OPEN RESEARCH — D-017)
2. Which fault-bound parameter (`f_bft`, `h_stake`, `h_pow`, `q_finality`) applies to each candidate architecture, and what values respect FST's principles? (OPEN RESEARCH — parameters, not constants)
3. How does Sybil resistance map economic cost to voting power without breaking the consensus fault bound under stake/hashrate concentration? (OPEN RESEARCH — RS-002, RS-013 §9.2)
4. What is the relationship between token value and security budget for a new network? (OPEN RESEARCH — D-015)
5. How does FST prevent validator centralization (stake pools, mining pools, geographic concentration)? (OPEN RESEARCH)
6. How does FST handle long-range attacks, nothing-at-stake, and weak subjectivity if PoS is used? (OPEN RESEARCH)
7. How does Sybil resistance behave under network partitions and prolonged outages? (OPEN RESEARCH — D-018)
8. What are the entry/exit mechanics (bonding, unbonding, slashing) that preserve safety? (OPEN RESEARCH)
9. Can new/returning nodes independently verify chain selection without weak-subjectivity trust? (OPEN RESEARCH — P9, P10)
10. What per-peer rate limits, misbehaviour scoring, and peer disqualification are used? (Unresolved — engineering)

All items remain OPEN RESEARCH. No adversary threshold or security parameter is approved by this document.
