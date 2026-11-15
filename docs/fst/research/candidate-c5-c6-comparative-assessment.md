# Comparative Assessment — C5 (Gasper PoS + finality gadget) vs C6 (DAG-based dissemination + separate agreement)

**Classification:** OPEN RESEARCH — no architectural decision approved. This is an evidence-based comparison only; it does not select or endorse either candidate.
**Scope:** Comparison focused on FST's multi-transport requirement (D-019, PROVISIONAL) and offline operation (D-018, PROVISIONAL), with cross-cutting reference to P1–P14, D-015/D-016/D-017.

## Summary conclusion (qualitative, not an approval)

C6 is **not categorically stronger** than C5 for FST, nor is C5 stronger than C6. Each candidate is stronger on different axes relevant to D-019/D-018, and each carries unresolved weaknesses on the other. The DAG dissemination pattern in C6 maps more cleanly to intermittent/multi-transport connectivity (D-019), while C5's deterministic checkpoint finality and explicit weak-subjectivity/offline-recovery model give it a more developed (but trust-dependent) story for prolonged disconnection and recovery (D-018). Both require an external Sybil-resistance layer for permissionless entry (D-016), and both fail to provide disconnected local-finality without a separate offline-payment architecture (D-018). No quantitative performance or economic comparison is possible without inventing numbers; all rankings below are qualitative and source-grounded.

---

## Evidence by dimension

### A. Multi-transport communication (D-019: internet, mesh, radio, satellite, delay-tolerant)

**C6 (Narwhal/Tusk):** Stronger fit on dissemination.
- Narwhal's DAG mempool is explicitly designed to tolerate asynchrony in transaction dissemination; the mempool continues under asynchrony while a separate consensus layer orders [S10 NarwhalTusk2022; RS-013 §5.6, §19].
- Narwhal/Tusk separates dissemination cost from consensus cost, which RS-013 flags as relevant for multi-transport networks where "dissemination and consensus have different communication requirements" [RS-013 §18.6; D-019].
- DTN (store-and-forward, RFC 4838/9171) maps to asynchrony: consensus safety preserved, liveness suspended until contact returns [S17 RFC4838; S18 RFC9171; RS-013 §19]. Narwhal's async dissemination is a relevant pattern here, but consensus over DTN remains open research [RS-013 §8.10].
- Tusk's leaderless randomized async consensus removes the leader as a single DoS/latency target, which is advantageous over variable-delay links [S10 NarwhalTusk2022; S16 DoS Resilience AsiaCCS2024 shows leader-targeted DoS disrupts HotStuff liveness; applied to Narwhal-HotStuff this is PARTIALLY VERIFIED as a concern for the HotStuff variant].
- Caveat: Tusk's randomized termination is probabilistic, not deterministic [S10 NarwhalTusk2022; S2 FLP1985]. Narwhal-HotStuff inherits partial-synchrony liveness, which is less suitable for highly intermittent links [S10 NarwhalTusk2022; S8 HotStuff2019].
- **Verification:** VERIFIED for Narwhal async dissemination and Tusk async BFT [S10]. PARTIALLY VERIFIED for DTN applicability (RFCs VERIFIED; consensus-over-DTN is open research per RS-013 §8.10).

**C5 (Gasper):** Weaker fit on multi-transport.
- LMD-GHOST + Casper FFG assume reliable point-to-point channels and partial synchrony for liveness [S14 Gasper2020; S13 CasperFFG2017; RS-013 §5.5]. Variable latency increases timeouts and degrades liveness [RS-013 §8.3, §8.9].
- Per-slot proposer is a leader target for DoS on variable/unreliable links [S16 DoS Resilience AsiaCCS2024 — VERIFIED for HotStuff-like leaders; PARTIALLY VERIFIED as applied to C5's per-slot proposer].
- No asynchronous dissemination layer; fork choice and finality both depend on timely gossip of attestations [S14 Gasper2020].
- **Verification:** VERIFIED for partial-synchrony liveness assumption [S13 CasperFFG2017; S14 Gasper2020]. PARTIALLY VERIFIED for leader-DoS applicability to C5.

**Assessment on D-019:** C6 has a structural advantage on dissemination-under-intermittency, but this advantage is not yet a proven security property for FST (no multi-transport deployment data; consensus over DTN is open research [RS-013 §8.10]). C5's partial-synchrony dependence is a genuine disadvantage for radio/satellite/DTN links, but C5 could still operate over an internet-connected initial test network (RS-013 §19: initial requirements assume internet connectivity with partial synchrony).

---

### B. Offline operation (D-018: bounded local spending, local operation during disconnection, reconciliation)

**Critical shared limitation:** Neither C5 nor C6 provides offline finality or global consensus during a partition. CAP forbids global linearizability + full availability during partitions [S6 GilbertLynch2002; RS-013 §3.7]. FLP forbids deterministic liveness under full asynchrony [S2 FLP1985]. FST's offline direction (D-018) is bounded local spending authority reconciled later — a separate offline-payment architecture must be composed with either consensus candidate [RS-013 §3.13; scope-v0.1.md Offline financial operation]. This limitation applies equally to C5 and C6.

**C5 (Gasper):**
- During a partition lacking ≥2/3 honest-stake quorum, finality halts; safety preserved; liveness suspended until reconnection [S3 DLS1988; S14 Gasper2020; RS-013 §8.5].
- Nodes offline longer than the weak-subjectivity period cannot independently rejoin; they require an external trusted checkpoint [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017].
- Unbonding > weak-subjectivity period required to prevent long-range attacks after exit [S4 BabylonBlog2023 (=RS-002 S41); S40 EthereumWeakSubjectivity2024].
- Inactivity leak can recover liveness if offline validators' weight drops below 1/3 [S14 Gasper2020; S40 EthereumWeakSubjectivity2024].
- **Advantage:** C5 has an explicit, documented model for recovery after prolonged disconnection (checkpoints, weak subjectivity, inactivity leak) [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017; S14 Gasper2020].
- **Disadvantage:** weak-subjectivity trust conflicts with P9 independent verification; offline-rejoin requires external trust [S40 EthereumWeakSubjectivity2024; RS-013 §18.5].

**C6 (Narwhal/Tusk):**
- Tusk: safety preserved, liveness suspended under asynchrony; randomized eventual termination when a quorum reconnects (no GST needed) [S10 NarwhalTusk2022; S2 FLP1985].
- Narwhal-HotStuff: liveness resumes after GST/partial synchrony [S8 HotStuff2019; S3 DLS1988].
- Narwhal DAG: mempool/DAG can continue accumulating transactions under intermittent connectivity [S10 NarwhalTusk2022].
- **Advantage:** Tusk's randomized async termination does not require GST, so recovery can begin as soon as a quorum reconnects without waiting for a synchrony bound [S10 NarwhalTusk2022]. Narwhal's accumulation-under-asynchrony suits store-and-forward links [S10 NarwhalTusk2022; S17 RFC4838; S18 RFC9171].
- **Disadvantage:** no checkpoint/weak-subjectivity model; no documented long-offline rejoin procedure; recovery assumptions depend on the uncomposed consensus layer [RS-013 §18.6]. No weak-subjectivity trust issue (no weak subjectivity in the BFT core), but also no established offline-recovery mechanism.

**Assessment on D-018:** C5 has a more developed *documented* offline/recovery model (checkpoints + weak subjectivity + inactivity leak), but it depends on weak-subjectivity trust (P9 tension). C6's Tusk variant offers GST-free async recovery and Narwhal accumulation under intermittency, but lacks a documented checkpoint/rejoin model. Neither satisfies D-018 offline operation without a separate bounded-offline-payment architecture.

---

### C. Cross-cutting dimensions relevant to D-019/D-018

| Dimension | C5 (Gasper) | C6 (Narwhal/Tusk) | Verification basis |
|---|---|---|---|
| Intrinsic Sybil resistance | Stake (Casper FFG) [S14] | None; external layer required [S10] | VERIFIED [S10; S14] |
| Agreement/ordering | LMD-GHOST + Casper FFG [S14] | Tusk async BFT or Narwhal-HotStuff [S10] | VERIFIED [S10; S14] |
| Voting-power derivation | Stake-weighted; per-slot proposer [S14] | Depends on composed layer [S10] | VERIFIED [S14]; UNRESOLVED [C6] |
| Byzantine threshold | ≥2/3 honest stake [S13/S14] | <1/3 committee (Tusk/HotStuff) [S10/S8] | VERIFIED (per layer) |
| Adaptive corruption | Not established [S13/S14] | Not established [S10] | UNRESOLVED both |
| Weak subjectivity / P9 | Required (P9 conflict) [S40] | Not in BFT core; depends on Sybil layer [S10/S40] | VERIFIED [S40]; UNRESOLVED [C6 composition] |
| Finality type | Deterministic checkpoint (qualified) [S14] | Deterministic (HotStuff) or randomized-probabilistic (Tusk) [S10/S8] | VERIFIED [S10; S14; S8] |
| Liveness synchrony | Partial synchrony [S13/S14] | Tusk: none (async randomized); HotStuff: partial [S10/S8] | VERIFIED [S10; S13; S8; S2] |
| Communication complexity | Not assigned [RS-013 §8.1] | Dissemination ≠ consensus; not assigned [S10; RS-013 §8.1] | VERIFIED (documentation gap) |
| Leader DoS surface | Per-slot proposer [S16 partial] | Tusk leaderless; HotStuff leadered [S10; S16] | VERIFIED [S10; S16] |
| Deployment track record | Ethereum consensus clients (multiple independent impls) [RS-013 §11] | Less deployment history [RS-013 §18.6] | VERIFIED (RS-013 §11 lists Ethereum clients) |
| Formal security proof | Accountable safety [S13/S14] | No single unified proof for composition [RS-013 §18.6] | VERIFIED [S13; S14]; PARTIALLY VERIFIED [C6 composition claim] |
| Economic circularity (new network) | Yes (stake funds security) [S13/S14] | Depends on Sybil layer [RS-013 §18.6] | VERIFIED [S13/S14]; UNRESOLVED [C6] |
| Stake concentration / P1-P4 | Yes, if stake-based [S35/S39] | Depends on Sybil layer [RS-013 §18.6] | VERIFIED [S35]; UNRESOLVED [C6] |

---

### D. Why neither candidate satisfies D-018 alone

RS-013 Section 3.13 defines four FST operation modes; global blockchain consensus (mode 1) is distinct from local regional consensus (mode 2), bounded offline spending (mode 3), and global settlement after reconnection (mode 4) [RS-013 §3.13; scope-v0.1.md Offline financial operation]. CAP and FLP constraints on global consensus do not preclude bounded local operation during disconnection [RS-013 §3.13], but they mean **no global-consensus protocol (C5 or C6) can provide offline finality**. D-018's bounded offline spending authority must be implemented as a separate layer composed with whichever consensus is selected [RS-013 §3.13; RS-002 §7; threat-model T-04]. This is a hard constraint that neutralizes any D-018-based preference between C5 and C6 at the consensus layer.

---

### E. D-019 / D-018 comparative verdict

**Is C6 a stronger case than C5 for FST given D-019 and D-018?**

- **On D-019 (multi-transport):** C6 is qualitatively better positioned because Narwhal's asynchronous DAG dissemination tolerates intermittent, high-delay links, and Tusk's leaderless async consensus avoids GST dependence and leader DoS targeting [S10 NarwhalTusk2022; S17/S18 RFCs; RS-013 §19]. C5's partial-synchrony liveness and per-slot proposer are less suited to radio/satellite/DTN [S13/S14 Gasper; S16 leader-DoS]. However, C6's advantage is structural, not yet validated for FST's transport stack (consensus over DTN is open research [RS-013 §8.10]).
- **On D-018 (offline operation):** C5 has a more developed documented recovery model (weak-subjectivity checkpoints, inactivity leak, unbonding relationship) [S40 EthereumWeakSubjectivity2024; S13 CasperFFG2017; S4 BabylonBlog2023], but this depends on weak-subjectivity trust that conflicts with P9. C6 offers GST-free async recovery (Tusk) and DAG accumulation under intermittency [S10 NarwhalTusk2022], but lacks a documented checkpoint/rejoin model. Neither provides offline finality; both require a separate bounded-offline-payment architecture [RS-013 §3.13].

**Overall:** C6 is **not** a uniformly stronger case than C5. C6 is stronger on dissemination-under-intermittency (D-019) and GST-free recovery (Tusk); C5 is stronger on deterministic finality and a documented, checkpoint-based recovery/rejoin model (albeit with weak-subjectivity trust). Both fail equally on the core D-018 constraint (no offline finality without a separate architecture) and both require an unresolved external Sybil layer for D-016 permissionless entry. The choice between C5 and C6 for FST remains OPEN RESEARCH and cannot be resolved from the inspected sources without: (a) selecting a Sybil-resistance mechanism (D-016), (b) defining the adversary model and finality requirements (D-017), and (c) multi-transport/DTN consensus adaptation research (D-019, RS-013 §8.10).

---

## Source / Evidence Appendix — Comparative Assessment

| Ref | Title | Authors | Date | Venue | DOI/URL | Verification status | Material inspected | Claims supported | Limitations |
|---|---|---|---|---|---|---|---|---|---|
| S10 | Narwhal and Tusk: A DAG-based Mempool and Efficient BFT Consensus | Danezis, Kokoris-Kogias, Sonnino, Spiegelman | 2022 | EuroSys pp. 34–50 | doi:10.1145/3492321.3519594; arXiv 2105.11827 | VERIFIED | Abstract + full text (§§1, 2, 3.1, 5) | Mempool/consensus separation; Narwhal DAG async dissemination; Tusk async BFT, zero extra communication; Narwhal-HotStuff partial-sync | Deployment throughput; exact complexity; Sybil resistance |
| S13 | Casper the Friendly Finality Gadget | Buterin, Griffith | 2017 | IACR ePrint / arXiv | arxiv.org/abs/1710.09437 | PARTIALLY VERIFIED | Abstract + §§1–2 + theorems | Checkpoint finality; 2/3 supermajority; accountable safety; plausible liveness | LMD-GHOST/weak subjectivity not in FFG paper |
| S14 | Combining GHOST and Casper (Gasper) | Buterin et al. | 2020 | arXiv | arxiv.org/abs/2003.03052 | VERIFIED | Abstract + §§1–4 | LMD-GHOST + Casper FFG; stake-weighted voting; per-slot proposer | Exact parameters; adaptive security |
| S2 | FLP Impossibility | Fischer, Lynch, Paterson | 1985 | JACM 32(2) | doi:10.1145/3149.214121 | VERIFIED | Abstract + full text | Deterministic termination impossible under asynchrony + 1 crash | Randomized async consensus; partial-synchrony liveness |
| S3 | Partial Synchrony (DLS) | Dwork, Lynch, Stockmeyer | 1988 | JACM 35(2) | doi:10.1145/42282.42283 | VERIFIED | Abstract + full text (Table I) | Unknown-bound model; safety always / liveness after GST | Protocol-specific liveness |
| S5 | The Sybil Attack | Douceur | 2002 | IPTPS (LNCS 2429) | doi:10.1007/3-540-45748-8_24 | VERIFIED | Full text (Abstract, model, conclusions) | Sybil = many identities; identity cost | No Sybil-resistance solution endorsed |
| S6 | CAP Theorem | Gilbert, Lynch | 2002 | SIGACT News 33(2) | doi:10.1145/564585.564601 | VERIFIED | Abstract + Theorem 1 + full text | C+A+P impossible in async | Local bounded operation during partitions |
| S8 | HotStuff | Yin, Malkhi, Reiter, Golan Gueta, Abraham | 2019 | ACM PODC | doi:10.1145/3293611.3331591 | VERIFIED | Abstract + full text (QC, complexity) | O(n) per view; QC=2f+1; partial synchrony safety/liveness | Worst case O(n²) under cascading leader failures |
| S16 | DoS Resilience of Consensus Protocols | ETH Zurich / Sonnino et al. | 2024 | AsiaCCS | arxiv 2012.01636 | VERIFIED | Abstract + paper | Leader-targeted DoS disrupts HotStuff liveness within seconds; cost independent of committee size | Mitigation deployment experience |
| S17 | DTN Architecture | Cerf et al. (IETF/IRTF) | 2007 | RFC 4838 | rfc-editor.org/rfc/rfc4838 | VERIFIED | Full text | Store-carry-and-forward; intermittent contacts | Consensus over DTN specifics |
| S18 | Bundle Protocol v7 | Sipos et al. (IETF) | 2020 | RFC 9171 | rfc-editor.org/rfc/rfc9171 | VERIFIED | Full text | DTN bundle protocol; persistent storage; async primitives | Consensus over DTN specifics |
| S35 | Cryptoeconomics of Slashing | a16z Frontier | 2023-01-25 | a16zfrontier.com | a16zfrontier.com/posts/article/the-cryptoeconomics-of-slashing | VERIFIED | Web page | Slashing economics; custodial/LST centralisation | Primary-source (industry blog) |
| S36 | Stake-Bleeding Attacks | Gaži, Kiayias, Russell | — | NSF PAR | par.nsf.gov/servlets/purl/10099216 | VERIFIED | Abstract + paper | Stake bleeding; posterior corruption | — |
| S40 | Weak Subjectivity | Ethereum Foundation | Current | ethereum.org docs | ethereum.org/en/developers/docs/consensus-mechanisms/pos/weak-subjectivity | VERIFIED | Docs | Weak subjectivity; checkpoints; long-range defence; unbonding relation | Protocol parameter values; not peer-reviewed |
| RS-013 §3.13 | Four operation modes (FST scope) | FST project | 2026-11-15 | docs/fst/research/consensus-validator-research.md | internal | PARTIALLY VERIFIED | RS-013 full text | Four modes; offline ≠ global consensus | FST research document, not peer-reviewed |
| scope-v0.1.md | FST Project Scope v0.1 | FST project | 2026-11-15 | docs/fst/scope/scope-v0.1.md | internal | PARTIALLY VERIFIED | Full text | D-018 offline operation; D-019 multi-transport; D-016 permissionless | FST scope document, not peer-reviewed |

**Final status:** All findings OPEN RESEARCH. No architectural decision approved. C6 is not categorically stronger than C5 for FST given D-019/D-018; each has domain-specific advantages and unresolved weaknesses.
