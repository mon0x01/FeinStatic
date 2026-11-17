# FST Project Plan — Canonical Scope and Ordered Implementation Plan

**Status:** ESTABLISHED — reconciled from approved scope, principles, decisions, research, architecture, and current implementation.

**Version:** 1.0
**Date:** 2026-11-16
**Classification:** APPROVED (scope and principles); PROVISIONAL (phase ordering and requirement definitions requiring validation); OPEN RESEARCH (requirements depending on unresolved decisions).

This document is the single source of truth for FST scope and implementation order. It reconciles:
- Approved FST project scope (`docs/fst/scope/scope-v0.1.md`)
- Approved engineering principles P1–P14 (`docs/fst/principles/engineering-principles.md`)
- Decision register classifications (`docs/fst/decisions/decision-register.md`)
- Development roadmap (`docs/fst/roadmap/development-roadmap.md`)
- Architecture specifications and ADRs (`docs/fst/architecture/`, `docs/fst/adr/`)
- Research register and dependency assessment (`docs/fst/research/research-register.md`)
- Actual current implementation and unfinished milestones (Git HEAD `8a1465c`)

## Phase Overview

| Phase | ID | Title | Status | Purpose |
|-------|----|-------|--------|---------|
| 0 | FST-PHASE-00 | Project Foundation & Documentation | COMPLETE | Establish FST identity, principles, decisions, research methodology, FeinStatic baseline |
| 1 | FST-PHASE-01 | Specification & Foundational Research | IN_PROGRESS | Protocol specification, consensus/Sybil/transaction research, adversary model, node architecture |
| 2 | FST-PHASE-02 | P2P Networking Foundation | IN_PROGRESS | Node identity, peer discovery, secure sessions, message dispatch, peer admission, bootstrap, peer exchange |
| 3 | FST-PHASE-03 | Consensus & Validator Architecture | IN_PROGRESS | Permissionless consensus, Sybil resistance, validator entry/exit, finality — owner decisions D-015/D-016/D-017/D-037 now APPROVED via ADRs; CometBFT feasibility spike in progress |
| 4 | FST-PHASE-04 | Transaction & State Layer | IN_PROGRESS | Transaction model, mempool, state transitions, fee market — owner decision D-037 APPROVED via ADR-0002; UTXO foundation next |
| 5 | FST-PHASE-05 | Financial Functionality | DEFERRED | Accounts, payments, settlement, exchange, merchant/institutional — incremental after core network |
| 6 | FST-PHASE-06 | Privacy & Offline Operation | DEFERRED | Transaction confidentiality, selective disclosure, bounded offline spending — requires research |
| 7 | FST-PHASE-07 | Multi-Transport & Resilience | DEFERRED | Mesh, radio, satellite, DTN — incremental after internet-connected test network |
| 8 | FST-PHASE-08 | Governance, Upgrades & Fork Safety | DEFERRED | Upgrade signaling, participant veto, fork protection implementation, migration |
| 9 | FST-PHASE-09 | Independent Implementations & Conformance | DEFERRED | Test vectors, conformance harness, independent client count decision |
| 10 | FST-PHASE-10 | Production Hardening & Mainnet Readiness | DEFERRED | Security audits, formal verification, operational tooling, monitoring |

**Ordering constraint:** Phases 0–2 complete and verified. Phases 3–4 are now UNBLOCKED (owner decisions D-015, D-016, D-017, D-037 APPROVED via ADRs). Phases 5–10 remain DEFERRED until core network is operational.

---

## Track B — Repository Quality and Maintenance

**Status:** PERMANENT — ongoing workstream running alongside Track A — FST Network Engineering.

Track A (the FST engineering phases above) builds the network. Track B keeps the repository clean, accurate, safe, and verifiable throughout development. Track B is not a one-time cleanup; it is a continuous responsibility reassessed during normal continuation and safe-stopping checkpoints.

Track B responsibilities are kept separate from the original 74 FST requirements (FST-REQ-0001 through FST-REQ-0074). Track B does not modify, reuse, or renumber those requirement IDs.

### B1. Repository cleanup

Continuously identify obsolete, unused, redundant, superseded, or out-of-scope files and directories. Actually remove items when their irrelevance has been conclusively established; do not merely report them.

Before any removal, inspect references and dependencies across source code, headers, CMake, tests, scripts, CI, packaging, installation rules, and documentation. Preserve important source code, tests, approved ADRs, research, specifications, security material, and operational history. Verify relevant builds, tests, and references after removal.

Never use `git add .` or `git add -A`; stage only explicitly reviewed paths.

### B2. Directory structure

Identify and simplify unnecessary directory nesting, especially one-child directory chains that provide no meaningful architectural or organizational value.

Check include paths, namespaces, CMake, tests, scripts, packaging, installation rules, and documentation before restructuring. Preserve meaningful architectural boundaries and avoid unnecessary compatibility breaks.

### B3. Public root README

Keep the root `README.md` concise, professional, and accurate to what FST currently implements.

Remove obsolete architecture descriptions, excessive version-by-version development histories, redundant explanations, and the generic publishing section. Describe implemented, tested, integrated, and planned capabilities accurately. Include useful architecture, technology, build, testing, and documentation information.

Do not claim unsupported functionality, security audits, decentralization guarantees, or production/mainnet readiness. Update the README whenever material changes affect project capabilities or status. Do not disclose private development workflows or AI-agent instructions.

### B4. Local-only files and GitHub publication protection

The root `AGENTS.md` is a local-only operational instruction file. It must
remain available in the owner's local working environment but must never
be newly tracked, committed, or published to GitHub.

The root `.gitignore` must permanently exclude `/AGENTS.md`.

During every repository cleanup, README review, Git status inspection,
staging operation, commit, push, branch integration and release
preparation:

1. Check whether `AGENTS.md` is tracked or staged.
2. Verify that the root ignore rule remains effective.
3. Preserve the local file and its contents.
4. Prevent it from entering new commits.
5. Audit unpublished history when necessary to identify historical copies.
6. Check protected session exports and other local-only material before
   publication.
7. Verify the exact staged file list before every commit and push.

Never assume that `.gitignore` protects an already tracked file.
Explicitly inspect Git's index. An ignore rule only affects untracked
files; a file already in Git's index must be removed with
`git rm --cached` to stop tracking it.

Also protect all `session-ses_*.md` files. Never read, modify, delete,
rename, stage, commit or publish them.

Apply the same checks to other local-only files, credentials, private
keys, environment files, backups, temporary artifacts and generated
files. Inspect their purpose and actual tracking state rather than
relying on filenames alone.

**Permanent enforcement:** This protection applies automatically during
every future `continue` session, `stop` checkpoint, repository cleanup,
GitHub polishing, commit, push, merge and release. No additional owner
approval is required for routine enforcement or safe removal of local-
only files from Git tracking.

### B5. Git branch integration

Safely integrate the existing FST feature branch into `main` when it is ready.
The project owner has authorized autonomous local commits and GitHub pushes
whenever appropriate and necessary. Agent-led review and verification are
mandatory; separate user approval for routine publishing is not required.

Inspect local and remote branch states, divergence, working-tree changes,
and commit history first. Review changes, resolve conflicts carefully, and
run appropriate verification before integration. Use a history-preserving
merge and push only when safe.

Never force-push, rewrite history, use destructive resets, discard user
changes, or use broad staging commands such as `git add .` or `git add -A`.
Review every staged file before committing.

**Durable checkpoint publication flow** — at every appropriate durable checkpoint:

1. Complete a meaningful, logically coherent implementation unit.
2. Run the relevant tests and verification.
3. Update the appropriate project records.
4. Inspect `git status`, branch, upstream and remote state.
5. Review the complete changes and exact staged files.
6. Create a meaningful local commit.
7. Fetch and compare with the remote.
8. Push completed, verified commits to the corresponding GitHub branch using
   normal, history-preserving Git operations.
9. Verify that GitHub reflects the expected commit.

Do not wait for the owner to explicitly request each commit or push. If tests
fail, the remote has unexpected divergence, the working tree contains unrelated
changes that cannot safely be separated, or another genuine risk exists,
investigate and resolve it safely. Stop only when proceeding would be unsafe.
Report the specific blocker.

### B6. Verification and tracking

Verify relevant builds, tests, documentation links, references, and packaging after maintenance changes.

Record actual implementation progress in `implementation-status.md`. Record verification evidence in `verification-status.md`. Update `defect-register.md` only for genuine defects.

Keep repository-maintenance work separate from the original 74 FST requirements. Do not modify, reuse, or renumber those requirement IDs.

### B7. Continuous responsibility

Repository quality, cleanliness, documentation accuracy, and safe Git integration are permanent responsibilities throughout FST development, not one-time activities. Reassess them during normal continuation and safe-stopping checkpoints.

---

## Phase 0 — Project Foundation & Documentation (COMPLETE)

**Purpose:** Establish FST project identity, approved principles, decision classifications, research methodology, and FeinStatic technical baseline. No implementation.

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0001 | FST project scope documented | `scope-v0.1.md` exists, APPROVED, records FST identity, native currency, financial functionality, decentralisation, offline operation, communication, privacy, fork protection, out-of-scope items | None | None | APPROVED |
| FST-REQ-0002 | Engineering principles recorded | `engineering-principles.md` exists, APPROVED, P1–P14 listed with classifications | None | None | APPROVED |
| FST-REQ-0003 | Decision register established | `decision-register.md` exists, classifications APPROVED/PROVISIONAL/OPEN RESEARCH for D-001 through D-037 | FST-REQ-0001, FST-REQ-0002 | None | APPROVED |
| FST-REQ-0004 | FeinStatic baseline documented | `feinstat-baseline.md` exists, records subsystems, transformation findings, conflicts with FST principles | None | FeinStatic source audit | APPROVED |
| FST-REQ-0005 | Research methodology defined | `research/README.md` exists, APPROVED, 12-step process, entry requirements, domains table | None | None | APPROVED |
| FST-REQ-0006 | Research register populated | `research-register.md` exists, RS-001 through RS-015 tracked with status, sources, implications | FST-REQ-0005 | Academic/prior-art survey | APPROVED (register); OPEN RESEARCH (entries) |
| FST-REQ-0007 | Development roadmap recorded | `development-roadmap.md` exists, PROVISIONAL, milestones 1–3 + future themes | FST-REQ-0001 | None | PROVISIONAL |
| FST-REQ-0008 | ADR framework established | `adr/README.md` and `adr-template.md` exist, register empty, process documented | None | None | APPROVED |
| FST-REQ-0009 | AGENTS.md instructions written | Root `AGENTS.md` exists with project identity, foundational docs, principles, decisions, workflow, prohibited actions, preservation rules, FeinStatic relationship, escalation | FST-REQ-0001–FST-REQ-0008 | None | APPROVED |

**Entry conditions:** None (project inception).

**Exit conditions:** All 9 requirements IMPLEMENTED and documented. No code changes.

**Owner decisions required:** None — all APPROVED.

**Implementation order:** Sequential as listed.

---

## Phase 1 — Specification & Foundational Research (IN_PROGRESS)

**Purpose:** Produce versioned protocol specification, complete foundational research for consensus, Sybil resistance, transaction model, identity, discovery, privacy, monetary policy, offline operation, bridge; establish adversary model and node architecture.

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0010 | Core protocol specification v0.1 | `core-protocol-specification-v0.1.md` exists, PROVISIONAL, covers network identity, node identity, signed records, framing, versioning, handshake, capabilities, message categories, transaction/block dissemination boundaries, error handling, replay protection, upgrade signaling, transport extensibility; unresolved items explicitly marked | FST-REQ-0001, FST-REQ-0003, FST-REQ-0004 | RS-001–RS-014 | PROVISIONAL |
| FST-REQ-0011 | Consensus & validator research | `consensus-validator-research.md` (RS-013) exists, OPEN RESEARCH, nine-component taxonomy, seven compositions C1–C7, comparison matrix, source verification records, network/financial/monetary/transaction dependencies | FST-REQ-0006 | 30+ sources inspected | OPEN RESEARCH |
| FST-REQ-0012 | Sybil resistance research | `sybil-resistance-research.md` (RS-002) exists, OPEN RESEARCH, six concept distinctions, seven mechanisms, attack analysis, C1–C7 compositions, 60 sources | FST-REQ-0006 | 60 sources inspected | OPEN RESEARCH |
| FST-REQ-0013 | Transaction model research | `transaction-model-research.md` (RS-015) exists, OPEN RESEARCH, UTXO/account/eUTXO/hybrid comparison, two FST candidates, sensitivity analysis, fee market, replay protection, cross-cutting dependencies | FST-REQ-0006 | Bitcoin, Ethereum, Cardano, Nervos sources | OPEN RESEARCH |
| FST-REQ-0014 | Identity, peer discovery & P2P research | `identity-peer-discovery-research.md` (RS-014) exists, OPEN RESEARCH, seven-domain identity model, hybrid bootstrap+peer-exchange (Candidate 3 PROVISIONAL), Noise evaluation, transport adapters, threat matrix, FeinStatic reuse assessment | FST-REQ-0006 | libp2p, Noise, DTN RFCs | OPEN RESEARCH |
| FST-REQ-0015 | Adversary model v0.1 | `adversary-model-v0.1.md` exists, OPEN RESEARCH, 9 threat classes, parameterized thresholds (f_bft, q_finality, h_stake, h_pow, f_async), capability mapping, source register, 10 open questions | FST-REQ-0011, FST-REQ-0012 | RS-001, RS-002, RS-013, RS-015 | OPEN RESEARCH |
| FST-REQ-0016 | Permissionless test-network evaluation | `permissionless-test-network-evaluation.md` exists, OPEN RESEARCH, deep evaluation of C1/C3/C5, transaction-model sensitivity, requirements-level architecture, conditional recommendation, no decision | FST-REQ-0011, FST-REQ-0012, FST-REQ-0013 | RS-013, RS-002, RS-015 | OPEN RESEARCH |
| FST-REQ-0017 | Permissionless test-network decision brief | `permissionless-test-network-decision-brief.md` exists, OPEN RESEARCH, 6 decision briefs (transaction model, Sybil mechanism, consensus, finality, adversary assumptions, test-network boundaries), all unresolved | FST-REQ-0016 | RS-013, RS-002, RS-015 | OPEN RESEARCH |
| FST-REQ-0018 | Node roles & architecture | `node-roles.md` exists, PROVISIONAL, 6 roles (full, light, archival, validator-capable, bootstrap), responsibilities, data, storage, trust assumptions, synchronization modes, transitions | FST-REQ-0010, FST-REQ-0014 | RS-009, RS-013 | PROVISIONAL |
| FST-REQ-0019 | Network runtime capability matrix | `network-runtime-capability-matrix.md` exists, BASELINE, 59 capabilities classified (IMPLEMENTED/VERIFIED, PARTIALLY IMPLEMENTED, NOT IMPLEMENTED), diagnostic probes, test plan | FST-REQ-0010, current implementation | FeinStatic source | BASELINE |

**Entry conditions:** Phase 0 complete.

**Exit conditions:** All 10 requirements documented. Research entries remain OPEN RESEARCH — no implementation of consensus, transaction model, or validator mechanics.

**Owner decisions required:** D-015 (monetary policy), D-016 (Sybil mechanism), D-017 (consensus), D-018 (offline operation), D-019 (multi-transport), D-020 (privacy), D-035 (independent clients), D-037 (transaction model) — all remain OPEN RESEARCH or PROVISIONAL. No implementation permitted until decisions approved.

**Implementation order:** Research and specification in parallel; decision briefs after evaluation.

---

## Phase 2 — P2P Networking Foundation (IN_PROGRESS)

**Purpose:** Implement candidate-neutral P2P networking: node identity, signed node records, persistent peer store, bootstrap discovery, peer exchange, TLS 1.3 authenticated sessions, message dispatch, dynamic peer admission, network runtime integration. No consensus, transactions, or blocks.

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0020 | Node identity subsystem | `identity.hpp/cpp`: `NodeId` v1 format, `KeyProvider` abstraction, `Ed25519KeyProvider`, `FileKeyProvider` (wraps FeinStatic `NodeIdentity`), `RotationRecord`; domain-separated signing; tests pass | FST-REQ-0010, FST-REQ-0014 | RS-014 (identity model) | PROVISIONAL |
| FST-REQ-0021 | Signed node record | `node_record.hpp/cpp`: canonical serialization, 7 validation rules (version, key algo, network ID, node ID match, expiry, sequence, signature), strict bounds, security invariant, fork protection (D-036) | FST-REQ-0020 | RS-014, D-036 | PROVISIONAL |
| FST-REQ-0022 | Persistent peer store | `peer_store.hpp/cpp`: SQLite-backed, WAL mode, validated records only, monotonic sequence enforcement, duplicate/conflict handling, discovery source tracking, freshness timestamps, connection state, bounded storage, safe restart recovery | FST-REQ-0021 | RS-014, D-031 (SQLite reuse) | PROVISIONAL |
| FST-REQ-0023 | Bootstrap discovery | `bootstrap.hpp/cpp`: multiple configurable sources, record validation, wrong-network rejection, `BootstrapSource` transport-independent interface, no source is trust root (P1, P2) | FST-REQ-0022 | RS-014 | PROVISIONAL |
| FST-REQ-0024 | Peer exchange & authentication | `peer_exchange.hpp/cpp`: `PeerAuthenticator` interface, `StorePeerAuthenticator`, record validation, freshness, rate limiting, duplicate detection, conflict handling, bounded exchange, record selection; `PeerExchange` local logic | FST-REQ-0022, FST-REQ-0023 | RS-014 | PROVISIONAL |
| FST-REQ-0025 | TLS 1.3 authenticated sessions | `secure_session.hpp/cpp`: OpenSSL 3.x + Boost.Asio SSL, TLS 1.3 only, self-signed certs, mutual auth, Ed25519 binding over cert hash, session nonce, replay protection, forward secrecy | FST-REQ-0020 | RS-014, D-023 (crypto review) | PROVISIONAL |
| FST-REQ-0026 | Message dispatch & framing | `message_dispatch.hpp/cpp`: length-type-checksum-payload frames, `PeerMessageType` enum (baseline types), per-type size limits, global 1 MiB cap, handler registration, send/dispatch, error codes | FST-REQ-0010 | RS-014 | PROVISIONAL |
| FST-REQ-0027 | Dynamic peer admission | `peer_admission.hpp/cpp`: `AdmissionController` with connection limits (64/32/32/2), duplicate resolution, capped exponential backoff (250ms–30s), inbound/outbound counters, session lifecycle, `tick()` reconnection | FST-REQ-0025, FST-REQ-0026 | RS-014 | PROVISIONAL |
| FST-REQ-0028 | Network runtime integration | `network_runtime.hpp/cpp`: `NetworkRuntime` orchestrating identity load, TLS creds, peer store, bootstrap, peer exchange, admission, dispatcher, lifecycle (initialize/run/stop); config validation; error-code returns | FST-REQ-0020–FST-REQ-0027 | RS-014 | PROVISIONAL |
| FST-REQ-0029 | Multi-node test harness | `tests/network_harness.hpp/cpp/tests.cpp`: in-process multi-runtime harness, independent identities/DBs, loopback TLS sessions, ping/pong, app messages, disconnect, peer-record exchange, bootstrap auto-connect, partition/heal, shutdown/reinit cycles | FST-REQ-0028 | RS-014 | PROVISIONAL |
| FST-REQ-0030 | Capability matrix baseline | `network-runtime-capability-matrix.md` records 59 capabilities with classification, implementation location, tests, diagnostic probes, observed results, remaining gaps | FST-REQ-0028 | FeinStatic source audit | BASELINE |

**Entry conditions:** Phase 1 specification complete (FST-REQ-0010).

**Exit conditions:** All 11 requirements IMPLEMENTED and VERIFIED (tests pass under ASan/UBSan, debug, release). Multi-node end-to-end tests exercise: two runtimes connect, authenticated TLS session, ping/pong, application message round-trip, peer record exchange, bootstrap ingestion, reconnection with backoff, clean shutdown/restart.

**Owner decisions required:** None — all PROVISIONAL based on approved direction (D-016, D-019). Implementation proceeds as engineering validation of research direction.

**Implementation order:** 0020 → 0021 → 0022 → 0023 → 0024 → 0025 → 0026 → 0027 → 0028 → 0029 → 0030. Current Git state shows 0020–0028 partially implemented; 0029–0030 in progress (harness files created, matrix documented).

---

## Phase 3 — Consensus & Validator Architecture (BLOCKED)

**Purpose:** Implement permissionless Byzantine fault-tolerant consensus with Sybil resistance, validator entry/exit, finality, fork choice. Requires owner decisions on D-015, D-016, D-017, D-037.

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0031 | Consensus algorithm selection ADR | ADR recording selected consensus family (C1/C3/C5 or other), fault-bound parameter, finality model, safety/liveness assumptions, network assumptions, coherence with Sybil layer | FST-REQ-0011, FST-REQ-0012, FST-REQ-0013, FST-REQ-0016, FST-REQ-0017 | RS-013, RS-002 | APPROVED (owner decision required) |
| FST-REQ-0032 | Sybil-resistance mechanism ADR | ADR recording selected mechanism (PoW/PoS/PoSpace/hybrid), resource mapping, validator eligibility, voting power derivation, concentration safeguards, entry/exit mechanics | FST-REQ-0012, FST-REQ-0013, FST-REQ-0031 | RS-002, RS-013 | APPROVED (owner decision required) |
| FST-REQ-0033 | Transaction model ADR | ADR recording selected model (UTXO/account-based/eUTXO/hybrid), serialization format, nonce/chain-ID replay protection, fee policy, mempool semantics, state representation | FST-REQ-0013, FST-REQ-0031, FST-REQ-0032 | RS-015 | APPROVED (owner decision required) |
| FST-REQ-0034 | Validator identity & credentials | Implementation: validator key management, staking/bonding credentials (if PoS), registration, eligibility verification, rotation | FST-REQ-0020, FST-REQ-0031, FST-REQ-0032 | RS-013, RS-014 | BLOCKED on FST-REQ-0031, FST-REQ-0032 |
| FST-REQ-0035 | Consensus protocol implementation | Implementation: proposal, voting, quorum certificates, view changes (BFT) OR block production, fork choice, difficulty adjustment (PoW) OR LMD-GHOST + Casper FFG (Gasper) — per selected architecture | FST-REQ-0031, FST-REQ-0032, FST-REQ-0033 | RS-013 | BLOCKED on FST-REQ-0031 |
| FST-REQ-0036 | Finality & fork choice | Implementation: deterministic finality (BFT checkpoints, Casper FFG) OR probabilistic (k-depth, settlement delay) — per selected architecture; fork detection and recovery | FST-REQ-0035 | RS-013 | BLOCKED on FST-REQ-0031 |
| FST-REQ-0037 | Validator entry & exit | Implementation: permissionless registration, bonding/unbonding (PoS), slashing evidence, inactivity leak, committee rotation, epoch transitions | FST-REQ-0034, FST-REQ-0035 | RS-013 | BLOCKED on FST-REQ-0031, FST-REQ-0032 |
| FST-REQ-0038 | Consensus networking | Implementation: consensus message types, gossip vs direct delivery, validator peer sets, message authentication, rate limiting, DoS mitigation (leader protection) | FST-REQ-0026, FST-REQ-0028, FST-REQ-0035 | RS-013 | BLOCKED on FST-REQ-0031 |
| FST-REQ-0039 | Multi-node consensus test network | Test: ≥4 independent validator nodes reach Byzantine finality on test chain; fault injection (Byzantine validators, partitions, message loss); safety/liveness metrics recorded | FST-REQ-0035, FST-REQ-0036, FST-REQ-0037, FST-REQ-0038 | RS-013 | BLOCKED on FST-REQ-0031 |

**Entry conditions:** Owner approves FST-REQ-0031, FST-REQ-0032, FST-REQ-0033 via ADRs with APPROVED classification.

**Exit conditions:** All 9 requirements IMPLEMENTED and VERIFIED. Minimal permissionless test network operational with multiple independent validators reaching finality.

**Owner decisions required:** D-015 (monetary policy), D-016 (Sybil mechanism), D-017 (consensus), D-037 (transaction model) — all must be APPROVED before any implementation in this phase.

**Implementation order:** ADRs first (0031–0033), then implementation (0034–0039) in dependency order.

---

## Phase 4 — Transaction & State Layer (BLOCKED)

**Purpose:** Implement transaction processing, mempool, state transitions, fee market, block production/validation. Requires FST-REQ-0033 (transaction model ADR).

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0040 | Transaction serialization & validation | Implementation: serialization format (per FST-REQ-0033), syntactic/semantic validation, signature verification, nonce/UTXO checks, fee calculation, replay protection (chain-ID) | FST-REQ-0033 | RS-015 | BLOCKED on FST-REQ-0033 |
| FST-REQ-0041 | Mempool implementation | Implementation: bounded mempool (size/data limits), admission policy, eviction, replacement (RBF/nonce), conflict detection, persistence, restart recovery | FST-REQ-0040, FST-REQ-0022 | RS-015, FeinStatic mempool | BLOCKED on FST-REQ-0033 |
| FST-REQ-0042 | State transition & execution | Implementation: state representation (UTXO set / account trie), transaction execution, balance/nonce updates, fee collection, event emission, determinism | FST-REQ-0033, FST-REQ-0040 | RS-015 | BLOCKED on FST-REQ-0033 |
| FST-REQ-0043 | Block production & validation | Implementation: block format (per consensus), transaction inclusion, state root, header validation, block verification, reward/fee distribution | FST-REQ-0035, FST-REQ-0040, FST-REQ-0042 | RS-013, RS-015 | BLOCKED on FST-REQ-0031, FST-REQ-0033 |
| FST-REQ-0044 | Fee market & prioritization | Implementation: fee calculation (per FST-REQ-0033), dynamic fee estimation, prioritization policy, minimum fees, fee burning/distribution (per D-015) | FST-REQ-0040, FST-REQ-0015 | RS-015, D-015 | BLOCKED on FST-REQ-0033, D-015 |
| FST-REQ-0045 | Light-client state proofs | Implementation: state commitment scheme (Merkle/verkle), proof generation, verification, sync protocol, pruning | FST-REQ-0042 | RS-009 | DEFERRED (after core network) |

**Entry conditions:** FST-REQ-0033 APPROVED.

**Exit conditions:** All requirements IMPLEMENTED and VERIFIED. Transactions process end-to-end through consensus.

**Owner decisions required:** D-015 (monetary policy/fees), D-037 (transaction model).

**Implementation order:** 0040 → 0041 → 0042 → 0043 → 0044 → 0045.

---

## Phase 5 — Financial Functionality (DEFERRED)

**Purpose:** Implement financial accounts, payments, transfers, settlement, exchange, merchant/institutional transactions incrementally after core network operational.

**Requirements:** (High-level — detailed in future ADRs)
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0046 | Financial accounts & balances | Implementation: account creation, balance queries, reservations, multi-asset (FST only initially) | FST-REQ-0042 | FeinStatic SettlementBook | DEFERRED |
| FST-REQ-0047 | Payments & transfers | Implementation: P2P transfers, merchant payments, batch/institutional transfers, settlement finality | FST-REQ-0046 | FeinStatic Match/Flow | DEFERRED |
| FST-REQ-0048 | Settlement layer | Implementation: separate settlement from consensus, cross-network settlement, reconciliation | FST-REQ-0047 | FeinStatic SettlementBook | DEFERRED |
| FST-REQ-0049 | FST exchange with national currencies | Implementation: decentralised bridge design (non-custodial), independent infrastructure, compliance boundaries | FST-REQ-0048 | RS-008 | DEFERRED |
| FST-REQ-0050 | Merchant & institutional support | Implementation: invoices, recurring payments, wholesale transactions, reporting | FST-REQ-0047 | FeinStatic Match/Flow | DEFERRED |

**Entry conditions:** Phase 3–4 complete (core network + transaction layer operational).

**Exit conditions:** Financial functionality operational and verified.

**Owner decisions required:** D-008 (bridge design), D-018 (offline operation), RS-008 (bridge research).

**Implementation order:** Incremental per P13/P14 (2–3 features per release).

---

## Phase 6 — Privacy & Offline Operation (DEFERRED)

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0051 | Transaction confidentiality | Implementation: confidential transactions, amount hiding, selective disclosure | FST-REQ-0040 | RS-003, D-020 | DEFERRED |
| FST-REQ-0052 | Identity & selective disclosure | Implementation: anonymous credentials, selective disclosure proofs | FST-REQ-0051 | RS-003, RS-004 | DEFERRED |
| FST-REQ-0053 | Bounded offline spending | Implementation: local spending authority, reconciliation on reconnect, amount/risk-tiered finality | FST-REQ-0047 | RS-007, D-018 | DEFERRED |

**Entry conditions:** Phase 5 sufficiently advanced; research complete.

**Owner decisions required:** D-018, D-020, RS-003, RS-004, RS-007.

---

## Phase 7 — Multi-Transport & Resilience (DEFERRED)

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0054 | QUIC transport adapter | Implementation: QUIC transport implementing common interface, TLS 1.3, frame send/receive | FST-REQ-0028 | D-019, RS-005 | DEFERRED |
| FST-REQ-0055 | Mesh/radio/satellite adapters | Implementation: transport adapters for non-IP links, DTN store-and-forward | FST-REQ-0054 | D-019, RS-005, RFC 4838/9171 | DEFERRED |
| FST-REQ-0056 | Delay-tolerant consensus adaptation | Implementation: consensus protocol adaptation for intermittent connectivity | FST-REQ-0035, FST-REQ-0055 | RS-013 §8.10, RS-005 | DEFERRED |

**Entry conditions:** Internet-connected test network operational; research complete.

**Owner decisions required:** D-019, RS-005.

---

## Phase 8 — Governance, Upgrades & Fork Safety (DEFERRED)

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0057 | Upgrade signaling mechanism | Implementation: version bits/capability strings/dedicated message; participant veto without loss of access | FST-REQ-0010, FST-REQ-0035 | RS-010, D-005, D-006 | DEFERRED |
| FST-REQ-0058 | Fork protection implementation | Implementation: unique network identity format, chain-ID in transactions, distinct currency identifiers, replay protection across forks | FST-REQ-0033, FST-REQ-0040 | D-036 (APPROVED), RS-015 | DEFERRED |
| FST-REQ-0059 | Migration functionality | Implementation: voluntary migration window, fixed deadline, market conversion after closure | FST-REQ-0058 | D-021 (PROVISIONAL) | DEFERRED |

**Entry conditions:** Core network operational; owner decisions on upgrade/governance.

**Owner decisions required:** RS-010, D-021 mechanisms.

---

## Phase 9 — Independent Implementations & Conformance (DEFERRED)

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0060 | Protocol test vectors | Implementation: canonical test vectors for all message types, state transitions, consensus rules | FST-REQ-0010, FST-REQ-0035, FST-REQ-0043 | RS-012, D-035 | DEFERRED |
| FST-REQ-0061 | Conformance test harness | Implementation: automated conformance testing for independent implementations | FST-REQ-0060 | RS-012, D-035 | DEFERRED |
| FST-REQ-0062 | Independent client decision | Owner decision: 1, 2, or 3 independent implementations before mainnet | FST-REQ-0061 | RS-012, D-035 | DEFERRED (OPEN RESEARCH) |

**Entry conditions:** Protocol stable; owner decision on client count.

**Owner decisions required:** D-035, RS-012.

---

## Phase 10 — Production Hardening & Mainnet Readiness (DEFERRED)

**Requirements:**
| Req ID | Title | Acceptance Criteria | Dependencies | Research | Owner Gate |
|--------|-------|---------------------|--------------|----------|------------|
| FST-REQ-0063 | Security audit & formal verification | External audit, model checking, theorem proving, fuzzing results | All prior phases | RS-013 §10 | DEFERRED |
| FST-REQ-0064 | Operational tooling & monitoring | Logging, metrics, alerting, debugging, upgrade tooling | All prior phases | None | DEFERRED |
| FST-REQ-0065 | Mainnet launch criteria | Documented: security review complete, test network stable, independent implementations verified, governance operational | FST-REQ-0063, FST-REQ-0064 | All | DEFERRED |

---

## Cross-Cutting Requirements (Apply to All Phases)

| Req ID | Title | Description |
|--------|-------|-------------|
| FST-REQ-0066 | Research-first compliance | Every implementation change must cite supporting research with exact titles, authors, dates, DOI/URL |
| FST-REQ-0067 | Test evidence requirement | Every implemented requirement must have unit, integration, and (where relevant) fault-injection tests with actual results recorded |
| FST-REQ-0068 | Documentation update requirement | Every implementation change must update relevant architecture/specification documents |
| FST-REQ-0069 | Decision classification respect | No PROVISIONAL/OPEN RESEARCH decision implemented as production design without owner APPROVED ADR |
| FST-REQ-0070 | Git safety | No `git add .`, force-push, history rewrite; stage only reviewed files; commit coherent verified increments |
| FST-REQ-0071 | Secrets protection | No private keys, credentials, or secrets in code, logs, or commits |
| FST-REQ-0072 | FeinStatic preservation | Existing FeinStatic source, tests, release notes, Git history unchanged during Milestone 1; selective reuse only after review |
| FST-REQ-0073 | Incremental releases | Releases after ~2–3 new features with tests, docs, independent review (P13/P14) |
| FST-REQ-0074 | Autonomous continuation protocol | Repository contains complete project state for session resumption without owner re-explanation; AGENTS.md includes the permanent Engineering Anti-Loop and Progress Watchdog enforced on every `continue` and `pause` |

---

## Repository Maintenance Workstream (Ongoing)

This workstream runs continuously alongside all engineering phases. It applies
automatically on every `continue` and `stop` as defined in AGENTS.md Section
"Repository Maintenance and Publication Policy". It is complementary to the
primary FST engineering mission and does not alter the 74 FST requirements.

### Maintenance Workstream Tasks

| Task ID | Title | Description | Status | Evidence Required |
|---------|-------|-------------|--------|-------------------|
| MW-001 | Obsolete and redundant file identification | Scan for duplicate, obsolete, or redundant files that do not contribute to the approved FST scope. Investigate references (#include, import, CMakeLists.txt, tests) before any action. | PENDING | File dependency analysis; safe cleanup plan; no approved ADRs/research/tests deleted |
| MW-002 | Directory nesting simplification | Simplify directory structures only where nesting adds no meaningful architectural value. Preserve module boundaries. Update CMakeLists.txt and include paths when relocating. | PENDING | Before/after directory listing; updated CMakeLists.txt; build verification |
| MW-003 | GitHub README maintenance | Keep README.md professional, concise, and accurate. Reflect actual implemented state, not aspirational state. No private agent workflow details or unimplemented claims. | COMPLETE | README.md rewritten to describe current FST scope (UTXO, permissionless PoS, CometBFT BFT consensus, monetary issuance/fees, deterministic ABCI application, authenticated P2P networking); all version-by-version change sections, "Publish to GitHub" section, and outdated FeinStatic Match/Flow/Raft architecture removed; project status table distinguishes implemented/tested/integrated/planned/deferred; one ASCII architecture diagram added; all links, paths, and build/test commands verified against source tree, CMakeLists.txt, CMakePresets.json, ci.yml, and verify-all.sh |
| MW-004 | Local AGENTS.md protection | Preserve AGENTS.md and its full contents. Ensure AGENTS.md is excluded from GitHub publication (.gitignore). Verify Kilo can read AGENTS.md locally. | PENDING | .gitignore entry; local read verification |
| MW-005 | Safe feature branch integration | Merge feature branch into main only after inspecting remote divergence, preserving changes, reviewing conflicts, verifying merged result builds and passes tests. | PENDING | git fetch/log/diff output; conflict review; post-merge build and test results |
| MW-006 | Post-cleanup and post-merge verification | After structural changes or branch integration: full build in Debug + one sanitizer config; complete test suite; update all four operation records; record defects. | PENDING | Build logs; test results; updated project records; defect entries |

### Automatic Execution

On every `continue`:
1. Read `AGENTS.md` and all four project records.
2. Check for pending maintenance tasks from this workstream.
3. Execute the highest-priority maintenance task alongside engineering work.

On every `stop`:
1. Reach the next safe checkpoint.
2. Update all four project records.
3. Record exact state, completed work, outstanding work, blockers, and precise next action.

---

## Requirement Dependency Graph (Critical Path)

```
FST-REQ-0001..0009 (Phase 0) → FST-REQ-0010..0019 (Phase 1)
                                                        ↓
FST-REQ-0020..0030 (Phase 2) ← FST-REQ-0010 (spec)
                                                        ↓
                    ┌─────────────────────────────────┴─────────────────────────────────┐
                    ▼                                                                   ▼
FST-REQ-0031 (Consensus ADR) ← D-017 APPROVED                                    FST-REQ-0033 (Tx Model ADR) ← D-037 APPROVED
                    │                                                                   │
                    ▼                                                                   ▼
FST-REQ-0032 (Sybil ADR) ← D-016 APPROVED                                       FST-REQ-0040 (Tx Serialization)
                    │                                                                   │
                    └───────────────────────┬─────────────────────────────────────────┘
                                            ▼
                              FST-REQ-0035 (Consensus Impl)
                                            │
                                            ▼
                              FST-REQ-0036..0039 (Phase 3 complete)
                                            │
                                            ▼
                              FST-REQ-0041..0045 (Phase 4)
                                            │
                                            ▼
                              FST-REQ-0046..0050 (Phase 5)
                                            │
                                            ▼
                              FST-REQ-0051..0065 (Phases 6–10)
```

---

## Current Status Summary (as of 2026-11-17, Git HEAD `8a1465c`)

| Phase | Requirements | IMPLEMENTED | PARTIALLY_IMPLEMENTED | IN_PROGRESS | NOT_STARTED | BLOCKED | DEFERRED |
|-------|--------------|-------------|----------------------|-------------|-------------|---------|----------|
| 0 | 9 | 9 | 0 | 0 | 0 | 0 | 0 |
| 1 | 10 | 9 | 1 | 0 | 0 | 0 | 0 |
| 2 | 11 | 11 | 0 | 0 | 0 | 0 | 0 |
| 3 | 9 | 0 | 9 | 0 | 0 | 0 | 0 |
| 4 | 6 | 0 | 6 | 0 | 0 | 0 | 0 |
| 5 | 5 | 0 | 0 | 0 | 0 | 0 | 5 |
| 6 | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| 7 | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| 8 | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| 9 | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| 10 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 3 |
| Cross-cutting | 9 | 0 | 0 | 9 | 0 | 0 | 0 |
| **Total** | **74** | **29** | **1** | **24** | **0** | **0** | **20** |

**Active work:** Phase 2 complete (all 11 requirements IMPLEMENTED and VERIFIED; 29 harness tests pass, 271/271 total tests pass; capability matrix reclassified BASELINE→VERIFIED, 59 capabilities). Phase 3 and Phase 4 are now UNBLOCKED: owner decisions D-015, D-016, D-017, D-037 are APPROVED and recorded via ADRs ADR-0002/ADR-0003/ADR-0004/ADR-0005.

**Blocked work:** None — Phases 3–4 unblocked by owner decisions. CometBFT production dependency remains subject to the mandatory feasibility spike (ADR-0004) before commitment.

**Deferred work:** Phases 5–10 (20 requirements) correctly deferred per P13 (core network first).

**Next engineering task:** Complete the CometBFT integration feasibility spike (stable release, advisories including CSA-2026-001/Tachyon, ABCI compatibility, validator-set activation-height semantics, state sync/snapshots), then build the minimal CometBFT + C++ FST ABCI application prototype over a Unix-domain socket. Record actual results in verification-status.md.

---

## Owner Approval Gates (Explicit)

| Gate | Decision | Classification | Status | Blocks |
|------|----------|----------------|--------|--------|
| G1 | Monetary policy (supply, issuance, fees) | D-015 | APPROVED (ADR-0005) | Unblocked — FST-REQ-0033, FST-REQ-0044, FST-REQ-0032 |
| G2 | Sybil-resistance mechanism class | D-016 | APPROVED (ADR-0003) | Unblocked — FST-REQ-0032, FST-REQ-0034, FST-REQ-0035 |
| G3 | Consensus algorithm family | D-017 | APPROVED (ADR-0004) | Unblocked — FST-REQ-0031, FST-REQ-0035–0039 |
| G4 | Transaction model | D-037 | APPROVED (ADR-0002) | Unblocked — FST-REQ-0033, FST-REQ-0040–0045 |
| G5 | Offline operation design | D-018 | PROVISIONAL | FST-REQ-0053 |
| G6 | Privacy mechanisms | D-020 | PROVISIONAL | FST-REQ-0051, FST-REQ-0052 |
| G7 | Independent client count | D-035 | OPEN RESEARCH | FST-REQ-0062 |
| G8 | Fork protection mechanisms | D-036 | APPROVED (reqs only); mechanisms OPEN RESEARCH | FST-REQ-0058 |

**No implementation in Phases 3–4 until G1–G4 are APPROVED via ADRs.**

---

## Implementation Order Enforcement

The autonomous continuation protocol (AGENTS.md) enforces:
1. Never implement BLOCKED requirements (Phases 3–4) without owner APPROVED ADRs.
2. Never implement DEFERRED requirements (Phases 5–10) before Phase 2 complete and Phase 3–4 unblocked.
3. Within Phase 2, follow dependency order: identity → records → store → bootstrap → exchange → TLS → dispatch → admission → runtime → harness.
4. Cross-cutting requirements (0066–0074) apply to every change.
5. Update all four operation records at every durable checkpoint.

---

## Cross-References

- `docs/fst/scope/scope-v0.1.md` — Project scope (APPROVED)
- `docs/fst/principles/engineering-principles.md` — P1–P14 (APPROVED)
- `docs/fst/decisions/decision-register.md` — D-001 through D-037 classifications
- `docs/fst/roadmap/development-roadmap.md` — Milestone themes (PROVISIONAL)
- `docs/fst/architecture/feinstat-baseline.md` — FeinStatic baseline
- `docs/fst/research/research-register.md` — RS-001 through RS-015
- `docs/fst/adr/README.md` — ADR framework
- `docs/fst/operations/implementation-status.md` — Implementation progress ledger
- `docs/fst/operations/verification-status.md` — Verification ledger
- `docs/fst/operations/defect-register.md` — Defect register
- `AGENTS.md` — Autonomous continuation protocol (this document's enforcement mechanism)

---

**End of Project Plan.** This document must be updated only when:
- Owner approves a new ADR changing phase dependencies or requirement definitions.
- A requirement is split (preserve original ID as parent).
- Phase completion changes the critical path.
- Cross-cutting requirement enforcement changes.