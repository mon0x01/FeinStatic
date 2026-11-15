# Candidate C5 Evaluation — PoS + Separate Finality Gadget (Gasper-style)

**Research ID:** RS-013 Section 18.5 (Candidate C5); RS-002 Section 6 (Candidate C5)
**Classification:** OPEN RESEARCH — no consensus algorithm or Sybil-resistance mechanism is approved or selected for FST.
**Evaluated against:** FST principles P1–P14; decisions D-015, D-016, D-017, D-018, D-019; scope-v0.1.md; threat-model-v0.1.md T-02/T-011.
**Evaluation stance:** Independent, read-only research. No implementation, no code changes, no architectural approval.

## Source baseline (verification status per RS-002 §8 / RS-013 §16)

| User S-ref | RS-002 table ref | Title | Authors | Date | Venue | DOI/URL | Verification status | Material inspected |
|---|---|---|---|---|---|---|---|---|
| S14 | S14 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arXiv | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 |
| S13 | S13 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arXiv | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems |
| S40 | S40 | Weak subjectivity | Ethereum Foundation | Current | ethereum.org docs | ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity | VERIFIED | Docs |
| S4 (=RS-002 S41) | S41 | Why is Stake Unbonding so Slow? (long-range defence via unbonding) | Babylon Labs | Current | babylonlabs.io blog | babylonlabs.io/blog/why-is-stake-unbonding-so-slow | VERIFIED | Web page |
| S5 | S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) |
| S2 | S2 | Impossibility of Distributed Consensus with One Faulty Process (FLP) | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text |
| S3 | S3 | Consensus in the Presence of Partial Synchrony (DLS) | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) |
| S35 | S35 | The cryptoeconomics of slashing | a16z Frontier | 2023-01-25 | a16zfrontier.com | a16zfrontier.com/posts/article/the-cryptoeconomics-of-slashing | VERIFIED | Web page |
| S36 | S36 | Stake-Bleeding Attacks on PoS Blockchains | Gaži, Kiayias, Russell | — | NSF PAR | par.nsf.gov/servlets/purl/10099216 | VERIFIED | Abstract + paper |
| S37 | S37 | Bribers, Bribers on The Chain | — | — | arXiv 2509.17185 | arxiv.org/html/2509.17185v2 | VERIFIED | Abstract + paper |
| S38 | S38 | Proof-of-Stake Attacks glossary | Cardano | — | cardano.org glossary | cardano.org/glossary/proof-of-stake-attacks | VERIFIED | Web page |

Note: RS-002 records the Babylon long-range source as table ref S41; this report uses the user's S-ref "S4" per the key-source list, and cross-references RS-002 S41.

---

## 1. Consensus and Sybil integration

### 1.1 Which exact component resists Sybil participation?

**Stake lockup via Casper FFG** is the Sybil-resistance component. Validator eligibility requires a participant to deposit (bond) native tokens as collateral [S14 Gasper2020]. Identity itself is free to create (a public key costs nothing [S5 Douceur2002]), but voting power is proportional to staked tokens, so creating additional identities does not multiply voting power without acquiring more stake [S14 Gasper2020; S5 Douceur2002].

**Status of claim:** VERIFIED for the mechanism description (Gasper abstract + §§1–4 inspected); the Sybil-resistance framing ("stake binds voting power to economic cost") is a composition interpretation recorded in RS-013 §18.5 [S14 Gasper2020] and RS-002 §6.6.5 [S14 Gasper2020]. The claim that stake-based Sybil resistance is economically sound for FST is OPEN RESEARCH (D-015 monetary policy unresolved; D-016 permissionless participation PROVISIONAL but mechanism unresolved).

### 1.2 Which component performs agreement and ordering?

Agreement and ordering are split across two composed mechanisms [S14 Gasper2020]:

- **LMD-GHOST fork choice** — selects the canonical chain among competing tips by weighing attestations with the "latest message" per validator (weighted by stake) [S14 Gasper2020]. This performs *ordering* (which chain is canonical) but does not finalize.
- **Casper FFG finality gadget** — validators vote on pairs (source checkpoint, target checkpoint); a checkpoint is "justified" when ≥2/3 of total stake attests to it, and "finalized" when a justified checkpoint is directly followed by another justified checkpoint [S13 CasperFFG2017; S14 Gasper2020]. This performs *agreement/finality*.

This is a **composition** (fork choice separate from finality), per RS-013 Section 2.10 composition principle [RS-013 §2.10; S14 Gasper2020]. RS-013 §18.5 records this explicitly: "Agreement and ordering: LMD-GHOST fork choice + Casper FFG finality gadget [S14 Gasper2020]" [RS-013 §18.5].

### 1.3 How is voting power or proposer probability derived?

- **Voting power:** proportional to staked (bonded) tokens [S14 Gasper2020].
- **Proposer probability:** proposer assigned per slot from the validator set (stake-weighted selection per slot) [S14 Gasper2020].
- **Finality threshold:** ≥2/3 of total stake supermajority to justify/finalize a checkpoint [S13 CasperFFG2017; S14 Gasper2020].

**Status:** PARTIALLY VERIFIED for exact proposer mechanics (Gasper abstract + §§1–4 inspected; exact per-slot assignment algorithm and parameters not fully extracted — RS-013 §16 ref #15 limitation: "Exact checkpoint finality parameters" not established). The ≥2/3 supermajority and stake-weight derivation are established in Casper FFG theorems (§§1–2 inspected) [S13 CasperFFG2017] and restated in Gasper [S14 Gasper2020].

### 1.4 How are validator identities mapped to scarce resources?

Validator identity = a cryptographic key pair (free to generate) [S5 Douceur2002]. The scarce resource is **native FST token stake**, locked as a bond [S14 Gasper2020]. The mapping: identity → bonded stake → voting power weight ∝ stake [S14 Gasper2020]. Unbonding releases stake after a delay; the delay must exceed the weak-subjectivity period / slashing detection window to preserve accountable safety [S4 BabylonBlog2023 (=RS-002 S41); S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017].

**Status:** VERIFIED for stake→voting-power mapping [S14 Gasper2020; S13 CasperFFG2017]. PARTIALLY VERIFIED for unbonding/weak-subjectivity period relationship (docs-level inspection of S40/S41; exact protocol parameters not established).

### 1.5 Does the Sybil mechanism actually support the consensus membership model?

**Conditionally.** Casper FFG's accountable-safety and liveness proofs assume the validator set's stake distribution respects the ≥2/3 honest-stake assumption [S13 CasperFFG2017; S14 Gasper2020]. The Sybil mechanism (stake lockup) does not, by itself, guarantee that the resulting voting-power distribution respects the fault bound: stake concentration (whale concentration, custodial staking, liquid-staking derivatives) can concentrate voting power and break the ≥2/3 assumption [S35 a16z slashing; S39 IrresbergerYang2023 — PARTIALLY VERIFIED metadata only in RS-002].

RS-013 §18.5 records: "Coherent security argument? Yes — Gasper formally combines fork choice and finality; accountable safety theorem [S13 CasperFFG2017; S14 Gasper2020]" but adds the caveat that finality is "qualified by Byzantine-stake and weak-subjectivity assumptions" [S14 Gasper2020; S40 EthereumWeakSubjectivity2024] [RS-013 §18.5].

**Coherence across components:** The security argument is coherent *internally* (fork choice + finality gadget formally combined; accountable safety ≥1/3 slashable on conflicting finality) [S13 CasperFFG2017; S14 Gasper2020]. It is **not** coherent unconditionally: it depends on (a) honest ≥2/3 stake, (b) slashing detection and enforcement, (c) weak-subjectivity checkpoints for new/returning nodes, (d) unbonding > weak-subjectivity period [S13 CasperFFG2017; S14 Gasper2020; S40 EthereumWeakSubjectivity2024; S4 BabylonBlog2023]. If any assumption fails, the Sybil→consensus mapping breaks [RS-013 §9.2; RS-002 §6.6.5].

---

## 2. Security

### 2.1 Safety conditions

- **Accountable safety:** If two conflicting checkpoints are finalized, at least 1/3 of total stake is slashable (equivocated) [S13 CasperFFG2017; S14 Gasper2020]. This is the core safety theorem of Casper FFG.
- **Deterministic checkpoint finality:** a finalized checkpoint cannot be reverted under the protocol's Byzantine-stake and slashing assumptions [S14 Gasper2020]. Finality is qualified, not unconditional irreversibility [S13 CasperFFG2017; S40 EthereumWeakSubjectivity2024].
- Safety under LMD-GHOST fork choice is preserved while the fork-choice rule follows the weighted attestation tree [S14 Gasper2020].

**Assumptions:** ≥2/3 honest stake; slashing conditions enforced; quorum intersection over stake-weighted votes [S13 CasperFFG2017; S14 Gasper2020].

**Verification:** VERIFIED for accountable-safety theorem statement (Casper FFG §§1–2 + theorems inspected) [S13 CasperFFG2017]; VERIFIED for Gasper combination (abstract + §§1–4) [S14 Gasper2020]. Exact safety-parameter values (checkpoint epoch length, slashing detection window) PARTIALLY VERIFIED / not established from inspected material.

### 2.2 Liveness conditions

- **Plausible liveness (≥2/3 honest):** if ≥2/3 of stake is honest and the network is partially synchronous, new checkpoints are finalized [S13 CasperFFG2017; S14 Gasper2020].
- **Probabilistic liveness:** under weaker conditions, finality may proceed probabilistically [S14 Gasper2020].
- **Inactivity leak:** if finality stalls (e.g., <2/3 online), the inactivity leak gradually reduces the weight of offline validators until a 2/3 supermajority of the remaining stake can finalize — this recovers liveness after prolonged offline periods [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].

**Liveness requires partial synchrony** (GST) — under full asynchrony, deterministic liveness is impossible by FLP [S2 FLP1985; S3 DLS1988] [RS-013 §3.1, §3.5].

**Verification:** VERIFIED for plausible-liveness ≥2/3 statement (Casper FFG theorems) [S13 CasperFFG2017]; VERIFIED for Gasper liveness classification (safety/plausible/probabilistic liveness) [S14 Gasper2020]. Inactivity-leak mechanics are documented at the Ethereum-implementation/docs level [S40 EthereumWeakSubjectivity2024], not re-derived from Gasper paper in inspected material — PARTIALLY VERIFIED for parameterization.

### 2.3 Byzantine assumptions

- ≥2/3 honest stake for finality [S13 CasperFFG2017; S14 Gasper2020].
- Accountable safety tolerates <1/3 Byzantine stake (the 1/3 slashable on conflicting finality) [S13 CasperFFG2017].
- Byzantine validators may equivocate, withhold votes, collude [RS-013 §3.9; S14 Gasper2020].
- Adaptive corruption: not explicitly covered by Casper FFG/Gasper core security model (unlike Praos's adaptive security [S12 OuroborosPraos2017]) — this is a research gap [RS-013 §18.4 vs §18.5 contrast].

**Verification:** VERIFIED for ≥2/3 honest-stake and <1/3 Byzantine thresholds [S13 CasperFFG2017; S14 Gasper2020]. Adaptive-corruption coverage in Gasper is UNRESOLVED from inspected material (Gasper §§1–4 do not establish adaptive security; contrast S12 Praos which is VERIFIED for adaptive security).

### 2.4 Quorum intersection

Safety relies on quorum intersection: any two 2/3 supermajorities over the same stake-weighted validator set share >1/3 of stake, so conflicting finality votes require ≥1/3 stake to equivocate (slashable) [S13 CasperFFG2017; S14 Gasper2020]. This is the stake-weighted analogue of the 3f+1 bound; the exact stake-weighted quorum construction follows Casper FFG's two slashing conditions (S1: no double vote for same target; S2: no surround vote) [S13 CasperFFG2017].

**Verification:** VERIFIED for two slashing conditions and ≥1/3 slashable accountability [S13 CasperFFG2017 §§1–2 + theorems]. Stake-weighted quorum-intersection arithmetic is a composition interpretation recorded in RS-013 §18.5 [S13 CasperFFG2017; S14 Gasper2020].

### 2.5 Adaptive corruption

**Not established** for C5 from inspected sources. Casper FFG/Gasper security model (as inspected: abstract + §§1–2 theorems, Gasper §§1–4) does not explicitly claim adaptive security [S13 CasperFFG2017; S14 Gasper2020]. Contrast: Ouroboros Praos provides fully adaptive security under honest-majority-of-stake [S12 OuroborosPraos2017 — VERIFIED]. RS-013 §18.5 lists no adaptive-corruption assumption for C5 (the "Adversary assumptions" line covers ≥2/3 honest stake + partial synchrony only) [RS-013 §18.5]. Mark UNRESOLVED for C5-specific adaptive-corruption guarantees.

### 2.6 Equivocation (nothing-at-stake / double-signing)

Casper FFG's two slashing conditions penalize equivocation: (S1) a validator cannot cast two votes for the same target epoch; (S2) a validator cannot cast a surround vote [S13 CasperFFG2017]. Equivocating on conflicting finalized checkpoints makes ≥1/3 of stake slashable [S13 CasperFFG2017]. Nothing-at-stake (voting on every fork at zero direct cost) is mitigated by slashing + economic finality [S13 CasperFFG2017; S14 Gasper2020; RS-013 §6.12].

**Verification:** VERIFIED for two slashing conditions and accountable safety [S13 CasperFFG2017]. Nothing-at-stake mitigation framing is VERIFIED at the RS-013 composition level [RS-013 §6.12 citing S13/S14].

### 2.7 Collusion and bribery

- Collusion among ≥1/3 Byzantine stake can attempt safety violations via equivocation (slashable) [S13 CasperFFG2017].
- Collusion among <2/3 stake can withhold votes to halt finality (liveness attack) [S13 CasperFFG2017].
- **Bribery:** Bribers2025 [S37] models bribery contracts (PayToAttest, PayToExit, PayToBias) that pay validators to misbehave; finds token toxicity alone is insufficient because bribe benefit is privatized while cost is socialized [S37 Bribers2025 — VERIFIED abstract + paper]. Slashing raises cost-of-corruption to ≥1/3 of total stake [S34 STAKESURE2024 — VERIFIED; RS-002 §3.2].
- Custodial/LST centralisation concentrates voting power in derivative managers, increasing collusion surface [S35 a16z slashing — VERIFIED].

**Verification:** VERIFIED for bribery-contract model [S37 Bribers2025]; VERIFIED for cost-of-corruption ≥1/3 stake [S34 STAKESURE2024]; VERIFIED for LST centralisation observation [S35 a16z slashing]. Application of bribery model to C5 specifically is RS-002 composition-level interpretation — PARTIALLY VERIFIED as applied to Gasper.

### 2.8 Censorship

A ≥2/3 honest-stake majority can censor transactions by excluding them from proposals/attestations; censorship resistance depends on proposer diversity and inclusion mechanisms (not part of Casper FFG/Gasper core) [S14 Gasper2020; RS-013 §7.11]. Censorship is classified as an economic/governance risk, not a protocol-measured property [RS-013 §6.15].

**Verification:** Censorship-as-risk is RS-013 qualitative assessment (no specific source citation for Gasper) [RS-013 §7.11] — UNRESOLVED as a C5-specific measured property.

### 2.9 Long-range attacks

**Relevant and a key C5 weakness.** In PoS, an adversary who previously controlled stake can rewrite history from an old checkpoint using withdrawn/sold keys ("posterior corruption") [S13 CasperFFG2017; S36 GažiEtAl2019; S38 CardanoPoSAttacks2024]. Defences:
- Weak-subjectivity checkpoints: new/returning nodes must trust a recent checkpoint from an external source to select the canonical chain [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017].
- Unbonding period must exceed the weak-subjectivity period so exited stake cannot be used for long-range rewriting before slashing detection [S4 BabylonBlog2023 (=RS-002 S41); S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017].
- Stake bleeding: a minority long-range attack drains fees onto a private fork [S36 GažiEtAl2019 — VERIFIED].

**Weak subjectivity is a trust assumption:** independent verification (P9) is compromised for new/returning nodes, who cannot determine the canonical chain from genesis alone [S40 EthereumWeakSubjectivity2024; RS-013 §18.5; RS-002 §9 Q9].

**Verification:** VERIFIED for long-range attack mechanics and weak-subjectivity defence [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017; S36 GažiEtAl2019; S4 BabylonBlog2023]. The claim that C5 requires weak-subjectivity trust is VERIFIED at the Ethereum-docs/Gasper level; application to FST's P9 independent-verification principle is RS-013 interpretation — PARTIALLY VERIFIED as applied to FST.

### 2.10 Resource-exhaustion attacks

Casper FFG/Gasper itself does not define DoS/resource-exhaustion resistance; that is a P2P/network-layer concern [RS-013 §9.12; threat-model T-05]. Validator vote messages are small (checkpoint pairs), but all-to-all or leader-targeted communication can be exhausted under flood [RS-013 §8.9]. Leader (proposer) DDoS is a known attack surface for leader-based PoS [S16 DoS Resilience AsiaCCS2024 — VERIFIED: leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size]. Applied to C5's per-slot proposer, this is a plausible but not Gasper-specific measured result — PARTIALLY VERIFIED as applied to C5.

### 2.11 Recovery after network disruption

- After a partition heals, liveness resumes once a ≥2/3 honest-stake quorum reconnects (partial synchrony restored) [S3 DLS1988; S14 Gasper2020].
- Safety is preserved during the partition under the fault bound [S13 CasperFFG2017; S14 Gasper2020].
- Nodes offline longer than the weak-subjectivity period cannot independently rejoin; they require an external checkpoint (weak-subjectivity trust) [S40 EthereumWeakSubjectivity2024].
- Inactivity leak recovers liveness if offline validators' weight drops below 1/3 [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].

**Verification:** VERIFIED for safety-preserved/liveness-suspended during partition [S13 CasperFFG2017; S14 Gasper2020; S3 DLS1988]. Weak-subjectivity rejoin constraint VERIFIED at docs level [S40 EthereumWeakSubjectivity2024]. Inactivity-leak recovery mechanics PARTIALLY VERIFIED (docs-level, not re-derived from Gasper paper).

---

## 3. Operational architecture

### 3.1 Validator entry and exit

- **Entry:** deposit stake (bond) to become eligible; any participant who deposits can become a validator [S14 Gasper2020].
- **Exit:** unbonding period delays stake release; must exceed weak-subjectivity period / slashing detection window [S4 BabylonBlog2023; S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017]. Exit does not slash unless misbehaviour is detected [RS-013 §6.9].
- Validator set changes per epoch [S14 Gasper2020].

**Verification:** VERIFIED for entry/deposit and per-epoch set changes [S14 Gasper2020]. PARTIALLY VERIFIED for exact unbonding/withdrawal mechanics (docs-level S40/S41; not re-derived from Gasper paper).

### 3.2 Registration and eligibility

Eligibility = bonded stake above threshold (threshold value is a protocol parameter not established in inspected material) [S14 Gasper2020]. Registration is on-chain via deposit contract (Ethereum implementation pattern; Gasper describes the mechanism abstractly) [S14 Gasper2020; RS-013 §5.5]. No central approval [RS-013 §5.5].

**Verification:** VERIFIED for "any participant who deposits stake can become a validator" [S14 Gasper2020 §1–4]. Exact threshold/registration contract not established — PARTIALLY VERIFIED.

### 3.3 Proposer or leader selection

Proposer assigned per slot from the validator set (stake-weighted) [S14 Gasper2020]. The proposer orders transactions in its slot [RS-013 §5.5].

**Verification:** VERIFIED for "proposer assigned per slot" [S14 Gasper2020]. Exact selection algorithm (randomization source, slot assignment) PARTIALLY VERIFIED (Gasper §§1–4 inspected; details not extracted — RS-013 ref #15 limitation).

### 3.4 Committee formation

Committee-based selection from staked validators [S14 Gasper2020] (RS-013 §5.5). Committees attest to checkpoints; the full validator set is the source of voting power [S13 CasperFFG2017; S14 Gasper2020].

**Verification:** VERIFIED for committee-based selection [S14 Gasper2020]. Committee size, rotation, and sampling mechanism PARTIALLY VERIFIED / not established from inspected material.

### 3.5 Validator rotation

Validator set changes per epoch [S14 Gasper2020]. Rotation frequency = epoch length (parameter not established) [RS-013 §18.5 research gaps]. Rotation limits the window in which a corrupted validator set can cause harm [RS-013 §6.10].

**Verification:** VERIFIED for per-epoch set changes [S14 Gasper2020]. Rotation frequency/parameters UNRESOLVED from inspected material.

### 3.6 State synchronization

New/rejoining validators synchronize state; snapshots and state proofs reduce sync time [RS-013 §8.6; FeinStatic snapshot patterns D-032]. For C5 specifically, weak subjectivity means a returning node needs a trusted checkpoint before it can safely sync [S40 EthereumWeakSubjectivity2024].

**Verification:** VERIFIED for weak-subjectivity sync constraint [S40 EthereumWeakSubjectivity2024]. C5-specific state-sync mechanics are RS-013 composition-level (FeinStatic reference) — PARTIALLY VERIFIED as applied to C5.

### 3.7 Recovery and checkpoint assumptions

Checkpoints are finalized by Casper FFG; weak-subjectivity period bounds how far back a node can sync without external trust [S40 EthereumWeakSubjectivity2024]. Unbonding > weak-subjectivity period required to prevent long-range attacks after exit [S4 BabylonBlog2023; S40 EthereumWeakSubjectivity2024].

**Verification:** VERIFIED for checkpoint + weak-subjectivity relationship [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017; S4 BabylonBlog2023].

### 3.8 Slashing or other punishment assumptions

Slashing penalizes double-signing/equivocation (burns stake) [S13 CasperFFG2017; S14 Gasper2020]. Requires evidence collection and slashing detection window [S13 CasperFFG2017; RS-013 §6.9]. Slashing introduces centralisation risk if a small group decides what counts as misbehaviour [RS-013 §6.9; S35 a16z slashing].

**Verification:** VERIFIED for two slashing conditions and accountable safety [S13 CasperFFG2017]. Slashing-centralisation risk is RS-013 qualitative assessment citing S35 — PARTIALLY VERIFIED as applied to C5.

### 3.9 Independent verification (incl. weak subjectivity for PoS)

**Weak subjectivity is a direct conflict with P9 (independent verification).** New/returning nodes cannot independently determine the canonical chain from genesis; they require an external recent checkpoint [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017]. This is a known PoS limitation, not unique to Gasper, but C5's deterministic checkpoint finality explicitly relies on it [S40 EthereumWeakSubjectivity2024; RS-013 §18.5].

**Verification:** VERIFIED for weak-subjectivity mechanics [S40 EthereumWeakSubjectivity2024]. The P9 conflict is RS-013 interpretation [RS-013 §18.5; scope P9].

---

## 4. Performance and network behaviour

### 4.1 Communication complexity

RS-013 does not assign a single complexity to Gasper: "Protocol-specific [Gasper]" [RS-013 §8.1 table row]. LMD-GHOST attestations are stake-weighted messages broadcast per slot; Casper FFG checkpoint votes require supermajority collection. The exact asymptotic cost (O(n²) all-to-all vs optimized aggregation) is not established from inspected material — PARTIALLY VERIFIED / UNRESOLVED for C5-specific numbers. RS-013 explicitly omits quantitative scores [RS-013 §8.1 qualification].

### 4.2 Bandwidth

Validators exchange attestations/votes on every slot/checkpoint. Bandwidth scales with validator count and message frequency; no fixed per-transaction bandwidth figure assigned without source-specific derivation [RS-013 §8.2]. UNRESOLVED for C5-specific bandwidth.

### 4.3 Latency sensitivity

Casper FFG finality requires multiple epochs (source → justified → finalized: two consecutive justified checkpoints) [S13 CasperFFG2017; S14 Gasper2020]. Latency scales with round/epoch count and network delay [RS-013 §8.3]. Exact round counts/latency ranges omitted (cannot be supported without source-specific validation) [RS-013 §8.3].

### 4.4 Message-size and propagation requirements

Attestation messages are small (checkpoint pairs + signatures). Propagation via gossip-based P2P layer [S14 Gasper2020; RS-013 §5.5]. Message-size limits and propagation scope (gossip vs direct to proposers) unresolved for FST [RS-013 §2.5; RS-015 §5.13].

### 4.5 Committee-size sensitivity

Larger committees increase communication overhead but improve decentralisation; exact sensitivity not quantified for C5 [RS-013 §8.1 qualification]. UNRESOLVED for C5-specific committee-size sensitivity.

### 4.6 Operation under packet loss

Packet loss increases timeouts and retransmissions, degrading liveness; degree is protocol- and implementation-specific [RS-013 §8.9]. UNRESOLVED for C5-specific packet-loss behaviour.

### 4.7 Operation under prolonged partitions

During a partition lacking ≥2/3 honest-stake quorum, finality halts; safety preserved; liveness suspended until reconnection [S3 DLS1988; S14 Gasper2020; RS-013 §8.5]. Nodes offline beyond the weak-subjectivity period require external trust to rejoin [S40 EthereumWeakSubjectivity2024]. This is a C5-specific weakness for prolonged partitions (offline operation D-018).

### 4.8 Recovery after connectivity returns

View changes / leader rotation / state sync enable recovery once a ≥2/3 quorum reconnects [S3 DLS1988; S14 Gasper2020; RS-013 §8.7]. Recovery time depends on validator count and state size [RS-013 §8.7]. Inactivity leak may be needed if too many validators remained offline [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].

---

## 5. Financial and economic implications

### 5.1 Security-budget dependencies

Economic security ≈ cost to attack ≈ value of stake at stake; requires token value high enough to make attacks uneconomic; economically circular for a new network (security budget depends on token value, token value may depend on perceived security) [S13 CasperFFG2017; S14 Gasper2020; RS-013 §7.5]. D-015 monetary policy OPEN RESEARCH; no security budget chosen.

### 5.2 Stake concentration

Stake concentration → voting-power concentration → breaks ≥2/3 honest assumption [S35 a16z slashing; S39 IrresbergerYang2023 — PARTIALLY VERIFIED metadata only]. Delegation/pools amplify concentration [RS-013 §6.6; S35 a16z slashing].

### 5.3 Reward and fee dependencies

Rewards = inflation + fees; fund security but dilute holders [RS-013 §7.2, §7.3]. Fees pay for block space; fee markets may centralise block production via MEV (Ethereum-observed, S21 DaianEtAl2020 — PARTIALLY VERIFIED metadata only). Ethereum-specific; not generalized [RS-013 §7.12].

### 5.4 Participation costs

Token acquisition (market price × minimum stake), validator hardware, uptime, slashing risk, unbonding illiquidity, opportunity cost of locked capital [RS-013 §6.4; S35 a16z slashing]. Ethereum 32 ETH minimum cited as example; FST minimum stake UNRESOLVED (D-015).

### 5.5 Entry barriers

Economic barrier = token acquisition cost + hardware + bonding [RS-013 §6.4]. No central approval, but capital requirement is a de facto barrier [S5 Douceur2002; RS-013 §6.4].

### 5.6 Wealth concentration

Rich-get-richer via rewards compounding stake [RS-013 §6.14; S39 IrresbergerYang2023 — PARTIALLY VERIFIED; study-specific, not universal law]. UNRESOLVED as a universal tendency.

### 5.7 Extractable value and transaction ordering

MEV arises from controlling transaction ordering [S21 DaianEtAl2020 — PARTIALLY VERIFIED metadata only; Ethereum-specific]. Proposer controls slot ordering → MEV/front-running risk [RS-013 §7.12]. Not generalized to all systems [RS-013 §7.12].

---

## Source / Evidence Appendix — Candidate C5

| Ref | Title | Authors | Date | Venue | DOI/URL | Verification status | Material inspected | Claims supported | Limitations |
|---|---|---|---|---|---|---|---|---|---|
| S14 | Combining GHOST and Casper (Gasper) | Buterin, Hernandez, Kamphefner, Pham, Qiao, Ryan, Sin, Wang, Zhang | 2020 | arXiv | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 | LMD-GHOST + Casper FFG combination; checkpoint finality; slashing S1/S2; safety/plausible/probabilistic liveness; stake-weighted voting; per-slot proposer | Exact checkpoint finality parameters; adaptive security; exact message complexity |
| S13 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arXiv | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems | Checkpoint finality; 2/3 supermajority; two slashing conditions; accountable safety (≥1/3 slash); plausible liveness (≥2/3) | LMD-GHOST and weak subjectivity (not in FFG paper); exact parameters |
| S40 | Weak subjectivity | Ethereum Foundation | Current | ethereum.org docs | ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity | VERIFIED | Docs | Weak subjectivity; checkpoints; long-range defence; unbonding relationship | Protocol parameter values; not a peer-reviewed paper |
| S4 (=RS-002 S41) | Why is Stake Unbonding so Slow? | Babylon Labs | Current | babylonlabs.io blog | babylonlabs.io/blog/why-is-stake-unbonding-so-slow | VERIFIED | Web page | Unbonding > weak-subjectivity period; long-range defence via unbonding | Primary-source status (blog); parameter values not given |
| S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil = one entity, many identities; identity cost; impossible without central authority except under unrealistic resource parity/coordination | No specific Sybil-resistance solution endorsed |
| S2 | Impossibility of Distributed Consensus with One Faulty Process (FLP) | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| S3 | Consensus in the Presence of Partial Synchrony (DLS) | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST; Byzantine N≥3t+1 | Protocol-specific liveness guarantees |
| S35 | The cryptoeconomics of slashing | a16z Frontier | 2023-01-25 | a16zfrontier.com | a16zfrontier.com/posts/article/the-cryptoeconomics-of-slashing | VERIFIED | Web page | Slashing economics; custodial/LST centralisation | Primary-source status (industry blog) |
| S36 | Stake-Bleeding Attacks on PoS Blockchains | Gaži, Kiayias, Russell | — | NSF PAR | par.nsf.gov/servlets/purl/10099216 | VERIFIED | Abstract + paper | Stake bleeding; posterior corruption | — |
| S37 | Bribers, Bribers on The Chain | — | — | arXiv 2509.17185 | arxiv.org/html/2509.17185v2 | VERIFIED | Abstract + paper | Bribery contracts (PayToAttest/PayToExit/PayToBias); token toxicity insufficient | — |
| S38 | Proof-of-Stake Attacks glossary | Cardano | — | cardano.org glossary | cardano.org/glossary/proof-of-stake-attacks | VERIFIED | Web page | Long-range, nothing-at-stake, grinding, stake-bleeding, posterior corruption, bribery, adaptive corruption, stake majority | Primary-source status (ecosystem glossary) |

**Overall assessment status for C5:** OPEN RESEARCH. Security argument coherent internally but qualified by honest-≥2/3-stake, slashing enforcement, weak-subjectivity trust, and unbonding > weak-subjectivity period. Weak subjectivity conflicts with P9 independent verification. Stake concentration and LST centralisation challenge P1/P4. Economic circularity (D-015 unresolved). No design approved.
