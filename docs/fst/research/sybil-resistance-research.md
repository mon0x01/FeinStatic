# FST Sybil-Resistance Research — v0.1

**Status:** DRAFT — research survey and architecture assessment.
**Research ID:** RS-002 (registered in [`research-register.md`](research-register.md)).
**Related decisions:** D-016 (permissionless validator participation, PROVISIONAL), D-017 (Byzantine fault tolerance research direction, OPEN RESEARCH), D-018 (offline financial operation, PROVISIONAL), D-019 (modular multi-transport communication, PROVISIONAL), D-036 (fork protection requirements, APPROVED).
**Classification:** OPEN RESEARCH — no Sybil-resistance mechanism, validator-selection mechanism, or voting-power model is approved or selected.

This document records dedicated research into Sybil-resistance mechanisms for FST. It is a **research and architecture milestone, not a mechanism selection milestone**. No Sybil-resistance mechanism, stake weight, bonding rule, or validator admission rule is selected or approved here.

RS-002 (Sybil-resistance mechanisms) and RS-013 (consensus algorithms and architecture) are **interdependent but not interchangeable**. Sybil resistance answers "what costly resource limits how much influence one actor can acquire?" Consensus answers "how do participants agree on a common sequence?" A Sybil mechanism alone does not provide agreement; a consensus algorithm alone does not provide Sybil resistance. Section 7 explicitly analyses credible combinations.

FST's existing NodeId and authenticated peer sessions (Milestones 2C–2D) authenticate participants but do **not** limit how many identities an adversary can create. Authentication ≠ Sybil resistance.

## 1. Executive summary

In a permissionless network, cryptographic identity is free to create, so an adversary can create unlimited identities at negligible cost [Douceur2002]. Without a costly or scarce resource gating participation, quorum thresholds and Byzantine fault bounds are meaningless: one actor can manufacture enough identities to exceed any threshold [Douceur2002; consensus-validator-research.md Section 3.3].

This document evaluates the main Sybil-resistance alternatives — Proof of Work, Proof of Stake, Delegated Proof of Stake, Proof of Space / Proof of Space-Time, Proof of Authority (comparison baseline only), reputation and social-trust mechanisms, and resource-based / hybrid approaches — against a common set of criteria. For each mechanism it examines: the scarce resource or trust assumption; what prevents identity multiplication; entry barriers; behaviour under adversarial concentration; independent verifiability; attack costs and incentives; exit and transfer; centralised infrastructure or gatekeepers; monetary-policy interaction; and partition behaviour.

A central finding is that **no mechanism is clearly superior across all FST principles**. PoW offers genuine permissionless entry with a long deployment track record but concentrates via mining pools and carries high energy cost. PoS aligns with FST's native asset but introduces economic circularity, wealth concentration, weak subjectivity, and long-range-attack surface. DPoS concentrates power in a small elected delegate set, conflicting with P1/P2. PoSpace/PoTime lowers ongoing cost but concentrates via large storage farms and adds VDF complexity. Reputation/social-trust/Proof-of-Personhood fails de facto Sybil resistance under economic incentive (puppeteering/oligopoly) and introduces exclusion and privacy issues.

Another central finding is that **the six concepts of node identity, peer admission, validator eligibility, validator selection, voting power, and consensus quorum formation must be treated separately**. A mechanism may resist Sybils at one layer but not another: PoW makes node identity cheap but ties voting power to hashrate; PoS makes identity cheap but ties voting power to stake; DPoS adds an election layer that concentrates voting power regardless of underlying stake distribution.

All findings remain OPEN RESEARCH. No mechanism is recommended for implementation.

## 2. Concept distinctions

These six concepts are frequently conflated; they must be treated separately when evaluating any Sybil-resistance mechanism or consensus composition.

| Concept | Definition | What it gates | Typical scarce resource |
|---|---|---|---|
| **Node identity** | A cryptographic (public-key) identifier for a network participant. | Whether a process can speak on the P2P layer. | None inherently (free to generate) |
| **Peer admission** | Whether a node is allowed to connect to / be accepted by peers. | P2P connectivity, gossip membership. | PoW, stake, storage, identity attestation, or nothing (open) |
| **Validator eligibility** | Whether an identity qualifies to become a validator. | Entry into the validator set (pre-selection filter). | Stake, hardware, bonded capital, identity credential |
| **Validator selection** | Which eligible validators are chosen to produce/attest blocks in a given round/slot. | Block author / attender selection. | Stake weight, hash power, storage share, randomness (VRF/VDF) |
| **Voting power** | The weight a participant's message carries in consensus/finality. | Fork-choice, quorum, finality. | Hashrate, staked tokens, delegated votes, storage space |
| **Consensus quorum formation** | The threshold of voting power needed to finalize / extend the canonical chain. | Safety/liveness boundary (e.g., >2/3, >1/2). | Aggregate of voting-power holders |

**Why this matters:** A mechanism may resist Sybils at one layer but not another. PoW makes node identity free (peer admission open) but makes voting power expensive (hashrate): splitting hashrate across 10,000 fake nodes yields no more influence than one node — "one-CPU-one-vote," not "one-node-one-vote" [Nakamoto2008; Douceur2002]. PoS likewise makes identity free but ties voting power to stake: creating many identities does not multiply voting power unless accompanied by more stake [OuroborosPraos2017; Gasper2020]. DPoS adds an election layer (validator selection via delegate vote) that concentrates voting power in a small elected set regardless of underlying stake distribution [LiXuDuan2023].

## 3. Mechanism analysis

Each candidate mechanism is examined below against the full criteria set from the mission. Verification status reflects source inspection per Section 8.

### 3.1 Proof of Work

**What scarce resource limits Sybil participation?** Computation / hash rate (electricity + specialized hardware) [Nakamoto2008].

**What prevents identity multiplication?** Node identity is free, but voting power is pegged to actual computational work performed [Nakamoto2008; Douceur2002]. Splitting hash power across many identities yields no more influence than one node.

**What does an honest participant need to join?** Mining hardware (ASIC/GPU), electricity, internet connection, mining software; optionally pool membership to smooth reward variance [MITDCI2020].

**Economic and infrastructure entry barriers:** CAPEX for mining hardware; ongoing electricity cost; cooling/facility; pool fees (typically 1–2%). High fixed cost creates a natural barrier but also concentrates where electricity is cheap [MITDCI2020].

**Behaviour under adversarial concentration:** Mining pools concentrate hashrate; historically 4–5 pools control >51% of Bitcoin hashrate on many days [blockchain.com; Chia docs]. Pool operators can direct hashrate but do not own it — miners can switch pools. Selfish mining and piggyback mining enable <50% attacks [EyalSirer2014; MajorityIsNotNeeded2023; HanEtAl2021]. Pool centralisation is the dominant PoW centralisation vector.

**Independent verification:** Yes — any node can verify a block's PoW by re-hashing; header verification is cheap. Full verification requires running a full node and storing the chain [Nakamoto2008].

**Attack costs and incentives:** 51% attack cost = cost to acquire/rent >50% hashrate. Hashrate rental markets (NiceHash etc.) reduce attack cost to marginal rental cost for smaller coins, with 40+ documented reorgs 2019–2020 [MITDCI2020]. Attack payoff must exceed cost plus lost future mining revenue (token destruction / exchange delisting) [HanEtAl2021].

**Exit and transfer:** Hardware can be resold or repurposed; no lock-up, no slashing, no unbonding. Mining pools can be exited instantly by redirecting hashrate.

**Centralised infrastructure or trusted gatekeepers:** Mining pools (reward distribution centralisation), ASIC manufacturers (hardware supply concentration), mining farms in low-cost-energy jurisdictions, exchanges (custody of mined rewards). No protocol-level gatekeeper, but pool operators are de facto choke points. No central authority required for entry.

**Interaction with monetary policy:** Block subsidy + transaction fees fund security; issuance schedule determines miner revenue and thus attack cost (security budget = hashrate value). Halvings reduce security budget unless price/fees rise; miner sell pressure circulates coins [Nakamoto2008].

**Behaviour under partitions:** Probabilistic finality (longest chain). Under partition, each side mines its own tip; on reconnection, the chain with most accumulated work wins, reorging the minority. No safety guarantee during partition; temporary forks resolved by work [Nakamoto2008]. Settlement finality is probabilistic, requiring confirmations.

**Sources:** Nakamoto2008 (VERIFIED — full text inspected); MITDCI2020 (VERIFIED); HanEtAl2021 ePrint 2019/752 (VERIFIED); MajorityIsNotNeeded2023 arxiv 2304.06313 (VERIFIED); EyalSirer2014 (VERIFIED — cited via secondary sources).

### 3.2 Proof of Stake

**What scarce resource limits Sybil participation?** Native token stake (economic capital locked as collateral) [OuroborosPraos2017; Gasper2020].

**What prevents identity multiplication?** Identity is free, but voting power is proportional to staked tokens. Creating many identities does not multiply voting power unless accompanied by more stake. Stake is divisible and reusable across identities/delegations, so identity-level Sybil cost is sublinear/constant once threshold stake is held [HCO2026].

**What does an honest participant need to join?** Acquire native tokens, run validator software, lock stake (bond), maintain online presence with correct key management [Gasper2020; EthereumPoS2024].

**Economic and infrastructure entry barriers:** Token acquisition cost (market price × minimum stake), validator hardware, uptime requirements, slashing risk, unbonding-period illiquidity, opportunity cost of locked capital. Ethereum: 32 ETH minimum; Zano: no minimum. Varies widely [Gasper2020; EthereumPoS2024].

**Behaviour under adversarial concentration:** Stake pools / custodial staking (exchanges stake user funds); whale concentration ("rich get richer" — stake begets rewards begets stake). Custodial concentration lets exchanges vote with user deposits. Wealth concentration directly maps to voting-power concentration [IrresbergerYang2023; CardanoPoSAttacks2024].

**Independent verification:** Block/header verification cheap; but chain selection from genesis requires weak subjectivity — new/returning nodes cannot fully independently determine canonical chain without an external checkpoint/trust source [EthereumWeakSubjectivity2024; CasperFFG2017].

**Attack costs and incentives:**
- **Stake majority attack (>50% stake):** Rewrite recent history, censor, double-spend. Cost = acquiring >50% of staked supply; market impact drives price up against attacker [Zano docs; CardanoPoSAttacks2024].
- **Nothing-at-stake:** Validators can vote on every fork at zero direct cost (unlike PoW, where mining on multiple chains splits hash rate). Slashing / economic finality required to deter [CasperFFG2017; Gasper2020; CardanoPoSAttacks2024]. Not universal — some designs mitigate natively [RS-013 Section 6.12].
- **Long-range attack:** An adversary who previously controlled stake rewrites history from an old checkpoint using withdrawn/sold keys. Mitigated by weak-subjectivity checkpoints, unbonding > weak-subjectivity period, key evolution, Bitcoin checkpointing (Babylon), or social consensus [EthereumWeakSubjectivity2024; CasperFFG2017; BabylonBlog2023; GažiEtAl2019].
- **Stake grinding:** Bias leader-election randomness (VRF grinding) — controlled by security parameters proportional to adversary hash power [OuroborosPraos2017].
- **Adaptive corruption:** Corrupt elected validators on the fly; Praos tolerates adaptive corruption with honest majority of stake [OuroborosPraos2017].
- **Bribery and collusion:** Pay validators to double-sign or withhold; slashing raises cost-of-corruption to ≥1/3 of total stake [STAKESURE2024; a16zSlashing2023]. Token toxicity alone is insufficient (socialized cost vs privatized bribe benefit) [Bribers2025].
- **Stake bleeding:** Minority long-range attack draining fees onto a private fork [GažiEtAl2019].
- **Posterior corruption:** Buy old keys from former stakeholders [CasperFFG2017; GažiEtAl2019].

**Slashing and exit:** Slashing penalises double-signing/equivocation (burns stake). Unbonding period delays liquidity after exit, creating a window to detect/slash misbehaviour; must exceed weak-subjectivity period [BabylonBlog2023; CasperFFG2017; Gasper2020]. Exit does not slash unless misbehaviour detected. Stake can be delegated/transferred in some designs; validator keys can be sold (enables posterior corruption / long-range attack).

**Centralised infrastructure or trusted gatekeepers:** Staking pools, custodial exchanges, liquid-staking derivatives (LSTs), node operators, checkpoint service providers (weak subjectivity). LSTs concentrate voting power in derivative managers [a16zSlashing2023].

**Interaction with monetary policy:** Staking rewards = inflation + fees; determines security budget and token dilution. Stakers earn yield; non-stakers are diluted. Circularity: token value funds security, security underpins token value — economic reflexivity [CasperFFG2017; Gasper2020; RS-013 Section 7.14].

**Behaviour under partitions:** Finality gadget (e.g., Casper FFG) requires 2/3 supermajority; under partition lacking 2/3, finality halts (safety preserved, liveness lost). Chain selection by weight of attestations; long reorgs possible after partition heals if checkpointing absent. Offline > weak-subjectivity period requires external trust to rejoin [EthereumWeakSubjectivity2024].

**Sources:** Gasper2020 (VERIFIED — arxiv full text inspected); CasperFFG2017 (PARTIALLY VERIFIED — arxiv abstract + §§1–2 inspected); OuroborosPraos2017 (VERIFIED — ePrint full text inspected); Ouroboros2017 (PARTIALLY VERIFIED — metadata + intro inspected); EthereumWeakSubjectivity2024 (VERIFIED — ethereum.org docs); EthereumPoS2024 (VERIFIED — ethereum.org docs); IrresbergerYang2023 (PARTIALLY VERIFIED — metadata only); CardanoPoSAttacks2024 (VERIFIED — cardano.org glossary); STAKESURE2024 (VERIFIED — arxiv); a16zSlashing2023 (VERIFIED — a16z frontier); Bribers2025 (VERIFIED — arxiv 2509.17185); GažiEtAl2019 (VERIFIED — NSF PAR); BabylonBlog2023 (VERIFIED — babylonlabs.io); HCO2026 (VERIFIED — arxiv 2601.03923).

### 3.3 Delegated Proof of Stake

**Delegation mechanics:** Token holders vote for a fixed small set of delegates/block producers (e.g., 21 on EOS, 27 on Tron, 101 on BitShares). Votes weighted by staked tokens; top-N elected take turns producing blocks in a schedule. Voting power can be delegated to proxies; votes may decay requiring periodic re-vote (EOS: 3-day stake lock, weekly re-vote, 2-year void) [BinanceResearchEOS2020; KomodoDPoS2020].

**Centralisation via delegates:** Power concentrated in 21–101 elected producers; collusion among top delegates enables censorship and governance capture. EOS 2018 allegations of mutual voting/vote-buying among producers (unproven). Binance Research: EOS governance lacks vote-trading resistance; exchanges vote with custodial tokens; top exchanges operate BPs with poor performance [BinanceResearchEOS2020].

**Voter apathy:** Large fraction of coins never used in elections (~half of coins never voted; few "decisive voters" can alter outcomes) [LiXuDuan2023]. Low turnout entrenches incumbents and reduces accountability.

**Entry barriers:** Delegates need infrastructure (EOS: 8GB RAM, ~190GB blockchain RAM growing 1KB/block; TRON: 64 cores, 64GB RAM, 50M bandwidth, 20T disk). Voters need tokens + active participation. Realistically voters choose known, well-resourced candidates — reputation barrier [BinanceResearchEOS2020; CoindaroDPoS2026].

**Collusion:** Vote trading/mutual voting; open unregulated vote market; producers incentivized to pay voters [BinanceResearchEOS2020; Whiteblock2019].

**Independent verification:** Block verification cheap; but quorum is small (21/27), so a colluding majority of the delegate set can finalize conflicting history without external detection until clients observe divergence [EOSIODecentralization2022].

**Attack costs and incentives:** Capture >1/3 of delegates to halt finality; >1/2 (or 15/21) to censor/reorg. Cost = acquiring delegate votes (vote buying) rather than raw stake — can be cheaper than PoS 51% because votes are tradable and apathetic holders sell cheap [BinanceResearchEOS2020].

**Exit and transfer:** Voters can re-delegate instantly; delegates exit by losing election/unbonding stake. No slashing typically (varies by chain) [KomodoDPoS2020].

**Centralised infrastructure or trusted gatekeepers:** Elected delegate set is a de facto permissioned gatekeeper; exchanges/custodians vote user tokens; geographic concentration (EOS: 14/21 BPs in Asia, 9 in China per CryptoSlate 2019) [CryptoSlateEOS2019].

**Interaction with monetary policy:** Block rewards funded by inflation (EOS originally 5%, redirected to BPs); delegate revenue creates consolidation incentive; reward distribution to voters creates vote-selling market [BinanceAcademyDPoS2018].

**Behaviour under partitions:** Small delegate quorum reaches fast finality under synchrony; under partition the delegate majority on each side may finalize conflicting chains; recovery requires re-election/social coordination. Higher centralisation means faster finality but weaker partition resilience than large PoW/PoS sets [BinanceResearchEOS2020].

**FST conflict:** DPoS concentrates power in 21–101 elected delegates — directly conflicts with P1/P2 (no unilateral/permanent authority) and P5 (participants reject upgrades). Voter apathy and vote buying are documented. Recorded here for completeness; not an FST candidate under current principles.

**Sources:** LiXuDuan2023 (VERIFIED — AAAI ICWSM); CoindaroDPoS2026 (VERIFIED); EOSIODecentralization2022 (VERIFIED — arxiv 2201.06187); KomodoDPoS2020 (VERIFIED); BinanceResearchEOS2020 (VERIFIED); BinanceAcademyDPoS2018 (VERIFIED); CryptoSlateEOS2019 (VERIFIED).

### 3.4 Proof of Space / Proof of Space-Time

**What scarce resource limits Sybil participation?** Storage capacity (disk space) plus time (verifiable delay function, VDF) [ChiaPoSpace2022; ChiaConsensus2022].

**What prevents identity multiplication?** Identity free, but chance to win a block is proportional to allocated storage space [ChiaConsensusIntro2022]. Splitting space across many identities yields no advantage — one unit of space = one unit of winning probability regardless of how many "farmers" it is split across [ChiaConsensusIntro2022].

**What does an honest participant need to join?** Disk storage, plotting (one-time computationally intensive plot generation, k32 ≈ 101.4 GiB), farming software, internet connection [ChiaFarmingOverview2022; ChiaFarmingRequirements2026].

**Economic and infrastructure entry barriers:** One-time plotting cost (RAM, SSD wear); storage CAPEX; ongoing near-zero farming cost (disk reads per challenge, minimal bandwidth/electricity). Minimum plot k32. Plot filter (1/512) randomly disqualifies plots per challenge [ChiaFarmingOverview2022].

**Behaviour under adversarial concentration:** Multi-PiB farms exist; no upper limit. Economies of scale in plotting and storage density favour large operators. Chia claims most decentralized (>100k nodes) because spare/laptop/desktop disk can participate [ChiaIntroduction2022]. Plot NFT pooling: farmer constructs own block; pool only distributes rewards — reduces pool-operator centralisation vs Bitcoin [ChiaConsensusIntro2022].

**Independent verification:** Proof of space fast to verify; proof of time (VDF) sequential and fast to verify. Any node can verify plots/challenges. Full node stores chain history [ChiaConsensusBasics2022].

**Attack costs and incentives:** To control block production, attacker needs >majority of total network space — expensive storage CAPEX. Plot grinding / grinding attacks on plot filter possible but bounded by plot filter design [ChiaPoSpace2022]. Reward structure (farmer 1/8, pool 7/8) discourages pool operators from withholding blocks [ChiaFarmingOverview2022].

**Exit and transfer:** Plots can be deleted/repurposed; storage resold; no slashing, no unbonding, no lock-up. Instant exit by stopping farming [ChiaFarmingOverview2022].

**Centralised infrastructure or trusted gatekeepers:** Timelords (VDF computation) — small number of timelord operators could influence challenge timing but not block content; large storage farms; pooling (reward-only, lower centralisation risk). No trusted gatekeeper for plot creation (permissionless) [ChiaIntroduction2022].

**Interaction with monetary policy:** XCH block rewards to farmers; farming rewards funded by issuance. Low ongoing cost means less sell pressure than PoW miners (no electricity to pay), potentially affecting token supply dynamics [ChiaIntroduction2022].

**Behaviour under partitions:** Nakamoto-style probabilistic finality with VDF-enforced block spacing. Under partition each side continues farming; on reconnection, heaviest chain (most accumulated proofs of space + time) wins. Reorgs resolve by space+time weight. Partition tolerance similar to PoW (probabilistic, no safety during partition) [ChiaConsensusBasics2022].

**Sources:** ChiaPoSpace2022 (VERIFIED — docs.chia.net); ChiaConsensus2022 (VERIFIED); ChiaConsensusIntro2022 (VERIFIED); ChiaFarmingOverview2022 (VERIFIED); ChiaFarmingRequirements2026 (VERIFIED); ChiaIntroduction2022 (VERIFIED); ChiaConsensusBasics2022 (VERIFIED); ChiaFAQ2022 (VERIFIED).

### 3.5 Proof of Authority — comparison baseline only

**Trusted gatekeepers / identity vetting:** A fixed set of trusted "authorities" (validators) pre-approved by a gatekeeper; write access and block production restricted to them. Identity vetted off-chain (KYC/reputation) [LiEtAl2024; Trustchain2023].

**What prevents identity multiplication?** The gatekeeper whitelists authorities; Sybil creation prevented by administrative identity vetting, not resource cost [LiEtAl2024].

**Honest participant needs:** Approval by governing authority; identity documentation; vetted node key [LiEtAl2024].

**Entry barriers:** Administrative/political, not economic. Permissioned by definition — cannot join without gatekeeper consent [LiEtAl2024].

**Behaviour under adversarial concentration:** Centralisation inherent — small set of authorities. Collusion or compromise of authorities breaks security. Geographic/jurisdictional concentration likely [LiEtAl2024].

**Independent verification:** Block verification cheap; but canonical chain definition is authoritative (authorities define it). No permissionless independent verification of validator-set membership [LiEtAl2024].

**Attack costs and incentives:** Compromise/bribe the authorities. Cost depends on off-chain vetting and legal contracts, not on-chain economics [LiEtAl2024].

**Exit and transfer:** Gatekeeper removes authority; keys rotated by governance. No economic unbonding [LiEtAl2024].

**Centralised infrastructure or trusted gatekeepers:** Yes — central to the design. Violates FST P1/P2/P8 [LiEtAl2024].

**Interaction with monetary policy:** Typically no native token or token has no security role; governance by authority consortium [LiEtAl2024].

**Behaviour under partitions:** Authorities on each side may diverge; resolution by gatekeeper/social consensus. Fast finality under synchrony, no partition safety without external coordination [LiEtAl2024].

**FST relevance:** PoA is explicitly NOT an FST candidate — conflicts with P1 (no unilateral control), P2 (no permanent privileged authority), P8 (no central emergency administrator). Included only as a baseline to show what FST rejects.

**Sources:** LiEtAl2024 (VERIFIED — Atlantis Press); Trustchain2023 (VERIFIED — arxiv 2305.08533); BesuPoA2022 (VERIFIED — arxiv 2208.04624).

### 3.6 Reputation and social-trust mechanisms

**Sybil resistance via social graph:** Web of Trust (PGP, 1992): identity certificates signed by trusted parties; Sybils poorly connected to trusted nodes identifiable [ViswanathEtAl2010; NortonWoT2020]. Graph-analysis defenses (SybilGuard, SybilLimit, SumUp, Whānau, Gatekeeper, SybilRank) bound Sybil regions via social-graph cuts [ViswanathEtAl2010].

**Limitations:**
- Privacy-invasive: requires disclosing all social ties [IdentityPersonhood2020].
- Detects only Sybil *regions* of sufficient size, not individual Sybils; other attacks wide open [IdentityPersonhood2020].
- Risks excluding legitimate poorly-connected users ("look like Sybil region") [IdentityPersonhood2020].
- Social graphs unique enough for re-identification [NarayananShmatikov2009].
- Trust cannot be easily quantified; only first-degree relationships fully trusted [IdentityPersonhood2020].
- "Proving not having alter egos" demands surveillance of contacts — surveillance-state dynamic [IdentityPersonhood2020].
- Online social networks poor identity bases: easy to create false nodes with real relationships; centralized platforms antithetical to self-sovereign identity [IdentityPersonhood2020].

**Reputation systems:** Track historical behavior; Sybil attack inflates reputation via fake entities [SocialReputationSurvey2022]. MeritRank bounds Sybil gain but does not eliminate. Reputation gaming/manipulation is a serious problem [SocialReputationSurvey2022].

**Proof of Personhood (PoP):** Aims for unique + singular identity (one human = one identifier), self-sovereign, privacy-preserving — the "Decentralized Identity Trilemma" [PoPReview2020]. Approaches: pseudonym parties, biometric/ZKP hybrids (Worldcoin, Humanity Protocol), vouching/stake-as-bet (HumanityDAO, Upala) [PoPReview2020].

**De jure vs de facto Sybil resistance:** PoP may achieve de jure resistance (filter humans from bots 1:1) but fail de facto resistance (humans acting like programmable bots / puppeteers controlling many accounts) [CompressedToZero2024]. "Compressed to 0": 23 entities (<0.6% of distinct entities) controlled ~40% of accounts and ~48% of rewards; 3 entities controlled ~19% accounts / ~24% rewards. Puppeteers pay participants to prove uniqueness in exchange for key control. Oligopoly emerges despite on-chain egalitarianism [CompressedToZero2024].

**Collusion-resistance:** De facto Sybil resistance ≈ collusion-resistance — cannot be disentangled. Economic incentives to periodically re-prove uniqueness create financial incentives for puppeteering [CompressedToZero2024].

**Entry barriers:** Physical/in-person events, biometric capture, Orb hardware, social connections, vouching — logistics burden, privacy concerns, excludes remote/low-infrastructure participants [PoPReview2020; CompressedToZero2024].

**Independent verification:** ZKP-based PoP can be independently verified; social vouching cannot (subjective) [PoPCrypto2026].

**Attack costs and incentives:** Sybil identity cheap; uniqueness proof costly. Bribery/coercion of human holders; key sale; puppeteering. Governance capture via one-person-one-vote subverted by hidden pools [CompressedToZero2024].

**Exit and transfer:** Credentials revocable/replaceable; reputation not easily transferable; persona credentials can be abandoned and restarted from zero [PoPReview2020].

**Centralised infrastructure or trusted gatekeepers:** Biometric PoP relies on issuing authorities/orb operators (centralized hardware); social web of trust relies on seed trusted nodes. Many PoP systems reintroduce central issuers [PoPReview2020].

**Interaction with monetary policy:** Often tied to UBI/airdrop distribution; one-person-one-reward undermined by puppeteering and oligopoly [CompressedToZero2024].

**Behaviour under partitions:** Social-graph mechanisms require connectivity to verify trust paths; partitions fragment the graph and break vouching/verification. Biometric PoP requires online verification infrastructure [PoPReview2020].

**Sources:** PoPReview2020 (VERIFIED — Frontiers in Blockchain); IdentityPersonhood2020 (VERIFIED — arxiv 2011.02412); CompressedToZero2024 (VERIFIED — assets.pubpub.org); PoPCrypto2026 (VERIFIED — IACR ePrint 2026/333); HCO2026 (VERIFIED — arxiv 2601.03923); SocialReputationSurvey2022 (VERIFIED — arxiv 2212.06436); NortonWoT2020 (VERIFIED).

### 3.7 Resource-based and hybrid approaches

**Hybrid PoW/PoS:** Both hash power and stake required. Attacker must control majority of both resources simultaneously (Zano: 51% attack needs majority hashrate + large share of stake; combinations like 20% hashrate vs 90% stake possible). Buying stake drives price up against attacker [ZanoConsensus2024; BinanceAcademyHybrid2019]. Decred: PoW miners find blocks, PoS ticket holders vote to finalize — stakeholders veto miners, removes secret-mine double-spend. Conflux: PoW + PoS chain for finality and committee voting. Zilliqa: PoW for Sybil resistance/shard committee election + PBFT within shards [ZilliqaHybrid2026].

**Trade-offs:** Higher attack cost (two resources) but added architectural complexity, latency (wait for both layers), developer/tooling accessibility. Security budget split across two mechanisms [ZanoConsensus2024; Synergeia2025].

**Proof of space + time (Chia):** Covered in Section 3.4 [ChiaPoSpace2022].

**Proof of personhood + resource hybrids:** Human Challenge Oracle (HCO): real-time human effort as non-reusable, non-parallelizable resource — linear Sybil cost Ω(s) vs constant/sublinear for PoW/PoS. Complements (does not replace) PoW/PoS; addresses identity-level Sybils that resource consensus alone doesn't prevent [HCO2026]. RTCT protocol: random recurring cryptographic challenges + token burn per node — ongoing "tax" on identities, exponentially increases attacker cost with difficulty [RTCT2026].

**Resource cost scaling summary (HCO2026 Table 2):**
- PoW: computation, reusable, sublinear/constant cost scaling
- PoS: capital, reusable, sublinear/constant cost scaling
- HCO: human effort, not reusable, linear cost scaling

**Adaptive Sybil resistance / economic web of trust:** Influence must be costly though identity is cheap. Credit influence tied to observable lending relationships (capital at risk, repayment history). Reputation is not replaceable — burned credentials make repeat attacks progressively more expensive [EconomicWebOfTrust2026].

**Sources:** ZanoConsensus2024 (VERIFIED — docs.zano.org); BinanceAcademyHybrid2019 (VERIFIED); Synergeia2025 (VERIFIED — IACR ePrint 2025/2138); InterleavingHybrid2019 (VERIFIED — arxiv 1911.09262); ConfluxPoS2024 (VERIFIED); ZilliqaHybrid2026 (VERIFIED); ThreatLandscapeSybil2026 (VERIFIED — MDPI); RTCT2026 (VERIFIED — CEUR-WS Vol-4223); HCO2026 (VERIFIED); EconomicWebOfTrust2026 (VERIFIED — iog.io); Anduin2026 (VERIFIED — arxiv 2611.05586).

## 4. Attack analysis

Concrete attacks mapped to vulnerable mechanisms and mitigations:

| Attack | Description | Primary vulnerable mechanisms | Mitigations |
|---|---|---|---|
| **Identity multiplication** | Creating many fake identities to gain influence | All (if identity = voting power); PoW/PoS resist via resource peg | Resource peg (hashrate/stake/space); PoP uniqueness |
| **Cheap-identity and resource amplification** | Splitting one resource across identities | PoW (sublinear marginal cost), PoS (stake divisible/reusable) | None fully — PoW/PoS have weak identity-level resistance |
| **Stake concentration and pooling** | Wealth/stake concentrates voting power | PoS, DPoS, hybrid PoS | Slashing, liquid-staking limits, delegation caps |
| **Mining-pool concentration** | Pools control >50% hashrate | PoW | Miners can switch pools; Chia-style reward-only pooling |
| **Delegation centralisation** | Elected delegate set colludes | DPoS | Larger delegate sets, vote decay, anti-vote-trading rules |
| **Nothing-at-stake** | Sign every fork at zero cost | PoS | Slashing, economic finality, longest-chain with penalties |
| **Long-range attacks** | Fork from genesis/past using withdrawn/stale keys | PoS | Weak subjectivity checkpoints, unbonding > WSP, key evolution, Bitcoin checkpointing, social consensus |
| **Stake grinding** | Bias leader-election randomness | PoS (VRF-based) | Security parameters vs adversary hash power (Praos) |
| **Adaptive corruption** | Corrupt elected validators on the fly | PoS, DPoS | Honest-majority adaptive security (Praos), short epochs |
| **Bribery and collusion** | Pay validators/delegates to misbehave | PoS, DPoS, PoW pools | Slashing raises cost-of-corruption (≥1/3 stake), masked/stochastic voting |
| **Censorship** | Block/exclude transactions | DPoS (small quorum), PoS (majority), PoW (pool collusion) | Large validator set, inclusion lists, fork-choice rules |
| **Wealth concentration** | Rich get richer (stake begets stake) | PoS, DPoS | Reward distribution design, non-linear reward curves |
| **Infrastructure and geographic concentration** | Mining farms / validators in few jurisdictions | PoW, PoS, DPoS | Permissionless entry, decentralized hardware/energy |
| **Governance capture** | Vote buying, plutocracy, delegate cartels | DPoS, PoS (on-chain governance) | Quadratic/liquid democracy, bribery-resistant voting |

**Sources:** CardanoPoSAttacks2024 (VERIFIED); Bribers2025 (VERIFIED); STAKESURE2024 (VERIFIED); GažiEtAl2019 (VERIFIED); HanEtAl2021 (VERIFIED); MajorityIsNotNeeded2023 (VERIFIED); MITDCI2020 (VERIFIED); BinanceResearchEOS2020 (VERIFIED); CompressedToZero2024 (VERIFIED).

## 5. Sybil resistance vs consensus — boundary analysis

Sybil resistance and consensus are separable concerns, but their boundary matters for FST:

- **Node identity and peer admission** are network-layer concerns (RS-014 / Milestones 2C–2D). Authenticated TLS sessions + signed node records provide peer admission but no Sybil resistance [threat-model-v0.1.md T-02].
- **Validator eligibility** is the Sybil-resistance layer: it decides who may become a validator based on a costly resource (stake, work, space, or other).
- **Validator selection, voting power, and quorum formation** are consensus-layer concerns: they decide how much weight each eligible validator carries and what threshold finalises a block.

**Key insight:** Sybil resistance converts a cheap resource (identity) into an expensive one (work/stake/space). Consensus then assumes the resulting voting-power distribution respects its fault bound (e.g., <1/3 Byzantine for classical BFT, honest-majority-of-stake for PoS, <50% hashrate for PoW). The two must be co-designed: a Sybil mechanism that produces highly concentrated voting power breaks the consensus fault bound even if the Sybil mechanism itself "works" [Douceur2002; consensus-validator-research.md Section 9.2].

**FST's NodeId and authenticated peer sessions do not, by themselves, provide Sybil resistance** [threat-model-v0.1.md T-02]. They authenticate but do not bound identity multiplicity.

## 6. Composition with consensus — candidate combinations

Sybil-resistance mechanisms and consensus families are not interchangeable; they must be composed deliberately. Putting two established mechanisms together does not automatically create a secure protocol [consensus-validator-research.md Section 2.10]. Each combination below is analysed for: which component provides Sybil resistance, which provides agreement/ordering, how voting power/proposer probability is derived, adversary assumptions, coherence of the security argument, effect of validator entry/exit on safety, compatibility with FST's decentralisation principles, and remaining research gaps.

### Candidate C1: HotStuff-style BFT + stake-based validator eligibility

- **Sybil resistance:** Stake lockup (PoS). Validator eligibility = bonded stake above threshold [Gasper2020].
- **Agreement and ordering:** HotStuff view-based BFT with quorum certificates; leader-driven, deterministic finality under partial synchrony [HotStuff2019].
- **Voting power / proposer probability:** Stake-weighted voting power; proposer selected round-robin or stake-weighted from active committee [HotStuff2019; DiemBFT2020].
- **Adversary assumptions:** <1/3 Byzantine voting power (3f+1 bound); partial synchrony for liveness; stake not concentrated beyond fault bound [CastroLiskov1999; HotStuff2019].
- **Coherent security argument?** Conditionally — requires stake layer to map economic cost to voting power, and HotStuff's 3f+1 bound to hold over the stake-weighted set. Coherence depends on stake distribution respecting the fault bound; concentration breaks it [Gasper2020; IrresbergerYang2023].
- **Validator entry/exit effect on safety:** Entry changes committee composition; exit (unbonding) must exceed slashing detection window to preserve accountable safety [CasperFFG2017; Gasper2020]. Rapid churn can temporarily concentrate voting power.
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016, but leader DDoS target and stake centralisation challenge P1/P4. Epoch-based reconfiguration (DiemBFT) helps entry/exit [DiemBFT2020].
- **Research gaps:** Stake economics (D-015), unbonding/slashing parameters, leader DoS mitigation, committee rotation frequency.

### Candidate C2: Tendermint/CometBFT-style BFT + stake-weighted voting

- **Sybil resistance:** Stake lockup (PoS) [Gasper2020].
- **Agreement and ordering:** Round-based Propose/Prevote/Precommit; deterministic finality under >2/3 voting power [CometBFT spec].
- **Voting power / proposer probability:** Stake-weighted voting power; proposer selected round-robin proportional to voting power [CometBFT spec].
- **Adversary assumptions:** <1/3 Byzantine voting power; partial synchrony for liveness [CometBFT spec; DLS1988].
- **Coherent security argument?** Conditionally — CometBFT's spec confirms the BFT instance; stake-weighted voting power is external. Same coherence caveat as C1: stake concentration breaks the >2/3 assumption [CometBFT spec; Gasper2020].
- **Validator entry/exit effect on safety:** Epoch/height-based validator set changes; voting-power weight changes require reconfiguration. Unbonding > slashing window [CasperFFG2017].
- **FST decentralisation compatibility:** Partially — deterministic finality and predictable proposer rotation aid liveness analysis, but leader DDoS surface (AsiaCCS2024) and stake concentration challenge P1/P4.
- **Research gaps:** Communication complexity at scale (PBFT-style voting), leader DoS mitigation, stake-weight reconfiguration safety.

### Candidate C3: Nakamoto consensus + computational-work admission

- **Sybil resistance:** Proof of Work (hashrate cost) [Nakamoto2008].
- **Agreement and ordering:** Longest-chain (cumulative work) fork choice; probabilistic finality [Nakamoto2008].
- **Voting power / proposer probability:** Hashrate-weighted; puzzle solver proposes [Nakamoto2008].
- **Adversary assumptions:** <50% adversarial hashrate (honest-majority-of-work); network-delay-dependent margin [PassEtAl2019; Nakamoto2008].
- **Coherent security argument?** Yes — PoW is both Sybil mechanism and block-author selector; the honest-majority-of-work assumption is self-consistent [Nakamoto2008; PassEtAl2019]. Well-understood deployment track record (Bitcoin).
- **Validator entry/exit effect on safety:** Open entry/exit; hashrate can enter/leave freely. Safety degrades gracefully with hashrate share; no fixed committee to reconfigure.
- **FST decentralisation compatibility:** Partially — genuinely permissionless (D-016), no stake concentration at genesis, but mining-pool concentration and energy cost challenge P1/P4; probabilistic finality challenges high-value settlement (scope-v0.1.md).
- **Research gaps:** Finality latency for settlement, energy cost, pool centralisation mitigation, 51% rental-attack economics for smaller networks [MITDCI2020].

### Candidate C4: PoS longest-chain + stake-weighted proposer selection and fork choice

- **Sybil resistance:** Stake lockup (PoS) [OuroborosPraos2017].
- **Agreement and ordering:** Stake-weighted slot-leader election (VRF) + chain selection; probabilistic finality with settlement delay [OuroborosPraos2017].
- **Voting power / proposer probability:** Stake-weighted; VRF hides slot leader until they speak [OuroborosPraos2017].
- **Adversary assumptions:** Honest majority of stake; semi-synchronous (unknown delay bound); adaptive corruption [OuroborosPraos2017].
- **Coherent security argument?** Yes — Praos provides provably secure PoS longest-chain with adaptive security under honest-stake-majority [OuroborosPraos2017]. Finality is probabilistic, not deterministic.
- **Validator entry/exit effect on safety:** Stake-weighted eligibility changes per epoch; unbonding/slashing windows apply [OuroborosPraos2017; CasperFFG2017].
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016 and native asset (D-014), but stake concentration, long-range attacks, nothing-at-stake, and weak subjectivity challenge P1/P9/P10 [Gasper2020; EthereumWeakSubjectivity2024].
- **Research gaps:** Weak-subjectivity checkpointing for independent verification (P9), long-range-attack defence, settlement-latency requirements for financial settlement.

### Candidate C5: PoS + separate finality gadget (Gasper-style)

- **Sybil resistance:** Stake lockup via Casper FFG [Gasper2020].
- **Agreement and ordering:** LMD-GHOST fork choice + Casper FFG finality gadget [Gasper2020].
- **Voting power / proposer probability:** Stake-weighted; proposer assigned per slot [Gasper2020].
- **Adversary assumptions:** ≥2/3 honest stake for finality; accountable safety (≥1/3 slashable on conflicting finality); partial synchrony for liveness [Gasper2020; CasperFFG2017].
- **Coherent security argument?** Yes — Gasper formally combines fork choice and finality; accountable safety theorem [CasperFFG2017; Gasper2020]. Finality is deterministic checkpoint finality qualified by Byzantine-stake and weak-subjectivity assumptions [Gasper2020; EthereumWeakSubjectivity2024].
- **Validator entry/exit effect on safety:** Validator set changes per epoch; unbonding > weak-subjectivity period required to prevent long-range attacks after exit [BabylonBlog2023; EthereumWeakSubjectivity2024].
- **FST decentralisation compatibility:** Partially — permissionless staking aligns with D-016, but stake concentration, custodial/LST centralisation, and weak-subjectivity trust challenge P1/P9/P10 [a16zSlashing2023; EthereumWeakSubjectivity2024].
- **Research gaps:** Weak-subjectivity period vs unbonding, slashing accuracy, inactivity-leak recovery, stake economics (D-015).

### Candidate C6: DAG-based dissemination + separate agreement/finality (Narwhal/Tusk-style)

- **Sybil resistance:** External stake/work layer required (none intrinsic to Narwhal/Tusk) [NarwhalTusk2022].
- **Agreement and ordering:** Narwhal DAG mempool for dissemination (asynchronous, tolerates asynchrony); Tusk async BFT consensus or Narwhal-HotStuff for ordering [NarwhalTusk2022].
- **Voting power / proposer probability:** Depends on composed consensus (HotStuff/Tusk) + external Sybil layer [NarwhalTusk2022].
- **Adversary assumptions:** Tusk tolerates asynchrony with randomized probabilistic termination; Narwhal-HotStuff inherits partial-synchrony liveness [NarwhalTusk2022].
- **Coherent security argument?** Partially — separation is well-defined empirically (130k+ tx/s WAN reported), but the combined system's formal security argument depends on the chosen consensus layer and Sybil layer; no single unified proof [NarwhalTusk2022].
- **Validator entry/exit effect on safety:** Depends on composed consensus; dissemination layer is independent of validator set [NarwhalTusk2022].
- **FST decentralisation compatibility:** Partially — separation may suit multi-transport networks (dissemination and consensus have different communication requirements) [D-019; consensus-validator-research.md Section 8.10], but complexity and less deployment history are concerns.
- **Research gaps:** Formal security of composition, Sybil layer integration, deployment track record, multi-transport adaptation.

### Candidate C7: Hybrid PoW/PoS (both hash power and stake required)

- **Sybil resistance:** Both PoW and PoS — attacker must control majority of both resources simultaneously [ZanoConsensus2024; Synergeia2025].
- **Agreement and ordering:** Protocol-specific (e.g., PoW finds blocks, PoS tickets vote to finalize; PoW + PoS chain for finality) [ZanoConsensus2024; ConfluxPoS2024].
- **Voting power / proposer probability:** Combined hashrate + stake weight [ZanoConsensus2024].
- **Adversary assumptions:** Honest majority of both resources; higher attack cost than either alone [ZanoConsensus2024].
- **Coherent security argument?** Conditionally — higher attack cost but added complexity and latency; security budget split across two mechanisms [Synergeia2025; InterleavingHybrid2019].
- **Validator entry/exit effect on safety:** Both layers' entry/exit interact; complexity increases with two Sybil layers [ZanoConsensus2024].
- **FST decentralisation compatibility:** Medium — permissionless on both layers but operational complexity high; aligns with neither extreme.
- **Research gaps:** Layer-interaction safety, economic security budget split, implementation complexity.

## 7. Network and financial requirements

Evaluated against FST's intended operating conditions (scope-v0.1.md):

**Globally distributed participants:** All candidate compositions assume geographically distributed validators. Geographic diversity improves partition resilience but increases latency (speed of light) — a physical constraint, not a protocol choice [consensus-validator-research.md Section 8.8].

**Internet connectivity with varying latency and reliability:** BFT protocols (C1, C2, C5) assume reliable point-to-point channels and partial synchrony for liveness; variable latency increases timeouts and degrades liveness [consensus-validator-research.md Sections 8.3, 8.9]. PoW/PoS longest-chain (C3, C4) are more tolerant of variable delay but probabilistic finality needs longer confirmation under high delay [Nakamoto2008; PassEtAl2019].

**Future mesh, radio, and satellite connectivity:** These introduce intermittent, high-delay links resembling asynchrony. FLP rules out deterministic consensus with guaranteed liveness under pure asynchrony [FLP1985]. DLS shows safety-always/liveness-after-GST is achievable under partial synchrony [DLS1988]. FST consensus must separate safety (always preserved) from liveness (conditional on connectivity recovery) [consensus-validator-research.md Section 3.1].

**Delay-tolerant or store-and-forward communication (future):** DTN (RFC 4838, RFC 9171) provides eventual delivery with unbounded delay — maps to asynchrony; consensus safety preserved, liveness suspended until contact returns [RFC4838; RFC9171]. Narwhal's asynchronous mempool demonstrates dissemination can tolerate asynchrony while a consensus layer provides ordering [NarwhalTusk2022] — a relevant pattern for multi-transport FST, but consensus over DTN is open research [consensus-validator-research.md Section 8.10].

**Native FST as the initial asset:** Sybil mechanisms using stake (C1, C2, C4, C5, C7) require FST to have market value to fund security — economic circularity for a new network [CasperFFG2017; Gasper2020; RS-013 Section 7.14]. PoW (C3) requires hardware/energy expenditure independent of FST value initially, but security budget still scales with token value [Nakamoto2008].

**Person-to-person, merchant and institutional payments:** All compositions can carry payments; finality model determines settlement risk. Deterministic finality (C1, C2, C5) suits high-value settlement; probabilistic finality (C3, C4) requires confirmation-depth risk management [Nakamoto2008; OuroborosPraos2017].

**High-value settlement requirements:** Deterministic finality preferred for high-value settlement (no reversion under assumptions); probabilistic finality requires economic finality (reversion cost exceeds transaction value) — unresolved for FST [D-017; consensus-validator-research.md Section 3.11].

**Independent implementation teams:** Protocol specification must be implementation-independent [consensus-validator-research.md Section 11]. Formal verification opportunities (model checking, theorem proving) increase confidence for independent implementations [consensus-validator-research.md Section 10].

**Network forks and replay protection:** D-036 (APPROVED) requires unique network identity, replay protection, distinct currency identifiers for independent forks. Fork choice and finality model affect fork behaviour; all compositions need fork-protection mechanisms at the transaction layer (D-036 mechanisms OPEN RESEARCH) [scope-v0.1.md; transaction-model-research.md Section 5.4].

**Bounded offline operation (future):** CAP — during a partition, unrestricted local spending and global double-spend prevention cannot both be guaranteed [GilbertLynch2002; scope-v0.1.md Offline financial operation]. Bounded offline spending authority (AP-style: local availability during disconnection, reconciliation on reconnect) is the FST direction (D-018 PROVISIONAL). Consensus layer does not provide offline finality; a separate offline-payment architecture must be composed [consensus-validator-research.md Section 3.13].

**Do not claim:** All regions can independently achieve unrestricted spending and global finality while disconnected — this is impossible under CAP/FLP [GilbertLynch2002; FLP1985].

**Initial internet-connected test network vs future multi-transport:** Initial requirements assume internet connectivity with partial synchrony; multi-transport (mesh/radio/satellite/DTN) is a future ambition requiring consensus adaptation to unbounded delay [D-019 PROVISIONAL; consensus-validator-research.md Section 8.10].

## 8. Verified source records

Each reference below records: bibliographic metadata, material inspected, exactly what was verified, what was not verified, limitations, and verification status. Metadata-only inspection is **not** labelled as full technical verification.

| # | Title | Authors | Date | Venue | DOI/URL | Status | Material inspected | What it establishes | What it does NOT establish |
|---|---|---|---|---|---|---|---|---|---|
| S1 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Full text (Introduction, Service Properties) via HTML mirrors | 3f+1 bound; safety without synchrony; liveness under partial synchrony (weak synchrony); optimal resiliency | O(n²)/view; quorum intersection (not stated in inspected text) |
| S2 | Impossibility of Distributed Consensus with One Faulty Process | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| S3 | Consensus in the Presence of Partial Synchrony | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST; Byzantine N≥3t+1 | Protocol-specific liveness guarantees |
| S4 | The Byzantine Generals Problem | Lamport, Shostak, Pease | 1982 | TOPLAS 4(3) | doi:10.1145/357172.357176 | VERIFIED | Abstract + §§2–3 | 3f+1 oral-message bound; signed messages lift bound; interactive consistency | Permissioned setting; synchrony assumptions |
| S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil attack = one entity, many identities; impossible without central authority except under unrealistic resource parity/coordination | No specific Sybil-resistance solution endorsed |
| S6 | Brewer's Conjecture and the Feasibility of Consistent, Available, Partition-Tolerant Web Services | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text | C+A+P impossible in async; consistency = atomic/linearizable; trade-off during partitions | Local bounded operation during partitions |
| S7 | Bitcoin: A Peer-to-Peer Electronic Cash System | Nakamoto | 2008 | bitcoin.org whitepaper | bitcoin.org/bitcoin.pdf | VERIFIED | Full text | PoW = one-CPU-one-vote; longest chain; honest CPU majority → outpace attackers; probabilistic finality, exponential catch-up decay | Deployment measurements; energy costs |
| S8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | PODC | doi:10.1145/3293611.3331591; arXiv 1803.05069 | VERIFIED | Abstract + full text (QC, complexity) | O(n) per view (correct leader); QC = n−f = 2f+1; partial synchrony, safety always, liveness after GST | Worst case O(n²) under cascading leader failures |
| S9 | CometBFT consensus specification | CometBFT project | Current (main) | GitHub | github.com/cometbft/cometbft/blob/main/spec/consensus/consensus.md | VERIFIED | Full spec text | Round-based Propose/Prevote/Precommit; >2/3 thresholds; voting power; round-robin proposer; safety <1/3 byzantine voting power | Not a permissionless admission mechanism |
| S10 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys | doi:10.1145/3492321.3519594; arXiv 2105.11827 | VERIFIED | Abstract + full text (§§1, 2, 3.1, 5) | Mempool/consensus separation; Narwhal DAG; Tusk async BFT, zero extra communication, wait-free | Deployment throughput; exact per-transaction complexity |
| S11 | Ouroboros: A Provably Secure Proof-of-Stake Blockchain Protocol | Kiayias, Russell, David, Oliynykov | 2017 | CRYPTO | doi:10.1007/978-3-319-63688-7_12; ePrint 2016/889 | PARTIALLY VERIFIED | Abstract + Introduction + model | Stake-weighted slot-leader election; epochs/slots; honest majority; synchronous model | VRF leader selection (belongs to Praos); settlement delay |
| S12 | Ouroboros Praos: An adaptively-secure, semi-synchronous proof-of-stake protocol | David, Gaži, Kiayias, Russell | 2017 | IACR ePrint 2017/573; EUROCRYPT 2018 | eprint.iacr.org/2017/573 | VERIFIED | Abstract + §1 + §2.2 + leader selection | VRF leader selection; fully adaptive security; semi-synchronous/unknown delay; honest majority of stake; probabilistic finality with settlement delay | Exact protocol parameters |
| S13 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | arXiv 1710.09437 | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems | Checkpoint finality; supermajority 2/3; two slashing conditions; accountable safety (≥1/3 slash); plausible liveness (≥2/3) | LMD-GHOST and weak subjectivity (Gasper/Ethereum docs, not FFG paper) |
| S14 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arXiv 2003.03052 | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 | LMD-GHOST + Casper FFG; checkpoint finality; slashing (S1/S2); safety/plausible/probabilistic liveness | Exact checkpoint finality parameters |
| S15 | Eclipse Attacks on Bitcoin's Peer-to-Peer Network | Heilman, Kendler, Zohar, Goldberg | 2015 | USENIX Security | eprint.iacr.org/2015/263 | VERIFIED | Abstract + paper | Eclipse attack feasibility; enables double-spend/selfish mining/adversarial forks | Mitigation effectiveness at scale |
| S16 | An Empirical Study of Consensus Protocols' DoS Resilience | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper | Leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size | Mitigation deployment experience |
| S17 | Delay-Tolerant Networking Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text | Store-carry-and-forward; intermittent contacts; no end-to-end contemporaneous path | Consensus over DTN specifics |
| S18 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc/rfc9171 | VERIFIED | Full text | Bundle protocol for DTN; persistent storage; asynchronous message primitives | Consensus over DTN specifics |
| S19 | Characterizing Coin-Based Voting Governance in DPoS | Li, Xu, Duan | 2023 | AAAI ICWSM | ojs.aaai.org/index.php/ICWSM/article/download/22225/22004/26288 | VERIFIED | Abstract + paper | Voter apathy; few decisive voters; coin-based voting centralisation | Protocol-specific mitigation efficacy |
| S20 | Understanding the Decentralization of DPoS (EOSIO) | — | 2022 | arxiv 2201.06187 | arxiv.org/abs/2201.06187 | VERIFIED | Abstract + paper | EOS delegate-set centralisation; vote-buying vulnerability | Generalization beyond EOS |
| S21 | Decentralisation, governance and EOS | Binance Research | 2020 | binance.com | binance.com/en-IA/research/analysis/eos-governance | VERIFIED | Web page | Vote trading; custodial token voting; exchange-operated BPs | Primary-source status (exchange research) |
| S22 | What Is Delegated Proof of Stake (DPoS)? | Binance Academy | 2018 | binance.com | binance.com/en/academy/articles/delegated-proof-of-stake-explained | PARTIALLY VERIFIED | Web page | DPoS mechanics (delegates, voting, rewards) | Independent verification; academic rigour |
| S23 | EOS voting structure encourages centralization | CryptoSlate | 2019 | cryptoslate.com | cryptoslate.com/eos-voting-structure-encourages-centralization | PARTIALLY VERIFIED | Web page | Geographic concentration of EOS BPs | Primary-source status |
| S24 | Proof of Space | Chia Network | — | docs.chia.net | docs.chia.net/proof-of-space | VERIFIED | Docs | Proof of Space = storage commitment; plotting/farming | Deployment measurements |
| S25 | How Chia Consensus Works | Chia Network | — | docs.chia.net | docs.chia.net/consensus-basics | VERIFIED | Docs | PoSpace + PoTime (VDF/timelords); Nakamoto longest-chain; farmer win ∝ space share | Deployment measurements |
| S26 | Farming Overview | Chia Network | — | docs.chia.net | docs.chia.net/farming-overview | VERIFIED | Docs | Plotting cost; plot filter; reward structure (farmer 1/8, pool 7/8) | Deployment measurements |
| S27 | Farming Requirements | Chia Network | 2026-03-03 | docs.chia.net | docs.chia.net/.../new-proof-farming-requirements/ | VERIFIED | Docs | Minimum plot k32 ≈ 101.4 GiB | — |
| S28 | Introduction | Chia Network | — | docs.chia.net | docs.chia.net/chia-blockchain/introduction | VERIFIED | Docs | Decentralisation claim (>100k nodes); timelord role | Claim not independently verified |
| S29 | Consensus Introduction | Chia Network | — | docs.chia.net | docs.chia.net/consensus-intro/ | VERIFIED | Docs | "500 GiB = 5 votes, 100 GiB = 1 vote"; one unit of space = one unit of winning probability | — |
| S30 | 51% Attacks | MIT Digital Currency Initiative | 2020-07-10 | dci.mit.edu | dci.mit.edu/projects/51-percent-attacks | VERIFIED | Web page | Hashrate rental markets lower attack cost for small coins; 40+ reorgs 2019–2020 | — |
| S31 | Fact and Fiction: Challenging the Honest Majority Assumption | Han, Sui, Yu, Liu, Chen | 2021-02-28 | IACR ePrint 2019/752 | eprint.iacr.org/2019/752.pdf | VERIFIED | Abstract + paper | Rational attack condition (payoff > cost + lost future revenue) | — |
| S32 | Majority is not Needed: A Counterstrategy to Selfish Mining | — | — | arxiv 2304.06313 | arxiv.org/html/2304.06313v2 | VERIFIED | Abstract + paper | <50% attacks via piggyback/selfish mining | — |
| S33 | Ouroboros Praos | David, Gaži, Kiayias, Russell | 2023-04-27 (rev) | IACR ePrint 2017/573 | eprint.iacr.org/2017/573.pdf | VERIFIED | Duplicate of S12 | (see S12) | (see S12) |
| S34 | STAKESURE: PoS with Strong Cryptoeconomic Safety | — | — | arxiv 2401.05797 | arxiv.org/html/2401.05797 | VERIFIED | Abstract + paper | Cost-of-corruption ≥1/3 stake | — |
| S35 | The cryptoeconomics of slashing | a16z Frontier | 2023-01-25 | a16zfrontier.com | a16zfrontier.com/posts/article/the-cryptoeconomics-of-slashing | VERIFIED | Web page | Slashing economics; custodial/LST centralisation | Primary-source status |
| S36 | Stake-Bleeding Attacks on PoS Blockchains | Gaži, Kiayias, Russell | — | NSF PAR | par.nsf.gov/servlets/purl/10099216 | VERIFIED | Abstract + paper | Stake bleeding; posterior corruption | — |
| S37 | Bribers, Bribers on The Chain | — | — | arxiv 2509.17185 | arxiv.org/html/2509.17185v2 | VERIFIED | Abstract + paper | Bribery contracts (PayToAttest/PayToExit/PayToBias) | — |
| S38 | Proof-of-Stake Attacks glossary | Cardano | — | cardano.org | cardano.org/glossary/proof-of-stake-attacks | VERIFIED | Web page | Long-range, nothing-at-stake, grinding, stake-bleeding, posterior corruption, bribery, adaptive corruption, stake majority | Primary-source status |
| S39 | Coin Concentration of Proof-of-Stake Blockchains | Irresberger, Yang | 2023 | Economics Letters 229:111219 | doi:10.1016/j.econlet.2023.111219 | PARTIALLY VERIFIED | Metadata only | Observed block-production concentration and validator entry/exit | Does not establish universal law for every PoS system |
| S40 | Weak subjectivity | Ethereum Foundation | — | ethereum.org | ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity | VERIFIED | Docs | Weak subjectivity; checkpoints; long-range defence; unbonding relationship | Protocol parameter values |
| S41 | Why is Stake Unbonding so Slow? | Babylon Labs | — | babylonlabs.io | babylonlabs.io/blog/why-is-stake-unbonding-so-slow | VERIFIED | Web page | Unbonding > weak-subjectivity period; long-range defence via unbonding | — |
| S42 | Long Range Attacks, and a new Fork Choice Rule | NEAR | 2019-09-10 | near.org | near.org/blog/long-range-attacks-and-a-new-fork-choice-rule | VERIFIED | Web page | Long-range attack mechanics; fork-choice defences | — |
| S43 | The History of Casper — Chapter 2 | Buterin | 2016-12-07 | blog.ethereum.org | blog.ethereum.org/2016/12/07/history-casper-chapter-2 | VERIFIED | Blog post | Long-range attack defence; weak subjectivity concept | Primary-source status |
| S44 | Who Watches the Watchmen? (Proof of Personhood review) | — | 2020-11-12 | Frontiers in Blockchain | frontiersin.org/journals/blockchain/articles/10.3389/fbloc.2020.590171/full | VERIFIED | Full text | PoP decentralized identity trilemma; approaches (pseudonym parties, biometric/ZKP, vouching) | — |
| S45 | Identity and Personhood in Digital Democracy | — | 2020-11-05 | arxiv 2011.02412 | arxiv.org/html/2011.02412v1 | VERIFIED | Abstract + paper | Privacy-invasive social graph; Sybil-region detection limits; exclusion risk; re-identification | — |
| S46 | Compressed to 0: The Silent Strings of Proof of Personhood | — | — | assets.pubpub.org | assets.pubpub.org/oof5izij/950a603c-53ea-42df-bb97-f7c024e78a4e.html | VERIFIED | Full text | De jure vs de facto Sybil resistance; puppeteering/oligopoly (23 entities = ~40% accounts/~48% rewards) | — |
| S47 | A Cryptographic Framework for Proof of Personhood | — | 2026-02-20 | IACR ePrint 2026/333 | eprint.iacr.org/2026/333 | VERIFIED | Abstract | ZKP-based PoP independently verifiable | — |
| S48 | Human Challenge Oracle | — | — | arxiv 2601.03923 | arxiv.org/html/2601.03923v1 | VERIFIED | Abstract + paper | Human effort = non-reusable, non-parallelizable resource; linear Sybil cost Ω(s) vs sublinear PoW/PoS | — |
| S49 | RTCT: Improving resistance to Sybil attacks | — | 2026-06-22 | CEUR-WS Vol-4223 | ceur-ws.org/Vol-4223/paper5.pdf | VERIFIED | Abstract + paper | Recurring challenges + token burn per node; exponential attacker cost with difficulty | — |
| S50 | The economic web of trust: adaptive Sybil resistance | Shapiro | 2026-08-03 | iog.io | iog.io/news/the-economic-web-of-trust-adaptive-sybil-resistance | VERIFIED | Web page | Costly influence despite cheap identity; credit influence tied to lending relationships | — |
| S51 | Threat Landscape in Decentralized Systems: Sybil Attacks (2015–2025) | — | 2026-07-01 | MDPI Applied Sciences | mdpi.com/2076-3417/16/14/6929 | VERIFIED | Abstract + paper | Sybil attack survey 2015–2025 | — |
| S52 | Hybrid PoW/PoS Consensus | Zano | — | docs.zano.org | docs.zano.org/docs/learn/how-zano-works/consensus | VERIFIED | Docs | Hybrid PoW/PoS; both resources required for 51% | — |
| S53 | Hybrid PoW/PoS Consensus Explained | Binance Academy | 2019-03-25 (upd 2026-06-09) | binance.com | binance.com/en/academy/articles/hybrid-pow-pos-consensus-explained | PARTIALLY VERIFIED | Web page | Hybrid PoW/PoS mechanics | Independent verification |
| S54 | An Interleaving Hybrid Consensus Protocol | Sun, Rajasekaran | 2019-11-22 | arxiv 1911.09262 | arxiv.org/pdf/1911.09262 | VERIFIED | Abstract + paper | Interleaving PoW/PoS | — |
| S55 | Proof-of-Stake Technical Overview | Conflux | — | doc.confluxnetwork.org | doc.confluxnetwork.org/docs/general/conflux-basics/consensus-mechanisms/proof-of-stake/pos_overview | VERIFIED | Docs | PoW + PoS chain for finality and committee voting | — |
| S56 | Zilliqa PBFT hybrid | — | 2026-02-25 | Springer | springer.com | PARTIALLY VERIFIED | Metadata only | PoW for Sybil/shard election + PBFT within shards | Primary source not inspected |
| S57 | Webs of Trust | Norton LifeLock | 2020-03-12 | nortonlifelock.com | nortonlifelock.com | PARTIALLY VERIFIED | Web page | Web-of-trust concept | Primary-source status |
| S58 | A Survey on Long-Range Attacks for PoS Protocols | — | 2019-02-26 | IEEE 8653269 | ieeexplore.ieee.org/document/8653269 | VERIFIED | Abstract + paper | Long-range attack taxonomy and defences | — |
| S59 | Synergeia: Hybrid PoW/PoS Consensus | Schutza | 2025-11-22 | IACR ePrint 2025/2138 | eprint.iacr.org/2025/2138.pdf | VERIFIED | Abstract + paper | Hybrid PoW/PoS security | — |
| S60 | Anduin: A Sybil-Resistant Agentic Marketplace Reputation System | Ramji et al. | 2026-11-03 | arxiv 2611.05586 | arxiv.org/html/2611.05586v1 | VERIFIED | Abstract + paper | Reputation-based Sybil resistance | — |

**Verification model:** VERIFIED = bibliographic record and technical claims confirmed against primary paper, official specification, or authoritative documentation. PARTIALLY VERIFIED = bibliographic metadata confirmed; technical claims based on abstract, docs, or secondary summary, not full primary inspection. None of the above should be read as implementation endorsement.

**Previously UNRESOLVED sources (AlephBFT, Avalanche-family, Chia) are now resolved:** AlephBFT [arXiv 1810.05256; AFT 2019] — PARTIALLY VERIFIED (leaderless async BFT, DAG/poset, trustless beacon; async liveness not claimed by implementation; prespecified committee). Avalanche-family [arXiv 1906.08936; IPFS 2018] — VERIFIED (Snow family; leaderless metastable subsampling; probabilistic safety; O(kn)–O(kn log n); "Team Rocket" pseudonymous in 2018, named authors in 2019). Chia Proof of Space/Time — VERIFIED (Chia business whitepaper v2.0; docs.chia.net; PoSpace plotting/farming + PoTime VDF/timelords; Nakamoto longest-chain). These are recorded for completeness but not cited as supporting FST design decisions.

## 9. Open questions

1. Which Sybil-resistance mechanism best suits FST's financial use case (work, stake, space, hybrid, or other)? (OPEN RESEARCH — D-016 PROVISIONAL)
2. How does the chosen Sybil mechanism interact with the chosen consensus family (Section 6 compositions)? (OPEN RESEARCH — RS-001/RS-002 composition)
3. What voting-power distribution respects the consensus fault bound under FST's adversary model? (OPEN RESEARCH — D-017)
4. How does FST prevent validator centralisation (stake pools, mining pools, geographic concentration)? (OPEN RESEARCH)
5. How does FST handle long-range attacks, nothing-at-stake, and weak subjectivity if PoS is used? (OPEN RESEARCH)
6. What is the relationship between token value and security budget (circularity problem)? (OPEN RESEARCH — D-015)
7. How does Sybil resistance behave under network partitions and prolonged outages? (OPEN RESEARCH — D-018)
8. What are the entry and exit mechanics (bonding, unbonding, slashing) that preserve safety? (OPEN RESEARCH)
9. Can new/returning nodes independently verify chain selection without weak-subjectivity trust? (OPEN RESEARCH — P9, P10)
10. How does Sybil resistance adapt to multi-transport networks (mesh, radio, satellite, delay-tolerant)? (OPEN RESEARCH — D-019)

All items remain OPEN RESEARCH. No mechanism is approved or selected.

## 10. Cross-references

- [`../scope/scope-v0.1.md`](../scope/scope-v0.1.md) — FST project scope; decentralisation (permissionless validator participation); offline operation; multi-transport communication.
- [`../principles/engineering-principles.md`](../principles/engineering-principles.md) — Approved engineering principles (P1–P14).
- [`../decisions/decision-register.md`](../decisions/decision-register.md) — Decision classifications (D-015, D-016, D-017, D-018, D-019, D-036).
- [`consensus-validator-research.md`](consensus-validator-research.md) — RS-013 consensus algorithms and architecture (companion entry; interdependent but not interchangeable).
- [`transaction-model-research.md`](transaction-model-research.md) — RS-015 transaction model and state representation.
- [`../security/threat-model-v0.1.md`](../security/threat-model-v0.1.md) — T-02 Sybil attacks (not mitigated, OPEN RESEARCH).
- [`../architecture/node-roles.md`](../architecture/node-roles.md) — Validator-capable node role (future; Sybil resistance unresolved).
- [`../research/research-register.md`](research-register.md) — Research register (RS-001 through RS-015).
- [`../glossary.md`](../glossary.md) — Sybil resistance definition.
