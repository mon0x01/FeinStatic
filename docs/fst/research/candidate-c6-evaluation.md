# Candidate C6 Evaluation — DAG-based Dissemination + Separate Agreement/Finality (Narwhal/Tusk-style)

**Research ID:** RS-013 Section 18.6 (Candidate C6); RS-002 Section 6 (Candidate C6)
**Classification:** OPEN RESEARCH — no consensus algorithm or Sybil-resistance mechanism is approved or selected for FST.
**Evaluated against:** FST principles P1–P14; decisions D-015, D-016, D-017, D-018, D-019; scope-v0.1.md; threat-model-v0.1.md T-02/T-011.
**Evaluation stance:** Independent, read-only research. No implementation, no code changes, no architectural approval.

## Source baseline (verification status per RS-002 §8 / RS-013 §16)

| User S-ref | RS-002 table ref | Title | Authors | Date | Venue | DOI/URL | Verification status | Material inspected |
|---|---|---|---|---|---|---|---|---|
| S10 | S10 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys, pp. 34–50 | doi:10.1145/3492321.3519594; arXiv 2105.11827 | VERIFIED | Abstract + full text (§§1, 2, 3.1, 5) |
| S2 | S2 | Impossibility of Distributed Consensus with One Faulty Process (FLP) | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text |
| S3 | S3 | Consensus in the Presence of Partial Synchrony (DLS) | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) |
| S5 | S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) |
| S1 | S1 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Full text (Introduction, Service Properties) via HTML mirrors |
| S8 | S8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | ACM PODC | doi:10.1145/3293611.3331591; arXiv 1803.05069 | VERIFIED | Abstract + full text (QC, complexity) |
| S15 | S15 | Eclipse Attacks on Bitcoin's Peer-to-Peer Network | Heilman, Kendler, Zohar, Goldberg | 2015 | USENIX Security | eprint.iacr.org/2015/263 | VERIFIED | Abstract + paper |
| S16 | S16 | An Empirical Study of Consensus Protocols' DoS Resilience | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper |
| S6 | S6 | Brewer's Conjecture and the Feasibility of Consistent, Available, Partition-Tolerant Web Services (CAP) | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text |
| S17 | S17 | Delay-Tolerant Networking Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text |
| S18 | S18 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc/rfc9171 | rfc-editor.org/rfc/rfc9171 | VERIFIED | Full text |

Note: RS-002 §8 assigns table refs S1–S60. C6's core source is S10 NarwhalTusk2022. Supporting theory sources S2/S3/S5/S6/S1/S8/S15/S16/S17/S18 are cross-referenced from RS-002 §8 and RS-013 §16.

---

## 1. Consensus and Sybil integration

### 1.1 Which exact component resists Sybil participation?

**None intrinsic to Narwhal/Tusk.** Narwhal and Tusk provide no Sybil resistance [S10 NarwhalTusk2022; RS-002 §6.6.6; RS-013 §18.6]. An external stake/work/space Sybil layer is required [S10 NarwhalTusk2022; RS-013 §18.6]. Identity (node key) is free to create [S5 Douceur2002].

**Status:** VERIFIED that Narwhal/Tusk have no intrinsic Sybil resistance [S10 NarwhalTusk2022 abstract + full text §§1,2,3.1,5; RS-013 §18.6]. The choice of external Sybil layer for FST is OPEN RESEARCH (D-016 PROVISIONAL, mechanism unresolved).

### 1.2 Which component performs agreement and ordering?

Split across two composed layers [S10 NarwhalTusk2022]:

- **Narwhal (dissemination/mempool):** a transaction DAG where each transaction references prior transactions, creating a causal history. Narwhal provides availability and dissemination, **not consensus itself** [S10 NarwhalTusk2022; RS-013 §5.6]. Narwhal is designed to tolerate asynchrony in dissemination (mempool continues under asynchrony) [S10 NarwhalTusk2022].
- **Agreement/ordering layer:** either (a) **Tusk** — asynchronous BFT consensus using the Narwhal DAG as its data structure, randomized consensus circumventing FLP via probabilistic termination; or (b) **Narwhal-HotStuff** — Narwhal + partially synchronous HotStuff, inheriting HotStuff's partial-synchrony liveness [S10 NarwhalTusk2022; RS-013 §5.6].

**Status:** VERIFIED for the three-component decomposition (Narwhal DAG / Narwhal-HotStuff / Tusk) [S10 NarwhalTusk2022]. VERIFIED that Narwhal provides dissemination not consensus, and Tusk provides async BFT [S10 NarwhalTusk2022].

### 1.3 How is voting power or proposer probability derived?

**Depends on the composed consensus layer + external Sybil layer** [S10 NarwhalTusk2022; RS-013 §18.6]. Narwhal/Tusk themselves do not define voting power:
- With Narwhal-HotStuff: voting power derives from the external Sybil layer (e.g., stake-weighted) and HotStuff's quorum certificates (2f+1 of 3f+1) [S8 HotStuff2019; RS-013 §18.6].
- With Tusk: voting power derives from the external Sybil layer applied to the BFT committee; Tusk's randomized agreement does not by itself define proposer probability [S10 NarwhalTusk2022].

**Status:** UNRESOLVED for C6-specific voting-power derivation (depends on uncomposed Sybil layer and uncomposed consensus choice) [RS-013 §18.6].

### 1.4 How are validator identities mapped to scarce resources?

Not defined by Narwhal/Tusk [S10 NarwhalTusk2022]. Mapping requires composing an external Sybil mechanism (stake → voting power, or work → voting power, etc.) [S5 Douceur2002; RS-013 §18.6; RS-002 §5 boundary analysis].

**Status:** UNRESOLVED for C6-specific identity→resource mapping [RS-013 §18.6].

### 1.5 Does the Sybil mechanism actually support the consensus membership model?

Cannot be assessed for C6 alone — no Sybil mechanism is part of C6 [S10 NarwhalTusk2022; RS-013 §18.6]. The coherence of C6 + an external Sybil layer depends on the chosen composition: the Sybil layer's output (voting-power distribution) must respect the consensus fault bound (e.g., <1/3 Byzantine for classical BFT, honest-majority for Tusk-style async) [RS-013 §18.8 composition principle; S5 Douceur2002].

**Coherence argument:** Partially coherent — the dissemination/consensus separation is well-defined empirically [S10 NarwhalTusk2022], but the combined system's formal security argument depends on the chosen consensus layer and Sybil layer; **no single unified proof** [RS-013 §18.6; S10 NarwhalTusk2022].

**Verification:** VERIFIED that no unified security proof exists for the composition [RS-013 §18.6 citing S10]. The claim "no unified proof" is an RS-013 composition-level assessment — PARTIALLY VERIFIED as applied to C6.

---

## 2. Security

### 2.1 Safety conditions

Depends on the composed consensus layer [S10 NarwhalTusk2022]:
- **Tusk:** asynchronous BFT safety under its fault bound (<1/3 Byzantine committee) using the Narwhal DAG as the underlying data structure [S10 NarwhalTusk2022].
- **Narwhal-HotStuff:** HotStuff deterministic safety under partial synchrony and <1/3 Byzantine voting power [S8 HotStuff2019; S1 CastroLiskov1999 quorum intersection].

Narwhal's DAG itself provides **availability** (transactions are eventually included if the DAG advances) but not safety/finality [S10 NarwhalTusk2022].

**Verification:** VERIFIED for Tusk async BFT safety and Narwhal availability-not-consensus [S10 NarwhalTusk2022]. VERIFIED for HotStuff safety under partial synchrony <1/3 [S8 HotStuff2019]. C6-specific safety depends on uncomposed layer — PARTIALLY VERIFIED as applied to C6.

### 2.2 Liveness conditions

- **Tusk:** randomized probabilistic termination under asynchrony — uses randomization to circumvent FLP; does not require partial synchrony for liveness in the same way PBFT/HotStuff do [S10 NarwhalTusk2022; S2 FLP1985]. Termination is probabilistic, not deterministic [S10 NarwhalTusk2022; RS-013 §3.1].
- **Narwhal-HotStuff:** inherits HotStuff's partial-synchrony liveness (liveness after GST) [S10 NarwhalTusk2022; S8 HotStuff2019; S3 DLS1988].
- **Narwhal dissemination:** tolerates asynchrony in the mempool layer [S10 NarwhalTusk2022].

**Verification:** VERIFIED for Tusk randomized async termination and Narwhal-HotStuff partial-synchrony inheritance [S10 NarwhalTusk2022]. VERIFIED for FLP boundary [S2 FLP1985; S3 DLS1988].

### 2.3 Byzantine assumptions

Depends on composed layer [S10 NarwhalTusk2022]:
- Tusk: <1/3 Byzantine committee (async BFT bound) [S10 NarwhalTusk2022].
- Narwhal-HotStuff: <1/3 Byzantine voting power, partial synchrony [S8 HotStuff2019; S1 CastroLiskov1999].

**Verification:** VERIFIED for fault bounds of composed layers [S10 NarwhalTusk2022; S8 HotStuff2019]. C6-specific Byzantine threshold depends on uncomposed Sybil+consensus — PARTIALLY VERIFIED as applied to C6.

### 2.4 Quorum intersection

Depends on composed consensus layer [S10 NarwhalTusk2022]:
- HotStuff/Tusk: quorum intersection over the committee's voting power; any two quorums share ≥1 honest participant under the fault bound [S1 CastroLiskov1999; S8 HotStuff2019].
- Narwhal DAG: does not itself form quorums; it provides causal history/availability [S10 NarwhalTusk2022].

**Verification:** VERIFIED for quorum intersection in classical BFT/HotStuff [S1 CastroLiskov1999; S8 HotStuff2019]. C6-specific quorum construction UNRESOLVED (depends on composed layer) [RS-013 §18.6].

### 2.5 Adaptive corruption

**Not established** for C6 from inspected sources. Narwhal/Tusk paper (§§1, 2, 3.1, 5 inspected) does not claim adaptive security [S10 NarwhalTusk2022]. Contrast: Ouroboros Praos provides fully adaptive security [S12 OuroborosPraos2017 — VERIFIED]. RS-013 §18.6 lists no adaptive-corruption assumption for C6. Mark UNRESOLVED.

### 2.6 Equivocation / double-signing

Depends on composed consensus layer [S10 NarwhalTusk2022]. Tusk/HotStuff safety assumes validators follow the protocol; equivocation handling (slashing/detection) is not part of Narwhal/Tusk core and would come from the external Sybil layer [RS-013 §18.6; S13 CasperFFG2017 if FFG composed]. UNRESOLVED for C6-specific equivocation handling.

### 2.7 Collusion and bribery

Depends on composed layers [S10 NarwhalTusk2022; S37 Bribers2025]. Bribers2025 models bribery contracts (PayToAttest/PayToExit/PayToBias); finds token toxicity alone insufficient [S37 Bribers2025 — VERIFIED]. Applied to C6, bribery resistance depends on the external Sybil layer's slashing/economic-finality design — UNRESOLVED for C6-specific bribery cost.

### 2.8 Censorship

Censorship resistance depends on proposer diversity and the consensus layer's inclusion rules; not a property of Narwhal/Tusk dissemination [RS-013 §7.11]. Dissemination-layer censorship (withholding transactions from the DAG) is a network-layer concern [S15 Eclipse Attacks 2015 — VERIFIED]. UNRESOLVED as a C6-specific measured property.

### 2.9 Long-range attacks

Not applicable in the same way as PoS checkpoint finality — Narwhal/Tusk is a BFT consensus over a fixed committee per instance; long-range attack relevance depends on the composed Sybil layer (if stake-based, long-range applies [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017]) [RS-013 §18.6]. UNRESOLVED for C6-specific long-range analysis.

### 2.10 Resource-exhaustion attacks

Narwhal's DAG dissemination increases per-node bandwidth (each node broadcasts transactions and DAG votes); Tusk adds "zero extra communication" consensus overhead on top of the DAG (consensus piggybacks on DAG messages) [S10 NarwhalTusk2022]. Resource exhaustion is primarily a P2P/network-layer concern [RS-013 §9.12; threat-model T-05]. Leader DDoS is mitigated in Tusk (leaderless async consensus) but present in Narwhal-HotStuff (leader target) [S16 DoS Resilience AsiaCCS2024 — VERIFIED: leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size]. Applied to C6: leaderless Tusk reduces leader-DoS surface; Narwhal-HotStuff retains it — PARTIALLY VERIFIED as applied to C6.

### 2.11 Recovery after network disruption

- **Tusk:** randomized async consensus resumes when a quorum reconnects; no GST required for eventual probabilistic termination [S10 NarwhalTusk2022; S2 FLP1985].
- **Narwhal-HotStuff:** liveness resumes after GST (partial synchrony restored) [S8 HotStuff2019; S3 DLS1988].
- **Narwhal dissemination:** mempool continues under asynchrony; transactions accumulate in the DAG during intermittent connectivity [S10 NarwhalTusk2022].
- During partitions, quorums may be unreachable; liveness sacrificed until reconnection; safety preserved under fault bound [S3 DLS1988; S1 CastroLiskov1999; RS-013 §8.5].

**Verification:** VERIFIED for Tusk async recovery and Narwhal-HotStuff GST recovery [S10 NarwhalTusk2022; S8 HotStuff2019]. VERIFIED for partition safety-preserved/liveness-suspended [S3 DLS1988; S1 CastroLiskov1999; RS-013 §8.5].

---

## 3. Operational architecture

### 3.1 Validator entry and exit

Depends on composed consensus + Sybil layer; not defined by Narwhal/Tusk [S10 NarwhalTusk2022; RS-013 §18.6]. "Validator entry/exit effect on safety: Depends on composed consensus; dissemination layer is independent of validator set [S10 NarwhalTusk2022]" [RS-013 §18.6].

**Status:** UNRESOLVED for C6-specific entry/exit [RS-013 §18.6].

### 3.2 Registration and eligibility

Not defined by Narwhal/Tusk; requires external Sybil layer [S10 NarwhalTusk2022; RS-013 §18.6].

**Status:** UNRESOLVED.

### 3.3 Proposer or leader selection

- **Tusk:** leaderless (asynchronous, randomized) — no single proposer/leader [S10 NarwhalTusk2022].
- **Narwhal-HotStuff:** leader-driven, proposer selected per view from the committee [S8 HotStuff2019; S10 NarwhalTusk2022].

**Verification:** VERIFIED for Tusk leaderless design and Narwhal-HotStuff leader-driven [S10 NarwhalTusk2022; S8 HotStuff2019].

### 3.4 Committee formation

Depends on composed consensus layer; Narwhal's dissemination layer is independent of the validator set [S10 NarwhalTusk2022; RS-013 §18.6]. Committee size and formation are consensus-layer parameters.

**Status:** UNRESOLVED for C6-specific committee formation [RS-013 §18.6].

### 3.5 Validator rotation

Depends on composed consensus layer; not defined by Narwhal/Tusk [RS-013 §18.6].

**Status:** UNRESOLVED.

### 3.6 State synchronization

Depends on composed consensus layer; Narwhal DAG provides transaction availability/causal history, not state sync [S10 NarwhalTusk2022]. State-commitment scheme and snapshot format are OPEN RESEARCH for FST (RS-009; node-roles.md §9.2) [RS-013 §8.6].

**Status:** UNRESOLVED for C6-specific state sync [RS-013 §18.6; node-roles.md §9].

### 3.7 Recovery and checkpoint assumptions

Narwhal/Tusk does not use checkpoints in the Casper-FFG sense; recovery via consensus-layer view changes/leader rotation and state sync [S10 NarwhalTusk2022; RS-013 §8.7]. Weak-subjectivity assumptions apply only if the composed Sybil layer is PoS-based [S40 EthereumWeakSubjectivity2024].

**Status:** UNRESOLVED for C6-specific checkpoint/recovery assumptions [RS-013 §18.6].

### 3.8 Slashing or other punishment assumptions

Not part of Narwhal/Tusk; would come from the external Sybil layer (e.g., Casper FFG slashing if PoS composed) [S10 NarwhalTusk2022; RS-013 §18.6]. Slashing accuracy and centralisation risk are composition-level concerns [RS-013 §6.9; S35 a16z slashing].

**Status:** UNRESOLVED for C6-specific slashing [RS-013 §18.6].

### 3.9 Independent verification (incl. weak subjectivity for PoS)

Depends on composed Sybil layer. If stake-based: weak subjectivity applies [S40 EthereumWeakSubjectivity2024], conflicting with P9. Narwhal/Tusk BFT core does not itself introduce weak subjectivity [S10 NarwhalTusk2022]. Independent verification of the BFT instance requires the committee membership to be publicly verifiable [RS-013 §18.6].

**Status:** UNRESOLVED for C6-specific independent-verification model [RS-013 §18.6].

---

## 4. Performance and network behaviour

### 4.1 Communication complexity

RS-013 explicitly does not assign a single complexity to Narwhal/Tusk: "Narwhal's DAG dissemination cost and Tusk's consensus cost are distinct; this document does not assign a single linear-per-transaction complexity to the combined system" [RS-013 §8.1; S10 NarwhalTusk2022].
- **Narwhal:** per-transaction communication cost depends on DAG dissemination pattern, not simply "linear per transaction" in all cases [RS-013 §8.1; S10 NarwhalTusk2022].
- **Tusk:** "zero extra communication" — consensus overhead piggybacks on DAG messages [S10 NarwhalTusk2022].
- **Narwhal-HotStuff:** O(n) per view via quorum certificates (HotStuff), plus Narwhal dissemination cost [S8 HotStuff2019; S10 NarwhalTusk2022].

**Verification:** VERIFIED for separation of dissemination vs consensus cost and Tusk zero-extra-communication [S10 NarwhalTusk2022]. VERIFIED for HotStuff O(n) per view [S8 HotStuff2019]. No single combined complexity established — RS-013 qualification VERIFIED as a documentation limitation.

### 4.2 Bandwidth

Narwhal DAG dissemination increases per-node bandwidth (broadcast of transactions + DAG votes); exact bandwidth not quantified [RS-013 §8.2; S10 NarwhalTusk2022]. No fixed per-transaction bandwidth figure assigned [RS-013 §8.2]. UNRESOLVED for C6-specific bandwidth numbers.

### 4.3 Latency sensitivity

Tusk: randomized async termination — latency depends on randomization rounds and network delay; probabilistic, not bounded deterministically [S10 NarwhalTusk2022; S2 FLP1985]. Narwhal-HotStuff: multiple communication rounds per view; latency scales with round count and network delay [S8 HotStuff2019; RS-013 §8.3]. Exact round counts/latency ranges omitted [RS-013 §8.3].

### 4.4 Message-size and propagation requirements

Narwhal transactions reference prior transactions (DAG edges); message size includes transaction payload plus references [S10 NarwhalTusk2022]. Propagation scope (gossip vs direct to proposers) unresolved for FST [RS-013 §2.5; RS-015 §5.13]. UNRESOLVED for C6-specific message-size/propagation requirements.

### 4.5 Committee-size sensitivity

Larger committees increase HotStuff/Tusk communication overhead; exact sensitivity not quantified for C6 [RS-013 §8.1 qualification]. UNRESOLVED.

### 4.6 Operation under packet loss

Packet loss increases timeouts/retransmissions, degrading liveness; degree protocol- and implementation-specific [RS-013 §8.9]. Narwhal's async dissemination is more tolerant of variable delay than round-locked BFT [S10 NarwhalTusk2022; RS-013 §19]. UNRESOLVED for C6-specific packet-loss behaviour.

### 4.7 Operation under prolonged partitions

- **Tusk:** safety preserved, liveness suspended under asynchrony; randomized eventual termination when quorum reconnects [S10 NarwhalTusk2022; S2 FLP1985].
- **Narwhal-HotStuff:** safety preserved, liveness suspended until GST/partial synchrony [S8 HotStuff2019; S3 DLS1988].
- **Narwhal dissemination:** mempool/DAG can continue accumulating under asynchrony/intermittent links [S10 NarwhalTusk2022; RS-013 §19].
- CAP: during a partition, global linearizability and full availability cannot both hold [S6 GilbertLynch2002; RS-013 §3.7].

**Verification:** VERIFIED for Tusk async safety+liveness-suspended [S10 NarwhalTusk2022; S2 FLP1985]. VERIFIED for CAP partition trade-off [S6 GilbertLynch2002]. C6-specific partition behaviour depends on composed layer — PARTIALLY VERIFIED as applied to C6.

### 4.8 Recovery after connectivity returns

Tusk: probabilistic termination resumes once quorum reconnects (no GST needed) [S10 NarwhalTusk2022]. Narwhal-HotStuff: resumes after GST [S8 HotStuff2019]. Narwhal DAG: accumulated transactions are ordered once consensus resumes [S10 NarwhalTusk2022]. Recovery time depends on validator count and state size [RS-013 §8.7].

**Verification:** VERIFIED for Tusk async recovery and Narwhal accumulation [S10 NarwhalTusk2022].

---

## 5. Financial and economic implications

### 5.1 Security-budget dependencies

Depends on composed Sybil layer. If stake-based: economic security ≈ value of stake at stake; circular for a new network [S13 CasperFFG2017; S14 Gasper2020; RS-013 §7.5]. Narwhal/Tusk itself has no economic security model [S10 NarwhalTusk2022]. D-015 monetary policy OPEN RESEARCH.

### 5.2 Stake concentration

Depends on composed Sybil layer. If stake-based: concentration maps to voting-power concentration and breaks the consensus fault bound [S35 a16z slashing; S39 IrresbergerYang2023 — PARTIALLY VERIFIED metadata only]. Narwhal/Tusk dissemination layer does not itself concentrate voting power [S10 NarwhalTusk2022].

### 5.3 Reward and fee dependencies

Depends on composed layer. Fees pay for block space; MEV may centralise block production via ordering control [S21 DaianEtAl2020 — PARTIALLY VERIFIED metadata only; Ethereum-specific]. Narwhal/Tusk separates dissemination from ordering; MEV implications depend on the consensus layer's proposer selection [RS-013 §7.12].

### 5.4 Participation costs

Depends on composed Sybil layer. If stake-based: token acquisition + hardware + bonding [RS-013 §6.4]. Narwhal/Tusk itself imposes dissemination bandwidth costs on full participants [S10 NarwhalTusk2022].

### 5.5 Entry barriers

Depends on composed Sybil layer. Narwhal/Tusk permissioned BFT core requires external permissionless admission for D-016 [S10 NarwhalTusk2022; RS-013 §18.6].

### 5.6 Wealth concentration

Depends on composed Sybil layer. If stake-based: rich-get-richer via rewards [RS-013 §6.14; S39 IrresbergerYang2023 — PARTIALLY VERIFIED; study-specific, not universal]. UNRESOLVED as universal.

### 5.7 Extractable value and transaction ordering

MEV arises from ordering control [S21 DaianEtAl2020 — PARTIALLY VERIFIED metadata only; Ethereum-specific]. C6's proposer/separator design (if Narwhal-HotStuff) or leaderless design (Tusk) affects MEV surface differently; not analyzed in inspected sources — UNRESOLVED for C6-specific MEV.

---

## Source / Evidence Appendix — Candidate C6

| Ref | Title | Authors | Date | Venue | DOI/URL | Verification status | Material inspected | Claims supported | Limitations |
|---|---|---|---|---|---|---|---|---|---|
| S10 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys, pp. 34–50 | doi:10.1145/3492321.3519594; arXiv 2105.11827 | VERIFIED | Abstract + full text (§§1, 2, 3.1, 5) | Mempool/consensus separation; Narwhal DAG; Tusk async BFT, zero extra communication, wait-free; Narwhal-HotStuff partial-sync inheritance | Deployment throughput; exact per-transaction complexity; Sybil resistance |
| S2 | Impossibility of Distributed Consensus with One Faulty Process (FLP) | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| S3 | Consensus in the Presence of Partial Synchrony (DLS) | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST; Byzantine N≥3t+1 | Protocol-specific liveness guarantees |
| S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil = one entity, many identities; identity cost | No specific Sybil-resistance solution endorsed |
| S1 | Practical Byzantine Fault Tolerance | Castro, Liskov | 1999 | USENIX OSDI | usenix.org/conference/osdi-99/practical-byzantine-fault-tolerance | PARTIALLY VERIFIED | Full text (Introduction, Service Properties) via HTML mirrors | 3f+1 bound; safety without synchrony; liveness under partial synchrony | O(n²)/view; quorum intersection not stated in inspected text |
| S8 | HotStuff: BFT Consensus with Linearity and Responsiveness | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | ACM PODC | doi:10.1145/3293611.3331591; arXiv 1803.05069 | VERIFIED | Abstract + full text (QC, complexity) | O(n) per view (correct leader); QC = n−f = 2f+1; partial synchrony, safety always, liveness after GST | Worst case O(n²) under cascading leader failures |
| S15 | Eclipse Attacks on Bitcoin's P2P Network | Heilman, Kendler, Zohar, Goldberg | 2015 | USENIX Security | eprint.iacr.org/2015/263 | VERIFIED | Abstract + paper | Eclipse attack feasibility; enables double-spend/selfish mining/adversarial forks | Mitigation effectiveness at scale |
| S16 | An Empirical Study of Consensus Protocols' DoS Resilience | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper | Leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size | Mitigation deployment experience |
| S6 | Brewer's Conjecture ... CAP | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text | C+A+P impossible in async; consistency = atomic/linearizable | Local bounded operation during partitions |
| S17 | Delay-Tolerant Networking Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text | Store-carry-and-forward; intermittent contacts; no end-to-end contemporaneous path | Consensus over DTN specifics |
| S18 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc/rfc9171 | VERIFIED | Full text | Bundle protocol for DTN; persistent storage; asynchronous message primitives | Consensus over DTN specifics |

**Overall assessment status for C6:** OPEN RESEARCH. Dissemination/consensus separation is well-defined and empirically demonstrated [S10 NarwhalTusk2022], but no intrinsic Sybil resistance, no unified security proof for the composition, and voting-power/quorum/adaptive-security depend on uncomposed layers [RS-013 §18.6]. Tusk's randomized async termination addresses FLP differently than partial-synchrony BFT [S10 NarwhalTusk2022; S2 FLP1985]. No design approved.
