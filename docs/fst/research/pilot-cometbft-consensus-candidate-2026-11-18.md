# Pilot Research Investigation — CometBFT as the FST Consensus Candidate (D-017 / M-03)

## Metadata

| Field | Value |
|-------|-------|
| Investigation ID | PILOT-COMETBFT-2026-11-18 |
| Date | 2026-11-18 |
| Agent / role | fst-research (Research Agent) |
| Classification | Pilot research report — read-only investigation; no implementation decision |
| Decision context | D-017 (APPROVED — stake-weighted BFT consensus with deterministic finality; CometBFT preferred candidate subject to feasibility evaluation), ADR-0004 (ACCEPTED), M-03 (DEFERRED-BY-DESIGN — CometBFT feasibility, security and real multi-validator acceptance evidence) |
| Related ADR | ADR-0004 "BFT consensus, finality and CometBFT integration" |
| Related decision | D-017 APPROVED; D-016 APPROVED (permissionless PoS); D-037 APPROVED (UTXO ledger); D-015 APPROVED (monetary policy); D-036 APPROVED (fork protection requirements) |
| Source constraint | Existing local sources ONLY: `docs/fst/research/` and `docs/fst/architecture/cometbft-integration-feasibility-spike.md`. No external URLs fetched. No production C++, tests, CI, decision register, or accepted ADRs modified. |
| Verification dates | Local sources accessed 2026-11-18; feasibility spike fetched/recorded 2026-11-17; RS-013/RS-002 verification date 2026-11-15 |
| Owner decision required? | Yes — proposed ADR outline in Section 16; the Research Agent does not approve its own recommendation |

---

## 1. Executive summary

This pilot investigation evaluates CometBFT v0.40.0 as the FST consensus engine candidate under approved decision D-017 and ADR-0004. All evidence is drawn exclusively from existing local FST sources: the CometBFT integration feasibility spike (`docs/fst/architecture/cometbft-integration-feasibility-spike.md`, FST-SPIKE-COMETBFT-001, status COMPLETE), ADR-0004, the decision register, the engineering charter, the project plan, the decision-closure roadmap (M-03), the consensus/Sybil research entries (RS-013, RS-002), the adversary and threat models, and the repository README.

Finding: CometBFT v0.40.0 is the APPROVED preferred candidate and the most completely evidenced option available locally, but it remains **an approved candidate, not a committed production dependency**. The feasibility spike is complete at the documentation/review level; the executable prototype and ≥4-validator multi-validator acceptance evidence cannot be produced in this environment because the Go 1.25.0 toolchain required by CometBFT v0.40.0 is not installed (`go: command not found`). That is a prerequisite, not an architectural blocker. No material architectural incompatibility was identified for running CometBFT as a separate process with a deterministic C++ FST ABCI application over a local Unix-domain socket (ABCI++ / ABCI 2.0). The dominant integration risks are (a) Go toolchain availability, (b) deterministic C++ application execution, and (c) correct activation-height handling of validator-set updates.

Recommendation: proceed with CometBFT v0.40.0 as the preferred consensus engine candidate, pin the release (not `main`), require Go 1.25.0, complete the feasibility spike's remaining executable evidence (prototype + ≥4-validator multi-node evidence + verification gate), and obtain an owner decision via the proposed ADR before CometBFT becomes a production dependency. HotStuff/Casper/Narwhal remain documented OPEN RESEARCH fallbacks; Nakamoto PoW and FeinStatic Raft are not viable for D-017.

---

## 2. FST requirements and constraints

All requirements below are taken from approved local documents (decision register, scope v0.1, engineering charter, ADR-0004, project plan).

### 2.1 Approved decisions governing consensus selection

| Decision | ID | Classification | Content relevant to consensus |
|----------|-----|----------------|------------------------------|
| BFT consensus and finality | D-017 | APPROVED (ADR-0004) | Stake-weighted Byzantine fault-tolerant consensus with deterministic finality; preferred implementation candidate Tendermint/CometBFT family subject to feasibility evaluation. |
| Permissionless validator participation | D-016 | APPROVED (ADR-0003) | Permissionless native-FST Proof of Stake; validator identity separate from node identity; voting power from active bonded stake; deterministic staking, delegation assessment, registration, exit and unbonding; evidence-based slashing. |
| Transaction model | D-037 | APPROVED (ADR-0002) | UTXO-based canonical financial ledger; atomic multi-input/multi-output transactions; domain-separated signatures; network/chain identifiers; replay protection; deterministic validation; no general-purpose VM in the initial core protocol. |
| Monetary policy | D-015 | APPROVED (ADR-0005) | Predetermined declining issuance approaching a small permanent tail emission, combined with congestion-adjusted transaction fees; base fee burned, priority rewards to block-producing validators. Numeric parameters remain subject to research and modeling (M-01, M-11 BLOCKED-ON-OWNER). |
| Fork protection requirements | D-036 | APPROVED (requirements); mechanisms OPEN RESEARCH | Unique network identity, replay protection, distinct currency identifiers for independent networks. |

### 2.2 Engineering principles (P1–P14) governing the evaluation

- P1: No single organisation with unilateral control over FST.
- P2: No permanent privileged protocol authority.
- P4: Independent teams must be able to maintain the software.
- P5: Participants can reject proposed upgrades.
- P6: Forks may be legitimate; neither has exclusive legitimacy.
- P8: No central emergency administrator or unilateral master key.
- P9: Security from protocol design and independent verification.
- P10: Network survives disappearance of founding developers.
- P12: Research-first engineering for every module.

### 2.3 Architectural constraints (from ADR-0004 and the scope)

- Consensus is separated from transaction execution and financial economics. CometBFT must NOT implement FST monetary policy, staking, transaction validation, or validator economics — those belong to the deterministic FST application (ADR-0004 problem statement).
- CometBFT runs as a separate consensus process per validator node; the C++ FST application connects over a local Unix-domain socket using the ABCI protocol. The application interface is not exposed publicly (ADR-0004 selected design; scope current architecture).
- The FST ABCI application must be deterministic: a pure function of (prior state, committed transactions). No wall clock, unordered iteration, floating-point calculation, or local filesystem input may influence consensus state transitions. Floating-point is prohibited for consensus-critical financial calculations (exact bounded integer/fixed-point only) (ADR-0004 determinism section).
- Validator voting power is derived from deterministic FST application state (staking module, ADR-0003), not from CometBFT internals (ADR-0004).
- Validator-set updates follow CometBFT's documented activation-height semantics: a validator-set change proposed at height H becomes part of the next validator set and takes effect as the active set at height H+2 (feasibility spike §6).
- Committed application state must be persisted before `Commit` acknowledges; crash recovery must not allow partially committed financial state to become authoritative (atomic commit semantics) (ADR-0004).
- The application hash is a reproducible state root over committed application state, returned in `Commit` and included in the block header; every correct node computes an identical hash (ADR-0004).
- CometBFT `main` branch is explicitly NOT a production dependency (CometBFT release policy: "Please do not depend on main as your production branch. Use releases instead.") (feasibility spike §1).

### 2.4 Phase context (project plan)

- Phases 0–2 complete; Phases 3–4 UNBLOCKED by owner decisions D-015/D-016/D-017/D-037 via ADRs ADR-0002/0003/0004/0005.
- Phase 3 requirements FST-REQ-0034–0039 (consensus implementation) remain BLOCKED on FST-REQ-0031 (consensus algorithm selection ADR) even though ADR-0004 is ACCEPTED; the implementation gate requires the feasibility evidence this investigation addresses (M-03).
- M-03 (decision-closure roadmap) is DEFERRED-BY-DESIGN, owned by fst-research with the fst-verification gate; closure condition: CometBFT feasibility + security evidence accepted; deterministic finality verified on a real multi-validator network.

### 2.5 Adversary model constraints (local)

- Classical/leader-based BFT fault bound `f_bft`: <1/3 Byzantine voting power (3f+1 replicas for f faults) with a known fixed replica set; partial synchrony for liveness; safety holds without synchrony (adversary model §3.2, §9.2; RS-013 §3.2).
- FLP: deterministic termination impossible under full asynchrony with even one crash failure; liveness requires timing assumptions (partial synchrony) or randomization (RS-013 §3.1, adversary model §9.8).
- CAP: during a partition, global linearizability and full availability cannot both hold; safety is preserved under the fault bound, liveness is suspended until reconnection (adversary model §3.7; RS-013 §8.5).
- No universal honest-majority constant is approved; each candidate states its own threshold as a parameter (adversary model §4).
- Offline operation (D-018, PROVISIONAL): no global-consensus protocol (CometBFT included) provides offline finality; bounded offline spending authority requires a separate offline-payment architecture composed with the consensus layer (RS-013 §3.13, §19; scope-v0.1 §Offline financial operation). This is a shared limitation of all candidates, not a CometBFT-specific weakness.

---

## 3. Candidate technologies and alternatives

Candidates are drawn from ADR-0004 §Alternatives evaluated, RS-013 §5/§13/§18, and the feasibility spike. Each candidate's classification reflects the local decision register; none of the OPEN RESEARCH candidates may be implemented as production design.

### 3.1 Candidate under evaluation: CometBFT v0.40.0 (Tendermint/CometBFT family)

- **What it is:** A fork of Tendermint Core implementing leader-based BFT consensus (BFT SMR) with deterministic finality under >2/3 voting power; round-based Propose/Prevote/Precommit; voting-power-weighted proposer selection round-robin proportional to voting power (RS-013 §5.2, reference #10 CometBFT consensus specification; feasibility spike §1).
- **Classification:** APPROVED candidate under D-017/ADR-0004, subject to mandatory feasibility evaluation; not yet a committed production dependency.
- **Local evidence:** feasibility spike (accessed 2026-11-18; fetched 2026-11-17), ADR-0004, RS-013 references #9–#10, adversary model §7 candidate C2 row.
- **Integration model:** separate process per validator; C++ FST ABCI application over local Unix-domain socket; ABCI++ (ABCI 2.0) since v0.38.0 (feasibility spike §1, §5).

### 3.2 Alternative A: HotStuff / C1 composition (leader-based BFT + stake Sybil layer)

- **What it is:** Leader-driven BFT with quorum certificates; O(n) communication per view via quorum certificates; deterministic finality under partial synchrony (RS-013 §5.2 HotStuff, reference #8; C1 composition in ADR-0004).
- **Classification:** OPEN RESEARCH alternative (ADR-0004 alternatives table).
- **Advantages:** optimistic responsiveness, linear view changes, deterministic finality.
- **Disadvantages:** less mature production deployment; deterministic-finality model differs; more integration effort; permissioned consensus core requires an external stake Sybil layer for permissionless entry (RS-013 §18.1).
- **Why not selected now:** CometBFT is the APPROVED preferred candidate under D-017 with a completed local feasibility spike; HotStuff remains an OPEN RESEARCH fallback documented in ADR-0004.

### 3.3 Alternative B: Nakamoto PoW / C3

- **What it is:** Proof-of-Work longest-chain consensus; probabilistic finality with k-block confirmation depth; honest-majority-of-hash-rate (<50% adversarial) assumption (RS-013 §5.3, reference #7).
- **Classification:** Not viable for D-017 (ADR-0004 alternatives table).
- **Reasons:** probabilistic finality conflicts with the D-017 deterministic-finality direction; high energy cost; mining-pool centralisation; slow settlement (minutes to hours); 51% rental-attack economics for smaller networks (RS-013 §5.3, §7.6, §18.3).

### 3.4 Alternative C: Casper FFG / Gasper / C5 (PoS + separate finality gadget)

- **What it is:** LMD-GHOST fork choice + Casper FFG finality gadget; deterministic checkpoint finality qualified by Byzantine-stake and slashing assumptions; ≥2/3 honest stake for finality; accountable safety (≥1/3 slashable on conflicting finality); weak-subjectivity checkpoints for long-range defence (RS-013 §5.5, §18.5; candidate-c5-evaluation.md).
- **Classification:** OPEN RESEARCH alternative (ADR-0004 alternatives table).
- **Disadvantages vs CometBFT:** weak-subjectivity trust required for new/returning nodes (tension with P9 independent verification); stake concentration and custodial/LST centralisation challenge P1/P4; economic circularity (security budget depends on token value, D-015 unresolved); long-range attacks; nothing-at-stake; unbonding/weak-subjectivity period relationship unresolved for FST parameters (candidate-c5-evaluation.md §2.9, §3.9).
- **Why not selected now:** OPEN RESEARCH; CometBFT is the APPROVED preferred candidate; Gasper remains a documented fallback.

### 3.5 Alternative D: Narwhal/Tusk / C6 (DAG-based dissemination + separate agreement/finality)

- **What it is:** Narwhal DAG mempool for transaction dissemination (asynchronous, tolerates asynchrony) + Tusk asynchronous BFT consensus (randomized probabilistic termination, no GST required) or Narwhal-HotStuff (partial-synchrony liveness). No intrinsic Sybil resistance; external stake/work layer required (RS-013 §5.6, §18.6; candidate-c6-evaluation.md).
- **Classification:** OPEN RESEARCH alternative (ADR-0004 alternatives table).
- **Advantages:** dissemination/consensus cost separation structurally better fits intermittent/multi-transport links (D-019); Tusk leaderless design removes leader DoS target; Narwhal mempool continues under asynchrony.
- **Disadvantages:** no intrinsic Sybil resistance; no unified security proof for the composition; less deployment history; voting-power/quorum/adaptive-security depend on uncomposed layers; Tusk termination is probabilistic, not deterministic (candidate-c5-c6-comparative-assessment.md).
- **Why not selected now:** OPEN RESEARCH; D-019 is PROVISIONAL and deferred; CometBFT is the APPROVED preferred candidate under D-017.

### 3.6 Alternative E: FeinStatic Raft / D-024

- **What it is:** The existing FeinStatic consensus implementation preserved as reference.
- **Classification:** Reference only; PROVISIONAL (D-024).
- **Reason rejected:** crash-fault tolerant only, not Byzantine; permissioned; conflicts with P9 and D-017; cannot be incrementally extended to meet FST requirements (RS-013 §1, ADR-0004 alternatives table).

---

## 4. Comparison matrix

All entries are qualitative and source-grounded from local documents; no quantitative performance figures are invented (RS-013 §8.1 qualification: protocol-specific claims require source-specific inspection; asymptotic notation does not infer deployment performance). "PV" = PARTIALLY VERIFIED per local verification conventions; "V" = VERIFIED.

| Criterion | CometBFT v0.40.0 (candidate) | HotStuff / C1 | Nakamoto PoW / C3 | Gasper / C5 | Narwhal/Tusk / C6 | FeinStatic Raft / D-024 |
|-----------|------------------------------|---------------|-------------------|-------------|-------------------|-------------------------|
| Finality type | Deterministic under >2/3 voting power (RS-013 ref #10; spike §1) | Deterministic under fault bound (RS-013 ref #8) | Probabilistic, k-block depth (RS-013 ref #7) | Deterministic checkpoint finality, qualified by Byzantine-stake/slashing/weak subjectivity (RS-013 refs #14–#15) | Deterministic (HotStuff variant) or randomized-probabilistic (Tusk) (RS-013 refs #8, #11) | Crash-fault only, deterministic (reference; not Byzantine) |
| BFT fault bound / safety assumption | <1/3 Byzantine voting power; quorum intersection; safety without synchrony (RS-013 ref #10; adversary model §7 C2) | <1/3 Byzantine voting power; partial synchrony (RS-013 ref #8) | <50% adversarial hash rate (RS-013 ref #7) | ≥2/3 honest stake; accountable safety ≥1/3 slashable (RS-013 refs #13–#15) | <1/3 Byzantine committee (Tusk) / <1/3 voting power (HotStuff) (RS-013 refs #11, #8) | N/A — crash-fault only |
| Liveness assumption | Partial synchrony; leader rotation (RS-013 ref #10; spike §1) | Partial synchrony (RS-013 ref #8) | Honest-majority hash rate; delay-dependent margin (RS-013 refs #7, #27) | Partial synchrony; ≥2/3 honest stake (RS-013 refs #13–#15) | Tusk: none for probabilistic termination (async randomized); HotStuff variant: partial synchrony (RS-013 ref #11) | Crash-fault model; not applicable to Byzantine liveness |
| Sybil resistance (intrinsic) | None — requires external stake layer (RS-013 §5.2) | None — requires external stake layer (RS-013 §5.2) | Proof of Work (intrinsic) (RS-013 ref #7) | Stake lockup via Casper FFG (intrinsic to composition) (RS-013 ref #15) | None — external layer required (RS-013 ref #11) | None — permissioned |
| Permissionless network admission | No (consensus core); permissionless via external staking layer (D-016, ADR-0003) | No (consensus core); permissionless via external staking layer | Yes (open mining) | Yes (open staking) | No (consensus core); permissionless via external Sybil layer | No — permissioned operator set |
| Voting-power / proposer derivation | Stake-weighted; round-robin proposer proportional to voting power (RS-013 ref #10) | Stake-weighted; round-robin or stake-weighted from committee (RS-013 refs #8, #16) | Hashrate-weighted; puzzle solver proposes (RS-013 ref #7) | Stake-weighted; proposer assigned per slot (RS-013 ref #15) | Depends on composed consensus + Sybil layer (RS-013 ref #11) | Fixed operator set |
| Implementation maturity / deployment track record | Actively maintained; used in Cosmos SDK ecosystem (RS-013 §11; spike §1); v0.40.0 latest stable | Less mature production deployment (ADR-0004 alternatives) | Long deployment track record (Bitcoin) (RS-013 §5.3) | Multiple independent Ethereum consensus clients (RS-013 §11) | Less deployment history (RS-013 §18.6; comparative assessment §E) | FeinStatic prototype; crash-fault only |
| Licensing | Apache-2.0 (spike §4) | Varies by implementation | Varies (Bitcoin Core MIT) | Varies by client | Varies by implementation | FeinStatic provenance |
| Language / integration model | Go (separate process); ABCI++ over TCP/UDS/gRPC; FST C++ app over local UDS (spike §5; ADR-0004) | Varies; typically Go/Rust; separate process likely | C++ (Bitcoin Core) or separate mining | Go/Rust clients; separate process | Typically Go/Rust; separate process | C++ (FeinStatic, in-process) |
| Known security advisories (local record) | CSA-2026-001/Tachyon Critical fixed in v0.38.21/v0.37.18, included in v0.40.0 (inference basis; spike §2); ASA-2025-003/002/001, ASA-2024-011/009/008/001 all fixed before v0.40.0 (spike §2) | Not tracked in local sources | Not tracked in local sources | Not tracked in local sources as FST-relevant advisories | Not tracked in local sources | Not applicable — FeinStatic baseline |
| Determinism requirement on application | Yes — pure function of (prior state, txs); no wall clock/unordered iteration/float/filesystem input (ADR-0004) | Yes (BFT determinism) | Yes (deterministic execution) | Yes | Yes | N/A |
| Validator-set update semantics | Activation-height semantics; change at H active at H+2 (spike §6; ADR-0004) | Epoch-based reconfiguration (DiemBFT pattern, RS-013 ref #16) | Open entry/exit by hashrate | Per-epoch committee changes; unbonding > weak-subjectivity period (RS-013 refs #13–#15) | Depends on composed layer (RS-013 ref #11) | Static operator set |
| State sync / snapshots / light client | ABCI snapshot methods (ListSnapshots/OfferSnapshot/LoadSnapshotChunk/ApplySnapshotChunk) + block sync; `light` package (spike §7) | Implementation-specific | SPV/headers (Bitcoin) | Checkpoints + weak subjectivity (RS-013 ref #18) | Depends on composed layer (RS-013 ref #11) | FeinStatic snapshot patterns (D-032 reference) |
| Operational overhead | Separate Go process per validator; Go 1.25.0 toolchain; config/key/metrics management (spike §9) | Separate process likely; similar operational profile | Mining hardware + energy | Validator nodes; slashing infrastructure | More components; separation adds complexity | Low (existing FeinStatic process) |
| Multi-transport fit (D-019) | Partial-synchrony dependence; per-slot proposer is a leader DoS target on variable/unreliable links (RS-013 §8.3/§8.9; S16 PV as applied to Tendermint-style leaders) | Leader-centric; leader DoS surface (RS-013 §8.4; S16) | More tolerant of variable delay; probabilistic finality needs longer confirmation under high delay (RS-013 §19) | Similar to CometBFT; per-slot proposer (RS-013 §19) | Stronger structural fit: Narwhal async DAG dissemination tolerates intermittency; Tusk leaderless async avoids GST and leader targeting (RS-013 ref #11; comparative assessment §A) | Not applicable — permissioned internet topology |
| Offline operation fit (D-018) | No offline finality; requires separate bounded offline-payment architecture (shared limitation; RS-013 §3.13) | Same shared limitation | Same shared limitation | Same shared limitation; weak-subjectivity rejoin needs external checkpoint (P9 tension) | Same shared limitation; Tusk GST-free async recovery, no documented checkpoint/rejoin model | Not applicable |
| P1/P2 organisational-control risk | Maintained by single organisation (Cosmos Labs / Interchain Foundation); managed dependency risk, not a P1/P2 violation per spike §10; Apache-2.0 fork right supports P10 | Depends on implementation governance | Decentralised mining (but pool concentration observed) | Ethereum Foundation-associated clients; stake concentration risk | Research-stage governance | Single-codebase prototype; not decentralised |
| Known weaknesses | Leader-based (leader DoS surface); stake concentration challenges P1/P4 if stake layer centralises; Go toolchain requirement; separate-process operational overhead; 10k TPS claim indicative only | Leader DoS; stake concentration; less deployment history | Probabilistic finality; energy; 51% attacks; slow settlement | Weak subjectivity (P9 conflict); long-range attacks; stake concentration; economic circularity | No intrinsic Sybil; no unified composition proof; probabilistic Tusk termination; less deployment history | Crash-fault only; not BFT |
| Classification for FST | APPROVED candidate (D-017), feasibility-gated | OPEN RESEARCH fallback | Not viable (D-017 conflict) | OPEN RESEARCH fallback | OPEN RESEARCH fallback | Reference only |

---

## 5. Security and threat assessment

### 5.1 Consensus security properties (CometBFT / leader-based BFT)

- Safety: deterministic under the <1/3 Byzantine voting-power bound via quorum intersection; safety holds without synchrony assumptions (RS-013 §5.2 reference #10 CometBFT consensus specification; adversary model §3.2).
- Liveness: requires partial synchrony; leader rotation and view changes recover liveness after synchrony is restored (RS-013 §5.2; adversary model §9.4).
- FLP boundary: deterministic liveness cannot be guaranteed under full asynchrony; partial synchrony (or randomization) is required (RS-013 §3.1; adversary model §9.8).
- CAP boundary: during a partition, safety is preserved under the fault bound and liveness is suspended until reconnection (RS-013 §8.5; adversary model §3.7).
- No weak-subjectivity trust: unlike Gasper/C5, CometBFT is classical BFT over a validator set; new/returning nodes synchronize via blocks and state sync rather than trusted checkpoints (adversary model §3.9 C5 contrast; RS-013 candidate C2).

### 5.2 Threat-class mapping (from local adversary/threat models)

| Threat class | Local ID | CometBFT relevance | Residual risk |
|--------------|----------|-------------------|---------------|
| Byzantine participants / validator coalitions | T-011 (threat model); T-03 (adversary model) | Addressed by CometBFT only once integrated; current baseline has NO BFT (Raft is crash-fault only). Safety fails if Byzantine voting power ≥ `f_bft`; liveness fails under asynchrony or leader unavailability (adversary model §5.3). | HIGH (no BFT in current baseline) |
| Sybil identity operators | T-02 | CometBFT's BFT core has no intrinsic Sybil resistance; permissionless entry comes from the external FST staking layer (D-016/ADR-0003). Stake concentration can break the <1/3 assumption (candidate-c5-evaluation §1.5 applied to stake-weighted BFT; RS-013 §9.2). | HIGH until staking layer designed and approved (D-015/D-016 parameters BLOCKED-ON-OWNER) |
| Network-level attackers / partitions / eclipse | T-04, T-03, T-06 | Partial synchrony for liveness; multiple bootstrap sources and signed node records already implemented at the FST network layer (threat model T-03/T-04 mitigations). CometBFT P2P handles consensus messaging between validators; default p2p transport is legacy (libp2p experimental in v0.40.0 — must not be enabled for consensus networking, spike §9). | Partially mitigated at network layer; consensus-layer partition behaviour per DLS/CAP |
| Resource exhaustion / DoS | T-05 | Leader (proposer) is a DoS target on variable/unreliable links; leader-targeted DoS disrupts HotStuff-like liveness within seconds, cost independent of committee size (S16 AsiaCCS2024, PV as applied to Tendermint-style proposers; spike §9 connection limits remain at FST network layer). | Moderate — leader DoS mitigation is engineering work, not yet specified |
| Long-range / nothing-at-stake | N/A to CometBFT core | Not applicable to classical BFT over a validator set (unlike PoS checkpoint finality). Relevant only to the external FST staking layer design (D-016). | Depends on staking design (OPEN RESEARCH) |
| Compromised implementations / supply chain | T-013 | Single CometBFT implementation dependency; Apache-2.0 fork right mitigates single-vendor risk (P10); no test vectors/conformance harness yet (RS-012/D-035 OPEN RESEARCH). | Moderate — mitigated by fork right; unmitigated by conformance tooling |

### 5.3 Security advisories and patch status (from feasibility spike §2)

| Advisory | ID | Severity | Published | Patched in | Included in v0.40.0? |
|----------|-----|----------|-----------|------------|----------------------|
| CSA-2026-001: Tachyon | GHSA-c32p-wcqj-j677 | Critical | 2026-01-23 | v0.38.21, v0.37.18 | Yes (inference basis — see limitation below) |
| ASA-2025-003 | GHSA-hrhf-2vcr-ghch | High | 2025-10-14 | v0.38.19 | Yes |
| ASA-2025-002 | GHSA-r3r4-g7hq-pq4f | High | 2025-02-03 | v0.38.17 | Yes |
| ASA-2025-001 | GHSA-22qq-3xwm-r5x4 | Low→Informational | 2025-02-03 | v0.38.17 | Yes |
| ASA-2024-011 | GHSA-p7mv-53f2-4cwj | High | 2024-11-06 | v0.38.15 | Yes |
| ASA-2024-009 | GHSA-g5xx-c4hv-9ccc | Moderate | 2024-09-03 | v0.38.12 | Yes |
| ASA-2024-008 | GHSA-hg58-rf2h-6rr7 | Moderate→Informational | 2024-06-27 | v0.38.8 | Yes |
| ASA-2024-001 | GHSA-qr8r-m495-7hc4 | High | 2024-01-17 | v0.38.3 | Yes |

**Evidence limitation (honest recording):** the spike concludes v0.40.0 includes the CSA-2026-001 fix and all other listed patches based on release timeline plus the advisory's statement that "the main unreleased branch is also patched" (patch completed 2026-01-12, published 2026-01-23; v0.40.0 released 2026-07-27 from the patched main branch, ~6.5 months after the fix). This is an inference, **not** re-verified by direct source inspection of the v0.40.0 tree (spike §2). This must be re-verified during the executable feasibility phase when the Go toolchain is available.

### 5.4 Organisational control (P1/P2)

- CometBFT is maintained by a single organisation (Cosmos Labs, wholly-owned subsidiary of the Interchain Foundation, Swiss nonprofit) (spike §1, §10).
- Using it as a dependency does not by itself give that organisation unilateral control over FST: FST owns genesis, staking, transactions, fees, and upgrades; CometBFT only orders transactions. Apache-2.0 plus separate-process deployment means FST can fork CometBFT or replace the consensus engine (ADR-0004 fallback: HotStuff/Casper/Narwhal).
- Documented as a managed dependency risk, not a P1/P2 violation (spike §10).

---

## 6. Performance and scalability evidence (from local sources only)

All evidence below is qualitative or repository-recorded; no deployment measurements are claimed by this investigation (RS-013 §8.1 qualification).

| Evidence item | Content | Source | Verification status |
|---------------|---------|--------|---------------------|
| CometBFT throughput claim | "up to 10k TPS" — treated as indicative, not a guarantee; real BFT throughput depends on topology, hardware, transaction size, signature scheme | Feasibility spike §9 | Recorded claim; not independently measured |
| Communication complexity | PBFT-style voting communication per block; exact per-block message count in CometBFT's current implementation requires spec inspection and is not stated as a verified deployment measurement (RS-013 §5.2, §8.1; reference #10 PV) | RS-013 | PARTIALLY VERIFIED (paper-level; no deployment measurement) |
| C++ test results | 343/343 tests pass under GCC Debug, GCC Release, GCC ASan/UBSan, Clang Release with -Werror, and ThreadSanitizer | Root README.md "Test results (verified)" | Repository-recorded claim; covers FST C++ side, not CometBFT performance |
| CometBFT integration tests | 3 tests pass against actual CometBFT v0.40.0: socket connection + Echo; single-node genesis + commit; 4-node genesis agreement | Root README.md "Real multi-validator CometBFT integration" | Repository-recorded claim; **not re-verified in this session** (Go toolchain unavailable); Byzantine fault testing, partition/recovery testing, and cross-node transaction flow with UTXO conservation verification remain outstanding (README disclosures) |
| Build environment | GCC 16.2.1, Clang 23.1.5, CMake 4.5.0, OpenSSL 3.x, SQLite3, Boost present; Go NOT installed (`go: command not found`) | Feasibility spike §9–10; CMakePresets.json | Observed in this environment (2026-11-18) |
| Build presets | debug, release, asan, clang-release, tsan (Ninja, jobs 2) | CMakePresets.json | Verified against file (2026-11-18) |

**Performance gaps:** no fixed per-transaction bandwidth or latency figures are assigned for CometBFT without source-specific validation (RS-013 §8.1–8.3); the ≥4-validator multi-validator acceptance evidence required by M-03 is not yet produced; no Byzantine fault/partition/recovery performance data exists locally.

---

## 7. Maintenance, maturity and release information

All from the feasibility spike (fetched/recorded 2026-11-17; accessed 2026-11-18).

| Attribute | Value |
|-----------|-------|
| Latest stable release | v0.40.0 |
| Release date | 2026-07-27T21:19:49Z |
| Release commit | 0880b4d (GPG-signed; committer signature verified; key 3BFB0FD2F8A4E6BC) |
| Commits ahead on `main` | 43 (unreleased; explicitly NOT a production dependency per CometBFT policy) |
| go.mod `go` directive | 1.25.0 |
| License | Apache-2.0 |
| Maintainer | Cosmos Labs (wholly-owned subsidiary of Interchain Foundation, Swiss nonprofit) |
| Protocol | Fork of Tendermint Core; implements Tendermint BFT consensus (BFT SMR) |
| ABCI version | ABCI 2.0 / ABCI++ (since v0.38.0) |
| Distribution | Source; `cometbft` CLI; docker images `cometbft/cometbft` |
| Release policy | "Do not depend on main as your production branch. Use releases instead." PATCH bumps compatible with existing blockchain histories; MINOR bumps may be protocol-breaking and may require a new chain (per UPGRADING.md) |
| Conservative LTS alternative | v0.38.26 (latest v0.38.x patch; Go 1.22+) |
| Upgrade/migration | Documented in UPGRADING.md; no automatic mainnet migration tooling; FST owns its genesis/upgrade procedure |
| Security response | Advisories tracked via GitHub Advisory Database (GHSA-*); all listed advisories patched in releases preceding v0.40.0 |

Maturity assessment: CometBFT is an established, maintained, extensively tested BFT implementation with a defined release policy, GPG-signed releases, a documented security-advisory process, and an LTS line (v0.38.x). It is the most mature of the BFT candidates evaluated locally. Remaining maturity risks: single-org maintenance (mitigated by Apache-2.0 fork right), Go-toolchain operational footprint, and the absence of locally re-verified executable evidence (prototype blocked by missing Go).

---

## 8. Licensing and dependency assessment

### 8.1 Licensing

- CometBFT: Apache-2.0 — redistributable, minimal restrictions, no copyleft (feasibility spike §4).
- FST root LICENSE: MIT (root README.md §License).
- Combining Apache-2.0 CometBFT (separate process) with MIT-licensed FST C++ creates no copyleft obligation and no license conflict.

### 8.2 Dependency model

- FST runs CometBFT as a **separate process**, not a linked library, so there is no Go/C++ linking obligation (spike §4).
- CometBFT's Go module dependencies are CometBFT's concern, not FST's (spike §4).
- FST C++20 build dependencies: GCC 12+ or Clang 15+, CMake 3.25+, Ninja, OpenSSL 3.x, SQLite 3.24+, Boost 1.74+, Protobuf (root README §Requirements).
- CometBFT build dependency: Go 1.25.0 for v0.40.0 (go.mod directive); **Go is not installed in this environment** (`go: command not found`) — a prerequisite, not an architectural blocker (spike §10).

### 8.3 Supply-chain and fork rights

- Apache-2.0 permits independent forking if Cosmos Labs ceases maintenance — supports P10 (network survives disappearance of founding developers) (spike §4).
- Dependency lifecycle: every dependency must be recorded with version, license, upstream maintenance status, and known vulnerabilities; vulnerability response on a documented schedule (engineering charter §10). CometBFT v0.40.0 + Go 1.25.0 would be pinned; upgrades require the same research-and-gate process as new technology adoption.

---

## 9. C++ integration assessment

### 9.1 Interface (from feasibility spike §5 and ADR-0004 §Selected design)

- ABCI messages are protocol buffers (`proto/tendermint/abci/types.proto`); language-agnostic — any language can implement the ABCI application over the wire protocol.
- Transport: TCP socket, Unix-domain socket, or gRPC. Local Unix-domain socket is the preferred FST deployment (application interface not exposed publicly).
- `abci-cli` tool available for testing.
- CometBFT initiates all ABCI calls; the application responds; CometBFT drives the lifecycle.

### 9.2 ABCI++ (ABCI 2.0) methods relevant to FST

| Method | Role in FST |
|--------|-------------|
| `InitChain` | Genesis initialization; initial validator set and application state |
| `PrepareProposal` / `ProcessProposal` | Proposal construction/validation hints (application-deterministic) |
| `FinalizeBlock` | Apply transactions; update UTXO/staking state; compute rewards/fees; return `ValidatorUpdates` |
| `Commit` | Persist application state; return application hash (state commitment) |
| `ExtendVote` / `VerifyVoteExtension` | Vote extensions (optional; FST may disable) |
| Snapshot methods (`ListSnapshots`, `OfferSnapshot`, `LoadSnapshotChunk`, `ApplySnapshotChunk`) | State synchronization |
| `ListInfo` | Application info |

### 9.3 Integration locations and process model

- New C++ module `src/consensus/abci/` and `include/feinstat/consensus/abci/` — C++ ABCI application, Unix-domain socket client, determinism guards (ADR-0004 §Selected design).
- CometBFT process managed externally (test-network orchestrator); **not vendored into the C++ build** (ADR-0004).
- C++ project remains C++20; Go runtime is separate (feasibility spike §3).

### 9.4 Determinism and persistence requirements

- FST ABCI application is a pure function of (prior state, committed transactions): no non-deterministic external API call, wall-clock dependency, unordered iteration, floating-point calculation, or local filesystem input in consensus-critical paths (ADR-0004 determinism section).
- Application state: UTXO set (ADR-0002) + staking/validator state (ADR-0003) + monetary/fee state (ADR-0005), all deterministic.
- Committed application state persisted before `Commit` acknowledges; crash recovery must not allow partially committed financial state to become authoritative (atomic commit semantics) (ADR-0004).
- Application hash: reproducible state root over committed application state, returned in `Commit`, included in the block header; every correct node computes an identical hash (ADR-0004).
- Validator updates returned from `FinalizeBlock`; CometBFT applies them per activation-height semantics (change signed into commit at H+1, active at H+2); FST's staking module must emit validator updates consistent with this, and deterministic application state is the sole source of voting power (feasibility spike §6).

### 9.5 Integration risk summary

| Risk | Severity | Mitigation status |
|------|----------|-------------------|
| Go toolchain unavailable in this environment | High (evidence gap only) | Prerequisite; not an architectural blocker (spike §10) |
| Non-deterministic C++ application → state divergence/fork | High | Determinism guards required in `src/consensus/abci/`; determinism tests needed (ADR-0004 verification evidence #10) |
| Activation-height validator-update mishandling | Medium | Staking module (ADR-0003) must align with H+2 semantics; tests needed |
| ABCI socket exposed publicly | Medium | Filesystem permissions; local UDS only (ADR-0004 limitations; root README limitations) |
| Leader-targeted DoS on variable/unreliable links | Moderate | Connection limits/rate limiting at FST network layer; leader-protection design is Phase 3 implementation work (FST-REQ-0038) |

---

## 10. Future compatibility analysis

- **Version pinning:** CometBFT `main` is excluded (43 unreleased commits; not a production dependency per CometBFT policy). v0.40.0 is the pinned candidate; v0.38.26 is the conservative LTS alternative (Go 1.22).
- **Upgrade policy:** PATCH versions are compatible with existing blockchain histories; MINOR versions may be protocol-breaking and may require a new chain or custom migration (UPGRADING.md, spike §8). FST owns its genesis/upgrade procedure; no automatic mainnet migration tooling is provided by CometBFT.
- **ABCI++ stability:** ABCI 2.0 / ABCI++ is stable since v0.38.0; the ABCI application interface is the compatibility boundary between CometBFT and the FST application.
- **State sync and light clients:** ABCI snapshot methods plus the `light` package provide state-sync and light-client verification paths for future light-node support (spike §7; RS-009/M-14 deferred).
- **Transport:** default p2p is the legacy transport; v0.40.0 libp2p is experimental and must not be enabled for consensus networking (spike §9). Future multi-transport work (D-019) sits in the FST P2P layer, not the CometBFT consensus dependency.
- **Fallback path:** If material incompatibilities are found, ADR-0004 requires documenting evidence and a proposed alternative rather than silently switching consensus families. Documented fallbacks: HotStuff/C1, Gasper/C5, Narwhal/Tusk/C6 (all OPEN RESEARCH).
- **Economic parameters:** numerical monetary policy (D-015 M-01/M-11) and validator economics (D-016 M-02) remain BLOCKED-ON-OWNER and do not affect the CometBFT integration decision; they are application-layer (ADR-0004 problem statement).

---

## 11. Known weaknesses and unresolved questions

1. **No executable prototype in this environment.** Go 1.25.0 is not installed; the minimal working prototype (one CometBFT process + one C++ FST ABCI application over a Unix-domain socket) cannot be built or executed here (feasibility spike §10). Evidence gap for M-03's executable-evidence expectation.
2. **CSA-2026-001 inclusion in v0.40.0 is by inference**, not re-verified by direct inspection of the v0.40.0 tree (feasibility spike §2). Re-verification required when the Go toolchain is available.
3. **Repository-recorded CometBFT test claims not re-verified.** The root README records 3 tests pass against actual CometBFT v0.40.0 (socket connection + Echo; single-node genesis + commit; 4-node genesis agreement). Not re-verified in this session (no Go toolchain); Byzantine fault testing, partition/recovery testing, and cross-node transaction flow with UTXO conservation verification remain outstanding (README disclosures).
4. **Determinism of the C++ ABCI application not demonstrated** in this session; determinism tests, crash-recovery tests, and activation-height validator-update tests are required (ADR-0004 verification evidence).
5. **Leader-based DoS surface.** Per-slot proposer is a leader DoS target; leader-targeted DoS disrupts HotStuff-like liveness within seconds (S16, PV as applied to Tendermint-style leaders). Mitigation is engineering work (FST-REQ-0038).
6. **Stake concentration.** The external FST staking layer must keep Byzantine voting power below `f_bft`; concentration (whales, custodial staking, liquid-staking derivatives) breaks the assumption (candidate-c5-evaluation §1.5 patterns applied to stake-weighted BFT; RS-013 §9.2). Staking economics (D-015/D-016 parameters) are BLOCKED-ON-OWNER.
7. **Economic circularity.** Security budget depends on token value; token value may depend on perceived security — unresolved for a new network (RS-013 §7.14; D-015 OPEN RESEARCH).
8. **Single-org maintenance risk.** CometBFT is maintained by one organisation (Cosmos Labs/Interchain Foundation); managed as a dependency risk with Apache-2.0 fork right (spike §10).
9. **No automatic migration tooling.** CometBFT provides none; FST owns genesis/upgrade procedure (spike §8).
10. **Throughput claim indicative only.** "up to 10k TPS" is a marketing claim, not a guarantee (spike §9); no local deployment measurements exist.
11. **Offline operation.** No consensus protocol (CometBFT included) provides offline finality; bounded offline spending requires a separate offline-payment architecture (D-018 PROVISIONAL; RS-013 §3.13). Shared limitation of all candidates.

---

## 12. Recommendation with explicit rationale

**Recommendation:** Adopt CometBFT v0.40.0 as the FST consensus engine candidate under D-017/ADR-0004, subject to the following conditions before it becomes a committed production dependency:

1. **Complete the feasibility spike's executable evidence** once Go 1.25.0 is available: initialize CometBFT v0.40.0 genesis; run one CometBFT process; connect the C++ FST ABCI application over a local Unix-domain socket; exercise `InitChain` → transaction submission → `FinalizeBlock` → `Commit` → `app_hash`; persistence/restart recovery; then expand to a ≥4-validator isolated multi-node test network with Byzantine fault injection, determinism tests, crash-recovery tests, and activation-height validator-update tests (feasibility spike §10; ADR-0004 verification evidence).
2. **Pass the fst-verification gate** (exact commit SHA, build preset, command, exit code, log location) before production commitment (engineering charter §7; M-03 closure condition).
3. **Obtain an owner decision** via the proposed ADR (Section 16). The Research Agent does not approve its own recommendation; no research result automatically becomes an approved protocol decision (research-status §Research-before-implementation obligations).

**Rationale (explicit):**
- D-017 is APPROVED with CometBFT/Tendermint as the preferred candidate; ADR-0004 is ACCEPTED and mandates this feasibility evaluation before production commitment.
- CometBFT is the most mature and most completely evidenced BFT candidate available locally: established release policy, GPG-signed v0.40.0, all listed security advisories patched (v0.40.0 inclusion by inference), Apache-2.0, ABCI++ stable interface, state sync/snapshots/light-client support, documented activation-height semantics.
- The separate-process ABCI-over-UDS integration model keeps CometBFT out of the C++ build, preserves the C++20 codebase, and keeps the ABCI interface off the public network — consistent with ADR-0004 and the repository README architecture.
- No material architectural incompatibility was identified locally (feasibility spike §10).
- Rejected alternatives are either incompatible with D-017 (Nakamoto PoW probabilistic finality; Raft crash-fault only) or OPEN RESEARCH fallbacks with less local evidence (HotStuff, Gasper, Narwhal/Tusk). Switching away from CometBFT without material incompatibility evidence would violate the "do not silently switch consensus families" rule in ADR-0004.

**CometBFT remains an approved candidate, not a committed production dependency, until conditions 1–3 above are satisfied.**

---

## 13. Rejected alternatives and reasons

| Alternative | Reason rejected | Local source |
|-------------|----------------|--------------|
| Nakamoto PoW / C3 | Probabilistic finality conflicts with D-017's deterministic-finality direction; high energy cost; mining-pool centralisation; slow settlement; 51% rental-attack economics for smaller networks | ADR-0004 alternatives table; RS-013 §5.3, §7.6, §18.3 |
| FeinStatic Raft / D-024 | Crash-fault tolerant only, not Byzantine; permissioned; conflicts with P9 and D-017; cannot be incrementally extended to FST requirements | ADR-0004 alternatives table; RS-013 §1 |
| HotStuff / C1 | OPEN RESEARCH; less mature production deployment; more integration effort; kept as documented fallback | ADR-0004 alternatives table; RS-013 §5.2, §18.1 |
| Gasper / C5 | OPEN RESEARCH; weak-subjectivity trust conflicts with P9; stake concentration/LST centralisation challenge P1/P4; economic circularity (D-015 unresolved); long-range attacks; nothing-at-stake | ADR-0004 alternatives table; RS-013 §5.5, §18.5; candidate-c5-evaluation.md |
| Narwhal/Tusk / C6 | OPEN RESEARCH; no intrinsic Sybil resistance; no unified security proof for the composition; less deployment history; probabilistic Tusk termination; D-019 PROVISIONAL and deferred | ADR-0004 alternatives table; RS-013 §5.6, §18.6; candidate-c6-evaluation.md; candidate-c5-c6-comparative-assessment.md |
| CometBFT `main` branch | Unreleased (43 commits ahead); explicitly not a production dependency per CometBFT release policy | Feasibility spike §1, §9 |

---

## 14. Prototype and reproducible benchmark plan

**Prerequisite (currently unmet):** Go 1.25.0 toolchain. Not installed in this environment (`go: command not found`). This is a prerequisite, not an architectural blocker; all C++-side work (ABCI client, determinism guards, UTXO/staking/monetary modules) can proceed without Go (feasibility spike §10).

**Prototype plan (execute when Go 1.25.0 is available):**
1. Install Go 1.25.0; fetch CometBFT v0.40.0 source (tag 0880b4d, GPG-verified).
2. Initialize CometBFT v0.40.0 genesis for a single validator.
3. Run one CometBFT process; connect the C++ FST ABCI application over a local Unix-domain socket using ABCI++.
4. Exercise the lifecycle: `InitChain` → transaction submission → `FinalizeBlock` → `Commit` → verify `app_hash`; confirm deterministic application state (identical state root on identical inputs across nodes).
5. Verify persistence/restart recovery: committed state persisted before `Commit` acknowledges; no partially committed financial state becomes authoritative after crash.
6. Expand to an isolated ≥4-validator multi-node test network; verify 4-node genesis agreement; perform Byzantine fault injection (equivocation, vote withholding), partition and recovery, and activation-height validator-set update tests.
7. Record all results in `docs/fst/operations/verification-status.md` with exact commit SHA, build preset, command, exit code, and log location (Verification Agent gate).

**Reproducible benchmark plan:**
- Benchmark only against actual CometBFT v0.40.0 builds; no synthetic or invented throughput numbers.
- Metrics: block finalization latency, transaction throughput (transactions/block), validator-set change overhead (activation-height transitions), state-sync snapshot/restore time, `app_hash` computation time.
- Conditions documented: hardware, topology, transaction size, signature scheme, network delay. The "up to 10k TPS" claim is treated as indicative only (feasibility spike §9); results are recorded as measured, not compared against the marketing claim unless independently measured.
- All benchmark evidence logged with commit SHA and environment details for the Verification Agent.

---

## 15. Sources, citations, URLs, and verification dates

All sources are existing local FST documents; no external URLs were fetched for this investigation. URLs cited below appear within local documents and are recorded for traceability only.

| # | Source (local file) | Title / content | Date | Verified / accessed | Relevance |
|---|---------------------|-----------------|------|---------------------|-----------|
| S1 | `docs/fst/architecture/cometbft-integration-feasibility-spike.md` | CometBFT Integration Feasibility Spike (FST-SPIKE-COMETBFT-001) | 2026-11-17 | Fetched 2026-11-17; accessed 2026-11-18 | Primary evidence: release v0.40.0, security advisories, Go/licensing, ABCI++, validator-set updates, state sync, performance, risks, Go-toolchain prerequisite |
| S2 | `docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md` | ADR-0004 BFT consensus, finality and CometBFT integration (ACCEPTED) | 2026-11-17 | Accessed 2026-11-18 | Approved architecture; determinism; activation-height; integration locations; security assumptions; alternatives |
| S3 | `docs/fst/decisions/decision-register.md` | FST Decision Register (D-001–D-037) | — | Accessed 2026-11-18 | D-017 APPROVED; D-016 APPROVED; D-037 APPROVED; D-015 APPROVED; D-036 APPROVED |
| S4 | `docs/fst/scope/scope-v0.1.md` | FST Project Scope v0.1 (APPROVED; Current-Scope Amendment 2026-11-17) | 2026-11-17 (amendment) | Accessed 2026-11-18 | P1–P14; D-018 PROVISIONAL; D-019 PROVISIONAL; offline operation; D-036 fork protection; out-of-scope items |
| S5 | `docs/fst/engineering-charter.md` | FST Engineering Charter (APPROVED) | — | Accessed 2026-11-18 | Research-first technology selection (§7); dependency lifecycle (§10); production-readiness (§17); role definitions |
| S6 | `docs/fst/decisions/decision-closure-roadmap.md` | FST Decision Closure Roadmap | — | Accessed 2026-11-18 | M-03 DEFERRED-BY-DESIGN (CometBFT feasibility); closure conditions; research-agent duties |
| S7 | `docs/fst/operations/project-plan.md` | FST Project Plan (v1.0, 2026-11-16) | 2026-11-16 | Accessed 2026-11-18 | Phase 3/4 unblocked; FST-REQ-0031–0039; M-03 target phase |
| S8 | `docs/fst/research/consensus-validator-research.md` | RS-013 Consensus and Validator Research (OPEN RESEARCH) | Verification date 2026-11-15 | Accessed 2026-11-18 | C1–C7 compositions; comparison matrix; CometBFT/Tendermint §5.2; adversary model; implementation candidates |
| S9 | `docs/fst/research/candidate-c5-evaluation.md` | Candidate C5 Evaluation — PoS + Separate Finality Gadget (Gasper-style) | — | Accessed 2026-11-18 | Gasper weaknesses; weak subjectivity (P9); long-range attacks |
| S10 | `docs/fst/research/candidate-c6-evaluation.md` | Candidate C6 Evaluation — DAG-based Dissemination + Separate Agreement/Finality (Narwhal/Tusk-style) | — | Accessed 2026-11-18 | Narwhal/Tusk properties; no intrinsic Sybil resistance |
| S11 | `docs/fst/research/candidate-c5-c6-comparative-assessment.md` | Comparative Assessment — C5 vs C6 (OPEN RESEARCH) | — | Accessed 2026-11-18 | D-019/D-018 comparison; neither categorically stronger |
| S12 | `docs/fst/research/adversary-model-v0.1.md` | FST Adversary Model v0.1 (DRAFT, OPEN RESEARCH) | — | Accessed 2026-11-18 | Threat classes T-01–T-13; fault-bound parameters f_bft/q_finality/h_stake/h_pow/f_async; residual risks |
| S13 | `docs/fst/security/threat-model-v0.1.md` | FST Network Threat Model v0.1 (DRAFT, PROVISIONAL) | — | Accessed 2026-11-18 | T-011 Byzantine participants HIGH; implemented vs planned mitigations |
| S14 | `docs/fst/research/research-register.md` | FST Research Register (RS-001–RS-015) | — | Accessed 2026-11-18 | RS-013/RS-002 status; research-entry requirements |
| S15 | `docs/fst/research/README.md` | FST Research-First Engineering (APPROVED methodology) | — | Accessed 2026-11-18 | 12-step research-first methodology; entry requirements |
| S16 | `docs/fst/adr/adr-template.md` | ADR Template | — | Accessed 2026-11-18 | Basis for proposed ADR outline (Section 16) |
| S17 | Root `README.md` | FST repository README (architecture diagram, project status, test results) | — | Accessed 2026-11-18 | CometBFT v0.40.0 ABCI integration diagram; 343/343 test claim; 3 CometBFT integration test claim (recorded claims, not re-verified — no Go toolchain); build requirements |
| S18 | `CMakePresets.json` | CMake build presets | — | Accessed 2026-11-18 | debug/release/asan/clang-release/tsan presets |

**External URLs cited within local sources (not fetched; recorded for traceability):**
- `github.com/cometbft/cometbft/releases/latest` — CometBFT v0.40.0 release (spike §1)
- `docs.cometbft.com` — CometBFT documentation (ADR-0004 existing research)
- `pkg.go.dev` — CometBFT license/maintainer (spike §1)
- `arxiv.org/abs/2003.03052` — Gasper (Buterin et al. 2020) (RS-013 ref #15)
- `arxiv.org/abs/1710.09437` — Casper FFG (Buterin, Griffith 2017) (RS-013 ref #14)
- `doi:10.1145/3492321.3519594` — Narwhal/Tusk (EuroSys 2022) (RS-013 ref #11)
- `doi:10.1145/3293611.3331591` — HotStuff (PODC 2019) (RS-013 ref #8)
- `doi:10.1145/3149.214121` — FLP (Fischer, Lynch, Paterson 1985) (RS-013 ref #2)
- `doi:10.1145/42282.42283` — DLS (Dwork, Lynch, Stockmeyer 1988) (RS-013 ref #3)
- `doi:10.1007/3-540-45748-8_24` — The Sybil Attack (Douceur 2002) (RS-013 ref #5)
- `GHSA-c32p-wcqj-j677` — CSA-2026-001 Tachyon advisory (spike §2)

---

## 16. Proposed ADR outline (owner decision required)

**Proposed ADR ID:** ADR-0006 (next available number; to be assigned on registration) — "CometBFT v0.40.0 as committed consensus engine dependency"

**Status:** PROPOSED (this report); **Classification:** APPROVED (architecture) — owner decision required to convert candidate to committed production dependency.

**Outline (per `docs/fst/adr/adr-template.md`):**

1. **Metadata** — ID, Title, Date, Status (PROPOSED), Classification (APPROVED pending owner decision), Author (fst-research), Reviewed by (pending), Supersedes/Superseded by (none).
2. **Problem statement** — FST requires stake-weighted BFT consensus with deterministic finality (D-017, APPROVED); an established, maintained implementation is preferred over writing a new BFT algorithm; CometBFT is the preferred candidate subject to feasibility evaluation (ADR-0004). This ADR records the owner decision to commit CometBFT v0.40.0 as the production consensus engine, conditional on feasibility evidence.
3. **Existing research and references** — cite S1–S18 above; ADR-0004; feasibility spike FST-SPIKE-COMETBFT-001; RS-013 references #9–#10; adversary model C2 row.
4. **Alternatives evaluated** — summary of Section 13 comparison matrix: HotStuff/C1 (OPEN RESEARCH fallback), Gasper/C5 (OPEN RESEARCH fallback), Narwhal/Tusk/C6 (OPEN RESEARCH fallback), Nakamoto PoW/C3 (not viable), Raft/D-024 (reference only), CometBFT `main` (excluded).
5. **Reasoning** — why CometBFT v0.40.0 (maturity, release policy, security patch status, Apache-2.0, ABCI++ stability, separate-process model) and why conditions are required (Go toolchain, executable prototype, ≥4-validator evidence, verification gate, owner decision).
6. **Selected design** — CometBFT v0.40.0 (pinned release, not `main`) as separate consensus process per validator; C++ FST ABCI application over local Unix-domain socket (ABCI++); ABCI methods implemented (InitChain, PrepareProposal/ProcessProposal, FinalizeBlock, Commit, ExtendVote/VerifyVoteExtension optional, snapshot methods, ListInfo); determinism requirements (ADR-0004); activation-height semantics (H+2); validator updates from deterministic FST application state only; integration locations `src/consensus/abci/` + `include/feinstat/consensus/abci/`; CometBFT not vendored; Go 1.25.0 toolchain requirement documented.
7. **Security assumptions** — <1/3 Byzantine voting power; partial synchrony for liveness; safety without synchrony; FLP/CAP boundaries; no weak-subjectivity trust; leader DoS surface; determinism guards; socket filesystem permissions; application-layer ownership of monetary policy/staking/fees.
8. **Limitations and trade-offs** — Go runtime/operational overhead; separate-process deployment; single-org maintenance risk (mitigated by Apache-2.0 fork right); MINOR version bumps may be protocol-breaking (may require new chain or custom migration); FST owns genesis/upgrade procedure; 10k TPS claim indicative only.
9. **Verification evidence** — feasibility spike documentation complete; executable prototype and ≥4-validator multi-node evidence PENDING (Go-toolchain prerequisite); determinism/crash-recovery/activation-height tests PENDING; fst-verification gate PENDING. Cite root README-recorded claims with the caveat they are not re-verified in this session.
10. **Cross-references** — decision-register.md (D-017, D-016, D-037, D-015, D-036); adr-0004; adr-0003; adr-0002; adr-0005; research-register.md (RS-013, RS-002); decision-closure-roadmap.md (M-03); project-plan.md (Phase 3 FST-REQ-0031–0039).

**Closure condition for owner decision:** feasibility evidence (prototype + ≥4-validator multi-node acceptance + verification gate) accepted; then CometBFT v0.40.0 becomes a committed production dependency; until then it remains an approved candidate only.

---

## 17. Investigation record (ledger updates)

This investigation is recorded in `docs/fst/operations/research-status.md` (the fst-research-owned ledger):

- Technology candidates under evaluation: CometBFT v0.40.0 added (status: under evaluation; feasibility spike complete; executable prototype blocked by missing Go 1.25.0 toolchain).
- Last research deliverable: this report (`docs/fst/research/pilot-cometbft-consensus-candidate-2026-11-18.md`).
- Proposed ADRs awaiting owner decision: ADR-0006 outline (CometBFT v0.40.0 as committed consensus engine dependency).
- RQ-02 (CometBFT feasibility, security and real multi-validator acceptance evidence, D-017/M-03): research-gate portion complete via this report; prototype/acceptance evidence remains blocked on Go 1.25.0 availability; fst-verification gate pending.
- RQ-01 (pilot: validate research deliverable format and sourced reporting): COMPLETE via this report.

No other ledgers are modified by this investigation (read-only for implementation-status.md, verification-status.md, defect-register.md, integration-status.md, decision-register.md, accepted ADRs). No production C++, tests, or CI are changed.

**Exact commit SHA:** reported after commit of this report and the research-status.md update (see Section 18 / tool result).