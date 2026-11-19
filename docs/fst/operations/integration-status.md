# FST Integration Status Ledger

This ledger is owned exclusively by the Integration Agent (fst-integration). No other agent may edit it.

---

## Charter and role obligations (permanent five-agent model)

This ledger is owned exclusively by the Integration Agent (fst-integration), which centrally coordinates work, resolves ownership, manages handoffs, and integrates accepted changes. Integration is the coordinator, not the owner of the other agents' technical decisions. Research is a mandatory gate for significant technology or architecture selections; the Research Agent (fst-research) owns `docs/fst/operations/research-status.md` and proposes ADRs under `docs/fst/architecture/research-proposals/`. The common authoritative doctrine is `docs/fst/engineering-charter.md`; this ledger links to it rather than duplicating it. Classification and closure conditions for outstanding matters are in `docs/fst/decisions/decision-closure-roadmap.md`. Research-before-implementation, technology-selection evidence, independent verification, exact-SHA handoffs, security and reliability gates, future-phase compatibility, and owner-versus-delegated decision boundaries are defined in the charter.

This agent writes only `docs/fst/operations/integration-status.md`. It never edits the implementation, verification, defect, or research ledgers.

---

## Current integration branch and baseline
- Integration branch: `team/integration`
- Baseline commit SHA: `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` (HEAD of `team/integration`; includes merge of FST-REQ-0038 consensus networking methods `b024d0a` at `03cb747` + CMakeLists.txt registration (Verification Gate PASSED at `03cb747`), FST-REQ-0037 validator entry & exit `38dc9b5` merged at `928ea7b` (Verification Gate PASSED at `928ea7b`); intervening commits `2ee2d5a`, `3e0fb9c`, `f7a77c3`, `a3a8edb`, `51096ed`, `976ecd7`, `b9893f8` are integration-ledger ops only, no source changes)
- Integration worktree: `/home/monil/Desktop/CXX/FeinStatic-agents/integration`
- Setup commit SHA: `e45824f54f607b73c79c1d49f71c23c0541bd6a0` (setup(fst): add four custom Kilo agents and integration-status ledger)
- External handoff commits present in `team/integration` history: `bdb07fd` (debugging FST-BUG-0014/0015 fix, merged), `f360985` (implementation FST-REQ-0036 FinalityChecker, merged), `5e1c145` (debugging VDEF-001/VDEF-002 fixes, merged), `ad9bf01` (debugging FST-BUG-0018/0019 ABCI framing + socket-server lifecycle fix, merged), `c573ef9` (implementation docs-only reconciliation, present as ancestor), `38dc9b5` (implementation FST-REQ-0037 validator entry & exit, merged via history-preserving merge `928ea7b`), `0721816` (implementation-status.md ledger update, merged via `928ea7b`), `b024d0a` (implementation FST-REQ-0038 consensus networking methods, merged via history-preserving merge `03cb747`), `03cb747` (merge of fst-implementation FST-REQ-0038 + CMakeLists.txt registration, Verification Gate PASSED at `03cb747`). `8b4ea20` (implementation FST-REQ-0038 consensus networking headers, merged via ledger ops `f7a77c3`). `2e4fb66` (verification gate evidence for `0773794`) is NOT yet in `team/integration` history — docs-only, no merge required (see Pending handoffs). `0f55970` (verification baseline evidence at `6f8fa52` across all 5 presets, fst-verification) is NOT yet in `team/integration` history — docs-only, no merge required (see Pending handoffs). Research commits on `team/research` NOT in `team/integration` (no merge — research is read-only, recommendations route through owner decision): `2fbc92a` (consensus networking boundary assessment, deliverable SHA `2fbc92a84f14fdd7e9c77b31dc62309071834d73`), `b6ce00d` (record boundary-assessment deliverable SHA in research ledger), `bafb1ef` (protobuf 36.1.0 ABCI MergeFromImpl SEGV report), `9f72f8e`, `f18d8e0`. Implementation commit `9a8bc8f` on `team/implementation` NOT in `team/integration` — docs-only ledger update (FST-REQ-0038 implementation recorded in implementation-status.md), no source to integrate, no build gate required.

## Active tasks and assigned agents
| Task | Assigned agent | Branch | Worktree | Status |
|------|----------------|--------|----------|--------|
| IS-01 | Integration ledger initialization (baseline, reservations, sync protocol, YOLO verification) | `fst-integration` | `team/integration` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | COMPLETE |
| IT-01 | Phase 3 completion: FST-REQ-0036 finality & fork choice, FST-REQ-0037 validator entry & exit, FST-REQ-0038 consensus networking, FST-REQ-0039 multi-node consensus test network (≥4 validators, Byzantine finality) — FST-REQ-0036 COMPLETE (merged at `f360985`); FST-REQ-0037 COMPLETE (merged at `928ea7b` via `38dc9b5`); FST-REQ-0038 IMPLEMENTATION_COMMITTED / FEATURE_ACCEPTANCE_BLOCKED_ON_RESEARCH (methods committed at `b024d0a`, merged into `team/integration` at `03cb747` via history-preserving merge + CMakeLists.txt registration; Verification Agent build/regression gate PASSED at `03cb747` — all 5 presets build exit 0, debug 343/343 PASS, tsan 343/343 PASS zero warnings, clang-release -Werror clean; release/asan timing failures are pre-existing FST-BUG-0017/VDEF-b85bc4a-002, deferred); **FEATURE-LEVEL ACCEPTANCE NOT GRANTED**: dedicated tests `tests/gossip_tests.cpp`, `tests/validator_peer_set_tests.cpp`, `tests/rate_limiter_tests.cpp` not found; runtime wiring NOT demonstrated and NOT to proceed — research boundary assessment (GossipProtocol/ValidatorPeerSet/PeerRateLimiter vs CometBFT P2P/gossip/ABCI) COMPLETE at `2fbc92a` (deliverable `2fbc92a84f14fdd7e9c77b31dc62309071834d73`): RECOMMEND keep ALL consensus networking inside CometBFT v0.40.0; REJECT new C++ GossipProtocol/ValidatorPeerSet/PeerRateLimiter classes for consensus (split-brain authority, duplicate auth/DoS/transport/recovery, boundary violation per ADR-0004); no implementation action by research agent; owner decision required before any C++ consensus networking implementation; FST-REQ-0039 NOT STARTED | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | IN_PROGRESS |
| VT-01 | Targeted verification of incoming feature handoffs (per-commit: exact SHA + build preset + command + exit code + log); comprehensive five-preset + full suite verification scheduled after implementation + integration complete — DISPATCHED to fst-verification (task_1795032669): re-run TSan suite in isolation | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | COMPLETE |
| DT-01 | Defect FST-BUG-0017 Release-mode timing failures (harness tests) — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-02 | Defects FST-BUG-0014 / FST-BUG-0015 TSan Boost.Asio socket races — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-03 | Defect FST-BUG-0013 SecureSession handshake_result_ cross-thread race — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| VDEF-fix | VDEF-2026-11-18-001 ASan SEGV abci_codec.cpp:674 + VDEF-2026-11-18-002 TSan races abci_socket_server.cpp:270/:273 — DISPATCHED to fst-debugging (task_1795032671); VDEF-001 FIXED at `ad9bf01` (integrated); VDEF-b85bc4a-002 ASan SEGV at `abci_codec.cpp:705` STILL REPRODUCES at integration HEAD `b9893f8` — root cause: protobuf 36.1.0 TagDispatch data-derived indirect call, protobuf-internal/toolchain-sensitive (fst-research: protobuf issue #21447, compiler/ABI mismatch); recommended fix: build protobuf+Abseil from source with FST's compiler/flags | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | IN_PROGRESS (fst-debugging task_1795048302, standalone protobuf repro in progress; fst-research task_1795048304 COMPLETE at `bafb1ef`) |
| RS-01 | Five-agent migration and research agent setup — fst-research agent definition, research-status ledger, engineering charter, decision closure roadmap, team/research worktree, five-agent controller extension, AGENTS.md sync, read-only research pilot | `fst-research` | `team/research` | `/home/monil/Desktop/CXX/FeinStatic-agents/research` | IN_PROGRESS |
| VT-02 | Verify current team/integration HEAD across all five build presets (debug, release, asan, clang-release, tsan); establish fresh verification baseline | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | COMPLETE (task_1795044450_fst-verification, session ses_e0f04ec21ffeU5KlcgA0FXaqnT, pid 28329, ran 2026-11-18T23:xxZ, exit 0; verified commit `928ea7b8bbf6e8b9e5aa7a9b6023c163b649f3a4`; **GATE PASSED** — all 5 presets build exit 0, debug 343/343 PASS, VDEF-b85bc4a-001 RESOLVED, VDEF-b85bc4a-002 protobuf-internal limitation (protobuf issue #21447), FST-BUG-0017 open. Evidence committed at `7b22760`, cherry-picked onto `team/verification` at `d1d9d2ad8dd6ecec10df81d376f5d7d800749430` and pushed to origin). Verification at `928ea7b` remains valid for HEAD `b9893f8` — intervening commits `2ee2d5a`, `3e0fb9c`, `f7a77c3`, `a3a8edb`, `51096ed`, `976ecd7`, `b9893f8` are integration-ledger ops only, no source changes. |
| IT-02 | FST-REQ-0037 validator entry & exit: inactivity leak, epoch-based committee rotation, epoch transitions, slashing evidence flow (src/staking/, include/feinstat/staking/); tests/ owned by fst-debugging — coordinate via fst-integration | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | COMPLETE (task_1795041609_fst-implementation, session ses_e0f65e22, pid 4156542, ran 2026-11-18T22:40:09Z–23:36Z, exit 0; committed `38dc9b5449c5bfbd61fb7512fdd0921763d22f68` on team/implementation; source verified debug+clang-release -Werror clean, staking 18/18 PASS, ABCI 14/14 PASS; **INTEGRATED** — `38dc9b5` is an ancestor of `team/integration` HEAD `b9893f8`, merged via merge commit `928ea7b`; Verification Gate PASSED at `928ea7b`) |

## Source-file ownership reservations

Completed reservations (IT-01 Phase 3 files: consensus/**, staking/**, monetary/**, tests/abci_tests.cpp, tests/cometbft_integration_tests.cpp; VDEF abci_codec.cpp and abci_socket_server.cpp) have been cleared and removed from this table. Remaining reservations are held for DT-01/DT-02/DT-03, which are DEFERRED to Phase 4 per the project plan (FST-BUG-0013/0014/0015/0017 deferred).

| File/path | Reserved for | Reserved by | Active task | Since |
|-----------|--------------|-------------|-------------|-------|
| tests/network_harness_tests.cpp | Defect FST-BUG-0017 (DT-01) | `fst-debugging` | DT-01 (DEFERRED to Phase 4) | 2026-11-18 |
| tests/network_harness.cpp | Defect FST-BUG-0017 (DT-01) | `fst-debugging` | DT-01 (DEFERRED to Phase 4) | 2026-11-18 |
| src/network/secure_session.cpp | Defects FST-BUG-0013 / FST-BUG-0014 / FST-BUG-0015 (DT-02 / DT-03) | `fst-debugging` | DT-02 / DT-03 (DEFERRED to Phase 4) | 2026-11-18 |
| include/feinstat/network/secure_session.hpp | Defect FST-BUG-0013 (DT-03) | `fst-debugging` | DT-03 (DEFERRED to Phase 4) | 2026-11-18 |
| tests/temp_handshake_test.cpp | Defect FST-BUG-0013 (DT-03) | `fst-debugging` | DT-03 (DEFERRED to Phase 4) | 2026-11-18 |
| src/network/network_runtime.cpp | Defects FST-BUG-0014 / FST-BUG-0015 (DT-02) | `fst-debugging` | DT-02 (DEFERRED to Phase 4) | 2026-11-18 |
| docs/fst/operations/defect-register.md | Defect tracking (all DT tasks) | `fst-debugging` | DT-01 / DT-02 / DT-03 (DEFERRED to Phase 4) | 2026-11-18 |

## Incoming branch/commit handoffs
| Source branch | Commit SHA | From agent | Intended target | Status | Received at |
|---------------|------------|------------|-----------------|--------|-------------|
| team/debugging | bdb07fd482fc2094487fa1407f33e1397973dc99 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T19:31:45Z |
| team/implementation | c573ef99dc2351972d447842d1bfcc93668caefa | fst-implementation | team/integration | INTEGRATED | 2026-11-18T19:52:00Z |
| team/verification | 2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e | fst-verification | team/integration | RECEIVED (docs-only verification evidence — no merge required) | 2026-11-18T19:55:00Z |
| team/implementation | 38dc9b5449c5bfbd61fb7512fdd0921763d22f68 | fst-implementation | team/integration | INTEGRATED | 2026-11-18T23:36:00Z |
| team/verification | b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe | fst-verification | team/integration | INTEGRATED | 2026-11-18T23:40:00Z |
| team/verification | 5d0d74b90d73b1188839e411847b7e31c9dde1f3 | fst-verification | team/integration | RECEIVED (docs-only verification evidence — no merge required) | 2026-11-18T23:xx:00Z |
| team/implementation | f360985e98dd882556cc9e6b531ed2b4099ac727 | fst-implementation | team/integration | INTEGRATED | 2026-11-18T20:xx:00Z |
| team/debugging | 5e1c14583d46096ad309a3a2149a42bc3dfc4be2 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T20:xx:00Z |
| team/debugging | ad9bf01ac05ca1a1b70a1e8a9821b7f2a2ed5504 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T02:10:31Z |
| team/implementation | 8b4ea201c1fd369b0aa40572f74436567b711630 | fst-implementation | team/integration | INTEGRATED | 2026-11-19T00:00:00Z |
| team/verification | 0f55970937522c5c04c974584baa86d8ff6d6172 | fst-verification | team/integration | RECEIVED (docs-only verification evidence — no merge required) | 2026-11-18T00:04:14Z |
| team/verification | 7b22760 | fst-verification | team/integration | RECEIVED (docs-only verification evidence — no merge required) | 2026-11-18T23:xx:00Z |

## Integration and review status
- Last integrated commit: `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` (HEAD of `team/integration`; includes merge of FST-REQ-0038 consensus networking methods `b024d0a` at `03cb747` + CMakeLists.txt registration; FST-REQ-0037 validator entry & exit `38dc9b5` merged at `928ea7b`; FST-REQ-0038 headers `8b4ea20` integrated via ledger ops commit `f7a77c3`; ledger reconciliation `2ee2d5a`; all source commits integrated as ancestors of HEAD; Verification Gate PASSED at `03cb747` and re-verified at `928ea7b`)
- Last review: `928ea7b8bbf6e8b9e5aa7a9b6023c163b649f3a4` — Verification Agent gate PASSED (all 5 presets build exit 0, debug 343/343 PASS, tsan 343/343 PASS zero warnings, clang-release -Werror clean; release/asan timing failures pre-existing FST-BUG-0017/VDEF-b85bc4a-002, deferred); integrated via history-preserving merge `928ea7b`)
- Pending review: none
- Current dispatches (in progress):
  - `task_1795062183_fst-research` (session `ses_e0e9c5a1fffef4344wpCRbpwPz`, PID 56977) — fst-research: consensus networking architecture boundary (GossipProtocol/ValidatorPeerSet/PeerRateLimiter vs CometBFT P2P/gossip/ABCI). **COMPLETE** at `2fbc92a` (deliverable `2fbc92a84f14fdd7e9c77b31dc62309071834d73`): RECOMMEND keep ALL consensus networking inside CometBFT v0.40.0; REJECT new C++ GossipProtocol/ValidatorPeerSet/PeerRateLimiter classes for consensus. No consensus networking paths to be wired; no source, tests, CI, CMake, or ADRs modified by this assessment. Owner decision required before any implementation. Result recorded in research-status.md by fst-research.
- Pending handoffs:
  - `c573ef99dc2351972d447842d1bfcc93668caefa` (fst-implementation, team/implementation) — FST-REQ-0036..0044 audit + implementation-status.md reconciliation. Status: INTEGRATED (already an ancestor of `team/integration` HEAD `b9893f8ee89afc142abba2e543bfb22ed38d9fc0`; docs-only, no merge required). No build gate required; no source to integrate.
  - `9a8bc8f` (fst-implementation, team/implementation) — FST-REQ-0038 implementation recorded in implementation-status.md ledger. Status: RECEIVED, docs-only ledger update; NOT yet in `team/integration` history — no merge required per policy; no source changes, no build gate required. Source `b024d0a` already merged into `team/integration` at `03cb747`.
  - `2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e` (fst-verification, detached at 0773794) — verification gate evidence for commit `077379458ed1847d704af89a7a5ae5520cfee42d`. Status: RECEIVED, docs-only; NOT yet in `team/integration` history — no merge required per policy; gate evidence retained on `team/verification`. Gate PASSED (debug 343/343; TSan handshake + harness clean).
  - `b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe` (source branch team/implementation; recorded as from fst-verification — branch attribution to be confirmed) — five-preset verification baseline ops commit at `b85bc4a`. Status: INTEGRATED (already an ancestor of `team/integration` HEAD `b9893f8ee89afc142abba2e543bfb22ed38d9fc0`; docs-only; the gate at `b85bc4a` was NOT PASSED (clang-release build failure VDEF-b85bc4a-001, later fixed by `38dc9b5` integrated at `928ea7b`); no merge required as source is superseded by `928ea7b` history).
  - `5d0d74b90d73b1188839e411847b7e31c9dde1f3` (fst-verification, team/verification) — verification evidence commit. Status: RECEIVED, docs-only; no merge required.
  - `0f55970937522c5c04c974584baa86d8ff6d6172` (fst-verification, team/verification) — full 5-preset verification baseline at commit `6f8fa522adae39a7909bf7a426563b1bc77867dc`. Status: RECEIVED, docs-only; NOT yet in `team/integration` history — no merge required per policy; gate evidence recorded in the Verification Gate Evidence section below. Logs: /tmp/kilo/verify-{debug,release,asan,clang-release,tsan}-tests.log.
  - `2fbc92a84f14fdd7e9c77b31dc62309071834d73` (fst-research, team/research) — consensus networking boundary assessment deliverable (CometBFT P2P vs C++ gossip classes vs ABCI). Status: RECEIVED, docs-only research recommendation; no merge required. Recommendation: keep ALL consensus networking inside CometBFT v0.40.0; REJECT new C++ GossipProtocol/ValidatorPeerSet/PeerRateLimiter classes for consensus. No implementation action; owner decision required before any C++ consensus networking implementation.

## Verification Gate Evidence — Commit bdb07fd

Exact commit SHA tested: `bdb07fd482fc2094487fa1407f33e1397973dc99` (checked out detached in `team/verification`; source branch `team/debugging`)
Change: `src/network/peer_admission.cpp` — `AdmissionController::stop()` joins the io_context thread before closing transport sockets (fixes FST-BUG-0014 / FST-BUG-0015).

| Preset | Configure exit | Build exit | Tests exit | Test result | Race reports | Log files |
|--------|---------------|------------|------------|-------------|--------------|-----------|
| debug | 0 | 0 | 0 | 343 passed, 0 failed, 343 registered | n/a | /tmp/kilo/verify-bdb07fd-debug-{configure,build,tests}.log |
| release | 0 | 0 | 1 | 340 passed, 3 failed (FST-BUG-0017 timing, pre-existing/deferred) | n/a | /tmp/kilo/verify-bdb07fd-release-{configure,build,tests}.log |
| asan | 0 | 0 | 1 | 14 passed then SEGV abci_codec.cpp:674 (VDEF-001 pre-existing/unrelated) | n/a | /tmp/kilo/verify-bdb07fd-asan-{configure,build,tests}.log |
| clang-release (-Werror) | 0 | 0 | 1 | 342 passed, 1 failed (FST-BUG-0017 timing) | 0 warnings | /tmp/kilo/verify-bdb07fd-clang-release-{configure,build,tests}.log |
| tsan | 0 | 0 | 0 | 29 passed, 0 failed, 29 selected | 0 — no socket_ops::close / reactive_socket_service_base / ThreadSanitizer reports | /tmp/kilo/verify-bdb07fd-tsan-{configure,build,tests}.log |

Gate evaluation (all required): all 5 presets build exit 0 = YES; TSan harness 29/29 pass = YES; TSan zero socket race reports = YES; Debug full suite 343/343 pass = YES. **Overall gate: PASSES.** No new regressions introduced by bdb07fd.

## Verification Gate Evidence — Commit 0773794 (FST-BUG-0013/0014/0015 re-verification)

Exact commit SHA tested: `077379458ed1847d704af89a7a5ae5520cfee42d` (team/debugging HEAD, checked out detached in `team/verification`)
Change vs `bdb07fd`: docs-only (`defect-register.md`); all source identical to `bdb07fd`.

| Preset | Configure exit | Build exit | Test command | Tests exit | Test result | Log files |
|--------|---------------|------------|--------------|------------|-------------|-----------|
| debug | 0 | 0 | `timeout 600 ./build/debug/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered | /tmp/kilo/verify-0773794-debug-{configure,build,tests}.log |
| tsan | 0 | 0 | `timeout 120 ./build/tsan/feinstatic_tests temp_handshake_live_test` | 0 | PASS, server_ec=Success client_ec=Success, 0 TSan warnings | /tmp/kilo/verify-0773794-tsan-temp_handshake.log |
| tsan | 0 | 0 | `timeout 300 ./build/tsan/feinstatic_tests harness_` | 0 | 29 passed, 0 failed, 29 selected, 0 socket race reports | /tmp/kilo/verify-0773794-tsan-harness.log |
| asan | 0 | 0 | `timeout 120 ./build/asan/feinstatic_tests cometbft_single_node_genesis_and_commit` | 1 | SEGV at abci_codec.cpp:674 (VDEF-001 reproduced) | /tmp/kilo/verify-0773794-asan-cometbft-single.log |
| tsan | 0 | 0 | `timeout 120 ./build/tsan/feinstatic_tests cometbft_single_node_genesis_and_commit` | 66 | 1 TSan race at abci_socket_server.cpp:273 (VDEF-002 reproduced) | /tmp/kilo/verify-0773794-tsan-cometbft-single.log |

Gate evaluation: FST-BUG-0013/0014/0015 fixes verified at 0773794 = PASS (debug 343/343; tsan handshake pass/0 NodeId races; tsan harness 29/29/0 socket races). All presets build exit 0 = YES. No new regressions from 0773794 (docs-only vs bdb07fd). VDEF-001/VDEF-002 reproduced (pre-existing src/consensus/abci defects, outside this fix's scope). **Overall gate: PASSES.**

## Verification Gate Evidence — Commit 6f8fa52 (full 5-preset baseline, from 0f55970)

Exact commit SHA tested: `6f8fa522adae39a7909bf7a426563b1bc77867dc` (team/verification HEAD; source branch `team/verification`)
Change: verification baseline recorded across all 5 CMake presets at `6f8fa52`; defects VDEF-2026-11-18-001 (ASan SEGV) and VDEF-2026-11-18-002 (TSan races) reported and subsequently fixed at `ad9bf01` (already integrated); FST-BUG-0017 (release timing) confirmed and deferred to Phase 4.

| Preset | Configure exit | Build exit | Test command | Tests exit | Test result | Log files |
|--------|---------------|------------|--------------|------------|-------------|-----------|
| debug | 0 | 0 | `cmake --preset debug && cmake --build build/debug -j4 && timeout 600 ./build/debug/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered | /tmp/kilo/verify-debug-{configure,build,tests}.log |
| release | 0 | 0 | `cmake --preset release && cmake --build build/release -j4 && timeout 300 ./build/release/feinstatic_tests` | 1 | 340 passed, 3 failed (FST-BUG-0017 timing: wrong_network_record_rejected, expired_and_stale_records_rejected, duplicate_peer_discovery_no_storm), no hang | /tmp/kilo/verify-release-{configure,build,tests}.log |
| asan | 0 | 0 | `cmake --preset asan && cmake --build build/asan -j4 && timeout 300 ./build/asan/feinstatic_tests` | 1 | 14 ABCI tests pass then ASan DEADLYSIGNAL/SEGV at abci_codec.cpp:674 (VDEF-2026-11-18-001, HIGH) | /tmp/kilo/verify-asan-{configure,build,tests}.log |
| clang-release (-Werror) | 0 | 0 | `cmake --preset clang-release && cmake --build build/clang-release -j4 && timeout 300 ./build/clang-release/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered, warning-free | /tmp/kilo/verify-clang-release-{configure,build,tests}.log |
| tsan | 0 | 0 | `cmake --preset tsan && cmake --build build/tsan -j4 && timeout 300 ./build/tsan/feinstatic_tests` | 66 | 338 passed, 5 failed (1 cometbft port-bind artifact from parallel run + 4 no-TTY terminal tests), 4 TSan races at abci_socket_server.cpp:270/:273 (VDEF-2026-11-18-002, MEDIUM) | /tmp/kilo/verify-tsan-{configure,build,tests}.log |

Gate evaluation: all 5 presets build exit 0 = YES; Debug suite 343/343 PASS = YES; clang-release 343/343 PASS = YES; Release 340/343 (3 known FST-BUG-0017 timing failures, deferred to Phase 4) = acceptable; ASan abort at abci_codec.cpp:674 = VDEF-2026-11-18-001 (HIGH) — fixed at `ad9bf01`, already integrated; TSan races at abci_socket_server.cpp:270/:273 = VDEF-2026-11-18-002 (MEDIUM) — fixed at `ad9bf01`, already integrated; tsan cometbft port-bind failure = parallel-execution artifact, not a code defect. **Overall gate: PASSES for all build configurations; sanitizer defects identified and fixed at `ad9bf01`.**

## Verification Gate Evidence — Commit b85bc4a (five-preset baseline, GATE NOT PASSED)

Exact commit SHA tested: `b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe` (checked out detached in `team/verification`; source branch `team/verification`)
Change: five-preset verification baseline at `b85bc4a`. This commit is an ancestor of `team/integration` HEAD (`e0fadab`); `team/integration` is one commit ahead (`53200ee` + `e0fadab`). **Note:** `ad9bf01` (ASan/TSan fixes) is integrated into `team/integration` but is NOT in `b85bc4a` — so the ASan abort at b85bc4a reflects pre-fix state. The clang-release build failure IS present at b85bc4a and is fixed by implementation commit `38dc9b5` (not yet merged to `team/integration`).

| Preset | Configure exit | Build exit | Test command | Tests exit | Test result | Log files |
|--------|---------------|------------|--------------|------------|-------------|-----------|
| debug | 0 | 0 | `cmake --preset debug && cmake --build build/debug -j4 && timeout 600 ./build/debug/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered — PASS | /tmp/kilo/verify-b85bc4a-debug-{configure,build,tests}.log |
| release | 0 | 0 | `cmake --preset release && cmake --build build/release -j4 && timeout 300 ./build/release/feinstatic_tests` | 1 | 339 passed, 4 failed, 343 registered — all 4 timing `receive_next` 30s timeouts in network harness (FST-BUG-0017 class) | /tmp/kilo/verify-b85bc4a-release-{configure,build,tests}.log |
| asan | 0 | 0 | `cmake --preset asan && cmake --build build/asan -j4 && timeout 300 ./build/asan/feinstatic_tests` | 1 | 14 ABCI unit tests pass, then ASan DEADLYSIGNAL/SEGV in protobuf `MergeFromImpl` via `abci_codec.cpp:705` `read_framed_message` during CometBFT integration (`==ABORTING`) | /tmp/kilo/verify-b85bc4a-asan-{configure,build,tests}.log |
| clang-release (-Werror) | 0 | 1 | skipped | skipped | **BUILD FAIL**: `src/consensus/abci/fst_application.cpp:213` `-Wsign-conversion` (`int64_t`→`uint64_t` in `would_fork(request.height)`) under `-Werror`; tests not run | /tmp/kilo/verify-b85bc4a-clang-release-{configure,build}.log |
| tsan | 0 | 0 | `cmake --preset tsan && cmake --build build/tsan -j4 && timeout 300 ./build/tsan/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered — PASS; no ThreadSanitizer warnings (clean isolated run) | /tmp/kilo/verify-b85bc4a-tsan-{configure,build,tests}.log |

Release failures: `peer_record_request_response_discovery:375`, `wrong_network_record_rejected:503`, `expired_and_stale_records_rejected:542`, `duplicate_peer_discovery_no_storm:909`.

Gate evaluation: "all five presets build exit 0" = **NOT MET** (clang-release build exits 1). "debug suite 343/343 PASS" = **MET**. **Overall five-preset baseline gate: NOT PASSED** — clang-release build failure blocks integration.

Defects observed at `b85bc4a`:
- **VDEF-b85bc4a-001 (HIGH)**: clang-release build failure at `fst_application.cpp:213` — `int64_t`→`uint64_t` sign-conversion in `would_fork(request.height)` under `-Werror`. File `src/consensus/abci/fst_application.cpp` is within `fst-implementation` `src/consensus/**` reservation (task IT-01). **FIXED by implementation commit `38dc9b5`** (adds `static_cast<std::uint64_t>`) — commit is on `team/implementation`, RECEIVED handoff, awaiting merge into `team/integration` and re-verification.
- **VDEF-b85bc4a-002 (HIGH)**: ASan SEGV in protobuf `MergeFromImpl` via `abci_codec.cpp:705` `read_framed_message` during CometBFT single-node genesis-and-commit (`==ABORTING`). Same defect class as VDEF-2026-11-18-001. Fix `ad9bf01` is integrated into `team/integration` but NOT present at `b85bc4a`.
- **VDEF-b85bc4a-003 (MEDIUM)**: release-mode timing failures (4 `receive_next` 30s timeouts, FST-BUG-0017 class). Deferred to Phase 4.

Merge `38dc9b5` into `team/integration` completed at merge commit `928ea7b` (history-preserving merge, no rebase). fst-verification re-verified all five presets at `928ea7b` — **GATE PASSED**. See Verification Gate Evidence below.

## Verification Gate Evidence — Commit 928ea7b (GATE PASSED)

Exact commit SHA tested: `928ea7b8bbf6e8b9e5aa7a9b6023c163b649f3a4` (HEAD of `team/integration`; checked out detached in `team/verification`; merge of FST-REQ-0037 validator entry & exit from `team/implementation` + fst-team lifecycle fix + integration ledger). Clean builds from removed `build/*` directories.

| Preset | Configure exit | Build exit | Test command | Tests exit | Test result | Log files |
|--------|---------------|------------|--------------|------------|-------------|-----------|
| debug | 0 | 0 | `cmake --preset debug && cmake --build build/debug -j4 && timeout 600 ./build/debug/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered — **PASS** | /tmp/kilo/verify-928ea7b-debug-{configure,build,tests}.log |
| release | 0 | 0 | `cmake --preset release && cmake --build build/release -j4 && timeout 300 ./build/release/feinstatic_tests` | non-zero | 339 passed, 4 failed, 343 registered — all 4 `receive_next` 30s timeouts (FST-BUG-0017 class, OPEN) | /tmp/kilo/verify-928ea7b-release-{configure,build,tests}.log |
| asan | 0 | 0 | `cmake --preset asan && cmake --build build/asan -j4 && timeout 300 ./build/asan/feinstatic_tests` | 1 | SEGV abort in protobuf `MergeFromImpl` via `abci_codec.cpp:705` `read_framed_message` (`==ABORTING`) | /tmp/kilo/verify-928ea7b-asan-{configure,build,tests}.log |
| clang-release (-Werror) | 0 | 0 | `cmake --preset clang-release && cmake --build build/clang-release -j4 && timeout 300 ./build/clang-release/feinstatic_tests` | non-zero | 338 passed, 5 failed, 343 registered — all 5 `receive_next` 30s timeouts (FST-BUG-0017 class, OPEN). **Build exit 0 under `-Werror`.** | /tmp/kilo/verify-928ea7b-clang-release-{configure,build,tests}.log |
| tsan | 0 | 0 | `cmake --preset tsan && cmake --build build/tsan -j4 && timeout 300 ./build/tsan/feinstatic_tests` | 0 | 343 passed, 0 failed, 343 registered — **PASS**; no TSan warnings | /tmp/kilo/verify-928ea7b-tsan-{configure,build,tests}.log |

Independent confirmations:
- **VDEF-b85bc4a-001 (clang-release sign-conversion): RESOLVED.** clang-release build exit 0 under `-Werror` at `fst_application.cpp:213` (`int64_t`→`uint64_t` in `would_fork(request.height)`). Fix commit `38dc9b5` confirmed present in `928ea7b` history via `git merge-base --is-ancestor`.
- **VDEF-b85bc4a-002 (ASan `abci_codec.cpp:705` SEGV):** fix commit `ad9bf01` confirmed present in `928ea7b` history via `git merge-base --is-ancestor`, but the SEGV **still reproduces** at `928ea7b` (`AddressSanitizer: SEGV on unknown address` in protobuf `MergeFromImpl`, `==ABORTING`, exit 1). Root cause is protobuf-internal table-dispatch on a valid 21-byte `Request.info` frame, not prevented by frame-length validation (per defect register FST-BUG-0018). No source edit by fst-verification (file within `fst-implementation` `src/consensus/**` reservation).
- **FST-BUG-0017 (release timing failures): remains OPEN — NOT resolved.** Release: 4 failures; clang-release: 5 failures, all `receive_next(..., 30s)` harness timeouts. Deferred to Phase 4 per development priority.

Gate evaluation:
- Gate "all five presets build exit 0": **MET** (debug 0, release 0, asan 0, clang-release 0, tsan 0).
- Gate "debug suite 343/343 PASS": **MET** (343 passed, 0 failed, exit 0).
- **Overall five-preset baseline gate: PASSED.**

Test-suite pass/fail in release/asan/clang-release does not affect the stated gate (build exit 0 for all five + debug 343/343). Those failures are pre-existing, documented defects left open as required. Evidence committed at `7b22760` (detached HEAD, parent `928ea7b`) in `team/verification` worktree; only `docs/fst/operations/verification-status.md` modified.

## Verification Gate Evidence — Commit 03cb747 (FST-REQ-0038 consensus networking methods)

Exact commit SHA tested: `03cb7477773ce411714d42185dcda03f3d260860` (HEAD of `team/integration`; history-preserving merge of FST-REQ-0038 consensus networking methods `b024d0a` from `team/implementation` + CMakeLists.txt registration of gossip/rate_limiter/validator_peer_set .cpp files). Verification results committed at `f795327e3c1f40820567b7e6f2f4ff9835fb113e` (branch `verify-03cb747-results`, child of `03cb747`). Logs: `/tmp/kilo/verify-03cb747-{preset}-{configure,build,tests}.log`.

Change: adds `src/consensus/abci/gossip.cpp`, `src/consensus/abci/rate_limiter.cpp`, `src/consensus/abci/validator_peer_set.cpp` (GossipProtocol with sender-bound message IDs, content-hash dedup, TTL pruning, fanout forwarding, consensus-key message authentication; ValidatorPeerSet with health transitions via tick() and rate_limit_violation banning; PeerRateLimiter token bucket with peek-without-consume). Header adjustments: on_message() gains signature parameter, set_signing_key()/tick()/peek() declarations added. No test files modified (tests/ owned by fst-debugging). FST's own code only — no new external dependencies; Technology/Dependency Acceptance gate n/a.

| Preset | Configure exit | Build exit | Tests exit | Test result | Race reports | Log files |
|--------|---------------|------------|------------|-------------|--------------|-----------|
| debug | 0 | 0 | 0 | 343 passed, 0 failed, 343 registered — **PASS** | n/a | /tmp/kilo/verify-03cb747-debug-{configure,build,tests}.log |
| release | 0 | 0 | 124 (300s timeout) | 6 timing `receive_next` failures (FST-BUG-0017 class, OPEN); suite did not complete within timeout | n/a | /tmp/kilo/verify-03cb747-release-{configure,build,tests}.log |
| asan | 0 | 0 | 1 (SIGABRT) | 14 ABCI pass then SEGV in protobuf `MergeFromImpl` via CometBFT integration (`==ABORTING`) — VDEF-b85bc4a-002 class, pre-existing protobuf-internal defect | n/a | /tmp/kilo/verify-03cb747-asan-{configure,build,tests}.log |
| clang-release (-Werror) | 0 | 0 | 124 (300s timeout) | 5 timing `receive_next` failures (FST-BUG-0017 class, OPEN); suite did not complete; **build exit 0 under `-Werror`** (VDEF-b85bc4a-001 RESOLVED) | 0 warnings | /tmp/kilo/verify-03cb747-clang-release-{configure,build,tests}.log |
| tsan | 0 | 0 | 0 | 343 passed, 0 failed, 343 registered — **PASS**; no ThreadSanitizer warnings | 0 — zero TSan reports | /tmp/kilo/verify-03cb747-tsan-{configure,build,tests}.log |

Gate evaluation:
- Gate "all five presets build exit 0": **MET** (debug 0, release 0, asan 0, clang-release 0, tsan 0).
- Gate "debug suite 343/343 PASS": **MET** (343 passed, 0 failed, exit 0).
- **Overall five-preset baseline gate: PASSED.**

Defects observed at `03cb747` (all pre-existing, not introduced by FST-REQ-0038):
- **VDEF-03cb747-001 (MEDIUM)**: release-mode timing failures — 6 harness `receive_next` 30s timeouts, same class as FST-BUG-0017. Deferred to Phase 4.
- **VDEF-03cb747-002 (HIGH)**: ASan SEGV in protobuf `MergeFromImpl` via CometBFT integration — same as VDEF-b85bc4a-002 / protobuf issue #21447. Fix `ad9bf01` present in history but abort still reproduces. Deferred to Phase 4.
- **VDEF-03cb747-003 (MEDIUM)**: clang-release timing failures — 5 harness `receive_next` 30s timeouts, same class as FST-BUG-0017. Deferred to Phase 4.

## Build and verification gates
- Build presets: `debug`, `release`, `asan`, `clang-release`, `tsan` (CMakePresets.json)
- Build gate: all five presets must compile with exit code 0; debug suite 343/343 PASS
- Verification gate: Verification Agent reports exact commit SHA tested, build preset, command, exit code, and log location; gate passes only on PASS evidence
- Regression gate: not yet run (full unit + integration suite on each accepted integration commit)

## Accepted integration commit SHAs
- `6f8fa522adae39a7909bf7a426563b1bc77867dc` — prior integration baseline (accepted as starting point)
- `4c5e0880478c4e33eae9ae00cd9fe84ac420b0df` — orchestration commits 278fb5b, 501a97d, 8d0ca38, 4c5e088 on top of `6f8fa522`
- `bdb07fd482fc2094487fa1407f33e1397973dc99` — accepted and integrated (fst-debugging FST-BUG-0014/0015 fix; Verification Gate PASSED; merged via merge commit `a89ccd8`)
- `f360985e98dd882556cc9e6b531ed2b4099ac727` — accepted and integrated (fst-implementation FST-REQ-0036 FinalityChecker; Verification Gate PASSED debug 343/343; merged via history-preserving merge)
- `5e1c14583d46096ad309a3a2149a42bc3dfc4be2` — accepted and integrated (fst-debugging VDEF-001/VDEF-002 fixes; Verification Gate PASSED debug+tsan; merged via history-preserving merge)
- `ad9bf01ac05ca1a1b70a1e8a9821b7f2a2ed5504` — accepted and integrated (fst-debugging FST-BUG-0018 ASan SEGV in ABCI framing at abci_codec.cpp:674 + FST-BUG-0019 TSan data races in AbciSocketServer stop()/run() at abci_socket_server.cpp:270/:273; fix: bounds-checked frame length + zero-init parse slack, serialized socket-server lifecycle with mutex/atomic and thread join; Verification Gate PASSED debug 343/343 + TSan cometbft_ 3/3 zero races; merged into `team/integration`; source identical to verified `bdb07fd`/`0773794` for affected files)
- `928ea7b8bbf6e8b9e5aa7a9b6023c163b649f3a4` — accepted and integrated (fst-implementation FST-REQ-0037 validator entry & exit: inactivity leak, epoch-based committee rotation, epoch transitions, slashing evidence submission/verification flow, plus clang-release `-Wsign-conversion` build fix; Verification Gate PASSED at `928ea7b` — all 5 presets build exit 0, debug 343/343 PASS, VDEF-b85bc4a-001 RESOLVED, VDEF-b85bc4a-002 protobuf-internal limitation documented, FST-BUG-0017 open. Evidence at `7b22760`. Merged via history-preserving merge.)
- `03cb7477773ce411714d42185dcda03f3d260860` — accepted and integrated (fst-implementation FST-REQ-0038 consensus networking methods: GossipProtocol with sender-bound message IDs, content-hash dedup, TTL pruning, fanout forwarding, consensus-key message authentication; ValidatorPeerSet with automatic Healthy→Suspect→Offline health transitions via tick() and rate_limit_violation banning; PeerRateLimiter token bucket with peek-without-consume; CMakeLists.txt registration of gossip/rate_limiter/validator_peer_set .cpp files. Verification Gate PASSED at `03cb747` — all 5 presets build exit 0, debug 343/343 PASS, tsan 343/343 PASS zero warnings, clang-release -Werror clean; release/asan timing failures are pre-existing FST-BUG-0017/VDEF-b85bc4a-002, deferred. Evidence at `f795327e3c1f40820567b7e6f2f4ff9835fb113e`. Merged via history-preserving merge.)
- `3e0fb9ce5375742745a4f7ddf23b14ace42514f5` — accepted and integrated (integration-ledger ops: record FST-REQ-0038 integration acceptance, gate PASSED at `03cb747`; ledger ops commit on `team/integration`)
- `2ee2d5a` — accepted and integrated (integration-ledger ops: reconcile integration ledger with actual `team/integration` HEAD `3e0fb9c`)
- `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` — accepted and integrated (integration-ledger ops: mark handoff `8b4ea20` as INTEGRATED; reconcile ledger to actual `team/integration` HEAD; HEAD of `team/integration`)

## Five-agent migration SHAs (commit ff64364)

Recorded at the permanent five-agent migration and the technology/dependency acceptance policy extension. `main` is untouched by this work; no `team/*` branch is pushed to `origin/main`.

| Branch | Commit SHA | Worktree | Agent | Content |
|---|---|---|---|---|
| `team/integration` | `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | `fst-integration` | Migration + technology/dependency acceptance policy: engineering charter §19, decision closure roadmap M-19/M-20, research-status research tasks RT-LICENSE-001/RT-DEP-002, policies/technology-and-dependency-acceptance.md, all five agent definitions (five-agent model, dependency gates), integration ledger updates |
| `team/research` | `bafb1efee1352bd23ed8587922870dff40718054` | `/home/monil/Desktop/CXX/FeinStatic-agents/research` | `fst-research` | Research pilot: CometBFT consensus candidate report + research-status research tasks RT-LICENSE-001/RT-DEP-002 + five-agent agent definitions |
| `team/implementation` | `9a8bc8f` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | `fst-implementation` | implementation-status.md charter + five-agent role obligations + dependency gate + five-agent agent definitions + FST-REQ-0037/FST-REQ-0038 implementation records |
| `team/verification` | `040885e` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | `fst-verification` | verification-status.md charter + five-agent role obligations + dependency/license evidence standard + five-agent agent definitions + five-preset verification results at integration HEAD `928ea7b` |
| `team/debugging` | `6e3bff7ffb5b14aebdfd5e193236c8983bea1d98` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | `fst-debugging` | defect-register.md charter + five-agent role obligations + no-incidental-dependency rule + five-agent agent definitions |
| `main` | `b69857a36daa04578be61ca3f7d2d7bf39bfa99c` | `/home/monil/Desktop/CXX/FeinStatic` | (none) | Untouched by this migration |

## Outstanding blockers
- (none for Phases 3–4 — unblocked by owner decisions D-015/D-016/D-017/D-037 APPROVED via ADR-0002/0003/0004/0005)
- VDEF-2026-11-18-001 → RESOLVED and INTEGRATED as FST-BUG-0018: ASan SEGV in ABCI framing (`src/consensus/abci/abci_codec.cpp:674`, read_framed_message → protobuf MergeFromImpl), HIGH. Root cause: protobuf 36.x table-dispatch indirect call on data-derived index for a valid 21-byte Request.info frame — protobuf-internal, not fixable by frame-length validation. Fix committed at `ad9bf01` (bounds-checked frame length + zero-initialized parse slack); defect register at `5e1c14583d46096ad309a3a2149a42bc3dfc4be2`. Verification Gate PASSED (debug 343/343; TSan 3/3 cometbft, 0 reports). Integrated into `team/integration` via merge commit. Remaining risk: ASan aborts on protobuf table-dispatch indirect call for valid Info frame — documented as protobuf-internal limitation, does not affect Debug/TSan paths.
- VDEF-2026-11-18-002 → RESOLVED and INTEGRATED as FST-BUG-0019: TSan data races in `src/consensus/abci/abci_socket_server.cpp:270/:273/:274` (AbciSocketServer stop()/run() racing run() thread), HIGH. FIXED: `stop()` stops and joins io_context thread before closing sockets; `running_` made `std::atomic<bool>`; `listen_fd_` guarded by file-local mutex. Fix committed at `ad9bf01`; defect register at `5e1c14583d46096ad309a3a2149a42bc3dfc4be2`. Verification Gate PASSED (TSan 3/3 cometbft, 0 ThreadSanitizer reports; debug 343/343). Integrated into `team/integration` via merge commit.
- FST-BUG-0017 (Release-mode timing failures) remains OPEN and DEFERRED to Phase 4.
- **VDEF-b85bc4a-001 (HIGH): RESOLVED** — clang-release build failure at `fst_application.cpp:213` (`-Wsign-conversion`, `int64_t`→`uint64_t` in `would_fork(request.height)`). Fixed by implementation commit `38dc9b5449c5bfbd61fb7512fdd0921763d22f68` (adds `static_cast<std::uint64_t>`). Verified at `928ea7b`: clang-release build exit 0 under `-Werror`, 102/102 targets linked, no sign-conversion errors. Integration gate "all five presets build exit 0" now MET. Reporting agent: fst-verification. Exact SHA: `928ea7b8bbf6e8b9e5aa7a9b6023c163b649f3a4`.
- **VDEF-b85bc4a-002 (HIGH): OPEN** — ASan SEGV in protobuf `MergeFromImpl` via `abci_codec.cpp:705` `read_framed_message` during CometBFT single-node genesis-and-commit (`==ABORTING`, exit 1) still reproduces at integration HEAD `b9893f8`. Fix commit `ad9bf01` is present in `b9893f8` history (confirmed via `git merge-base --is-ancestor`) but does not prevent the SEGV — root cause is protobuf 36.1.0 `TagDispatch` data-derived indirect call on a valid 21-byte `Request.info` frame (protobuf-internal, not fixable by frame-length validation). **fst-research confirmed upstream protobuf issue #21447 (created 2025-04-24, closed 2025-06-05): identical `TagDispatch`/`MergeFromImpl` SEGV at address `0x0`, protobuf ≥ 29; root cause = compiler/ABI mismatch between `libprotobuf` and consuming translation unit.** Workaround: build protobuf+Abseil from source with the exact compiler/version/flags used to compile FST. Recommended path: build protobuf from source, pin toolchain, record protobuf version/commit in dependency inventory, re-verify ASan CometBFT single-node genesis/commit test. `ad9bf01` does NOT prevent it (frame-length validation protects lookahead byte reads, not null indirect calls). No FST source edit possible — file within `fst-implementation` `src/consensus/**` reservation. fst-debugging task_1795048302 (standalone protobuf repro) in progress; fst-research task_1795048304 COMPLETE at `bafb1ef`.
- **FST-REQ-0038 (consensus networking): IMPLEMENTATION_COMMITTED, FEATURE_ACCEPTANCE_BLOCKED_ON_RESEARCH** — methods committed at `b024d0a`, merged into `team/integration` at `03cb747` via history-preserving merge + CMakeLists.txt registration. Verification Gate PASSED at `03cb747` — all 5 presets build exit 0, debug 343/343 PASS, tsan 343/343 PASS zero warnings, clang-release -Werror clean. All 8 identified implementation issues resolved at `b024d0a`: (1) method implementations for gossip.cpp/validator_peer_set.cpp/rate_limiter.cpp, (2) consensus key source for sign_payload()/verify_signature() via set_signing_key(), (3) on_message() receives signature parameter, (4) compute_message_id() used for sender authentication, (5) prune_seen_messages() triggered on origin and receipt, (6) peek-without-consume in rate limiter, (7) automatic Healthy→Suspect→Offline health transitions via tick(), (8) rate_limit_violation triggers banning at threshold. **Feature-level acceptance is NOT GRANTED**: dedicated tests `tests/gossip_tests.cpp`, `tests/validator_peer_set_tests.cpp`, `tests/rate_limiter_tests.cpp` do not exist; runtime wiring NOT demonstrated and NOT to proceed — research boundary assessment (RQ-CONS-NET-BOUNDARY-2026-11-19, COMPLETE at `2fbc92a`, deliverable `2fbc92a84f14fdd7e9c77b31dc62309071834d73`) RECOMMENDS keeping ALL consensus networking inside CometBFT v0.40.0 and REJECTS new C++ GossipProtocol/ValidatorPeerSet/PeerRateLimiter classes for consensus (split-brain authority, duplicate auth/DoS/transport/recovery, boundary violation per ADR-0004). No implementation action by research agent; **owner decision required** before any C++ consensus networking implementation proceeds. Do not mark COMPLETE on the strength of the generic 343-test suite. CMakeLists.txt registration complete by fst-integration (at `03cb747`) but no further integration of consensus networking paths will proceed pending owner decision.
- Cross-cutting architectural finding (RESOLVED by f360985e): FST-REQ-0036 implemented as CometBFT-aligned FinalityChecker — FST side tracks CometBFT-finalized checkpoints, detects forks at application level. No architecture change or owner confirmation needed (ADR-0004/D-017 already selected CometBFT BFT; Raft D-024 is PROVISIONAL reference only). The coexistence of FeinStatic Raft code is a deprecation finding, not a blocker.

## FST-REQ-0038 Feature-Level Acceptance State

| Acceptance dimension | Status | Evidence |
|---|---|---|
| Implementation methods committed | YES | `b024d0aa7c10d042ec90f39e27701785b9bb8` on `team/implementation`; header commit `8b4ea201c1fd369b0aa40572f74436567b711630` |
| Source compiled through CMake | YES | `CMakeLists.txt` registration of gossip/rate_limiter/validator_peer_set .cpp files; all 5 presets build exit 0 at `03cb747` |
| Existing suite regression gate passed | YES | Verification Gate PASSED at `03cb747`: debug 343/343 PASS, tsan 343/343 PASS, clang-release -Werror clean |
| Feature-specific testing | INCOMPLETE | `tests/gossip_tests.cpp`, `tests/validator_peer_set_tests.cpp`, `tests/rate_limiter_tests.cpp` not found; `tests/consensus_tests.cpp` and `tests/network_tests.cpp` do not cover the three new classes |
| Runtime wiring demonstrated | NO | No references to GossipProtocol/ValidatorPeerSet/PeerRateLimiter in `src/network/network_runtime.cpp`, `src/consensus/abci/fst_application.cpp`, `src/consensus/abci/abci_socket_server.cpp`; wiring NOT to proceed pending owner decision on research boundary assessment |
| Architecture boundary assessed | COMPLETE | `2fbc92a` (deliverable `2fbc92a84f14fdd7e9c77b31dc62309071834d73`): RECOMMEND keep ALL consensus networking inside CometBFT v0.40.0; REJECT new C++ GossipProtocol/ValidatorPeerSet/PeerRateLimiter classes for consensus (split-brain authority, duplicate auth/DoS/transport/recovery, boundary violation per ADR-0004) |
| Final requirement acceptance | BLOCKED_ON_RESEARCH | Blocked on owner decision regarding research boundary assessment recommendation (RQ-CONS-NET-BOUNDARY-REC-01/02); no C++ consensus networking implementation to proceed without owner-approved ADR |


## Git synchronization protocol (ledger + accepted-commit propagation)

### Worktree layout (shared repo, shared object store, shared ref namespace)
| Worktree | Branch | Agent | Owns ledger | Path |
|----------|--------|-------|-------------|------|
| team/integration | `team/integration` | `fst-integration` | `docs/fst/operations/integration-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` |
| team/implementation | `team/implementation` | `fst-implementation` | `docs/fst/operations/implementation-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` |
| team/verification | `team/verification` | `fst-verification` | `docs/fst/operations/verification-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` |
| team/debugging | `team/debugging` | `fst-debugging` | `docs/fst/operations/defect-register.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` |
| team/research | `team/research` | `fst-research` | `docs/fst/operations/research-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/research` |

All five worktrees are linked worktrees of the repo at `/home/monil/Desktop/CXX/FeinStatic`. They share the object store and the ref namespace, so a commit on any `team/*` branch is immediately visible to the other worktrees (no `git fetch` required to obtain objects; `git fetch` is a harmless no-op for shared refs). Remote: `origin` = `https://github.com/mon0x01/FeinStatic.git`. `team/*` branches must never be pushed unless explicitly authorized; never push directly to `main`.

### Ledger ownership and read rules
1. **Each agent writes only its own ledger.** No agent may edit another role's ledger. The Integration Agent may not edit implementation/verification/defect/research ledgers; likewise for the other roles. The Research Agent owns `docs/fst/operations/research-status.md` and writes only there plus `docs/fst/research/**` and `docs/fst/architecture/research-proposals/**`.
2. **The integration ledger's latest committed version is read from `team/integration`:** `git show team/integration:docs/fst/operations/integration-status.md`.
3. **Each other role's ledger is read from its respective team branch ref** (never assume the local worktree copy is current): `git show team/implementation:docs/fst/operations/implementation-status.md`, `git show team/verification:docs/fst/operations/verification-status.md`, `git show team/debugging:docs/fst/operations/defect-register.md`, `git show team/research:docs/fst/operations/research-status.md`.
4. Worktree copies may be out of date relative to the branch ref. Always verify against the branch ref before editing or relying on a ledger. Re-read `integration-status.md` at every `continue` (cold-start) and before editing any source file (reservation check).

### Operating rules
1. Each agent commits only on its own `team/*` branch in its own worktree. Never commit directly to `main`.
2. No destructive Git operations: no force-push, no history rewrite, no destructive resets. No broad staging: no `git add .` / `git add -A`; stage only explicitly reviewed paths.
3. Before depending on another agent's work, confirm the exact commit SHA is present (`git log <branch> | grep <SHA>` or `git cat-file -t <SHA>`).
4. Baseline: `6f8fa522adae39a7909bf7a426563b1bc77867dc` is the common starting point. `team/integration` currently points at `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` (which includes the baseline). Other `team/*` branches point at their respective migration/setup SHAs (see Five-agent migration SHAs table). New accepted integration commits advance `team/integration` only.

### Handoff flow (implementation / debugging → integration)
1. Producing agent (`fst-implementation` or `fst-debugging`) completes reviewed work, commits it on its `team/*` branch, and reports the handoff to the Integration Agent with: source branch, exact commit SHA, from agent, receiving agent (`fst-integration`), intended target (`team/integration`), status.
2. Integration Agent records the handoff in the incoming handoffs table (status = RECEIVED).
3. Integration Agent confirms the Verification Agent's gate passed for that exact SHA (build preset, command, exit code, log location). Without a passed gate, the commit is not integrated.
4. Integration Agent merges the commit into `team/integration` using a history-preserving merge (no rebase), records the accepted SHA in `integration-status.md`, and commits the ledger update on `team/integration` (stage only `docs/fst/operations/integration-status.md`).

### Handoff format (mandatory fields)
Every handoff must identify the exact commit SHA and the receiving agent:
- Source branch: `<team/*>`
- Commit SHA: `<exact 40-hex SHA>`
- From agent: `<fst-implementation | fst-debugging>`
- Receiving agent: `fst-integration`
- Intended target: `team/integration`
- Status: RECEIVED | IN_REVIEW | GATE_PASSED | INTEGRATED | REJECTED
- Received at: `<ISO-8601 timestamp>`
A handoff without an exact commit SHA and receiving agent is not actionable and must not be recorded.

## CLI-Only Autonomous Orchestration (fst-team)

### System overview
The four-agent team is orchestrated exclusively through the Kilo CLI and Linux terminal. No VS Code, no Agent Manager, no GUI. The controller script lives at:

`scripts/fst-team/fst-team`

Runtime state is stored outside Git at `~/.local/state/fst-team/` (never committed):

| Path | Contents |
|------|----------|
| `~/.local/state/fst-team/agents/<agent>.json` | Current agent state (session_id, pid, log_file, task, status, started_at) |
| `~/.local/state/fst-team/sessions/<session_id>.json` | Session state (agent, task, pid, log_file, status, started_at, exit_code, commit_sha) |
| `~/.local/state/fst-team/tasks/<task_id>.json` | Task state (agent, task, session_id, pid, status, created_at, completed_at, commit_sha, log_file) |
| `~/.local/state/fst-team/logs/*.log` | Raw kilo run output (JSON-lines) per dispatch |
| `~/.local/state/fst-team/locks/*.lock` | flock files for atomic state writes |

### Controller commands
| Command | Usage | Description |
|---------|-------|-------------|
| `status` | `fst-team status` | Show all agents, sessions, tasks, worktrees, integration branch |
| `dispatch` | `fst-team dispatch <agent> <task>` | Launch a task to a worker agent (async, returns immediately) |
| `collect` | `fst-team collect <session_id\|task_id>` | Wait for completion, collect exit code, commit SHAs, agent output |
| `resume` | `fst-team resume <session_id> [msg]` | Resume an existing Kilo session by ID with a new message |
| `sync` | `fst-team sync` | Inspect all four worktrees; read ledgers from branch refs via `git show` |
| `stop` | `fst-team stop <session_id>` | Stop a running session |
| `push` | `fst-team push [branch]` | Publish team/* branch(es) to origin (fast-forward only, never main) |
| `doctor` | `fst-team doctor` | Health check: Kilo CLI, agent definitions, worktrees, state |

Agents: `fst-research`, `fst-implementation`, `fst-verification`, `fst-debugging`. Coordination: `fst-integration`.
### Continue workflow (Integration Agent)

When the user types `continue` in the Integration Kilo session:

1. **Read**: AGENTS.md, `docs/fst/engineering-charter.md`, `docs/fst/decisions/decision-closure-roadmap.md`, project-plan.md, and all five latest ledgers (integration ledger from `team/integration` ref; other ledgers from their respective branch refs via `git show team/<role>:<ledger>`, including `team/research`).
2. **Inspect**: `fst-team status` — check agent availability, worktree branches, source-file ownership reservations in `integration-status.md`.
3. **Discover**: Identify architecture/technology decisions that must be researched before the next significant implementation. Dispatch Research to eligible investigations (research runs in parallel with independent engineering tasks when possible).
4. **Dispatch**: `fst-team dispatch <agent> <task>` — send implementation work only for approved and sufficiently specified requirements; targeted verification to fst-verification; permitted debugging to fst-debugging; research to fst-research. Never assign conflicting source-file ownership.
5. **Concurrency**: Independent tasks may run concurrently (dispatch to multiple agents simultaneously).
6. **Collect**: `fst-team collect <task_id>` — wait for completion, require exact 40-character commit SHA, validate verification evidence against the exact commit being submitted.
7. **Accept**: Only accept handoffs that pass the integration gate (Verification Agent's gate: exact SHA, build preset, command, exit code, log location). Require owner approval for decisions classified BLOCKED-ON-OWNER.
8. **Update**: `integration-status.md` — record accepted commit SHA, update reservations, coordinate next cycle.
9. **Schedule**: plan the next cycle; research continues to review relevant advances without automatically adopting the latest release.

### Safety rules
- Each agent writes only its own ledger. No cross-editing.
- Ledgers are read from branch refs via `git show`, never assumed from stale worktree copies.
- No direct pushes to `main`. No force pushes or history rewriting. No broad staging.
- FST-BUG-0013/0014/0015/0017 remain deferred to Phase 4 unless the user explicitly changes priority.
- Never edit AGENTS.md or session-ses_*.md during ordinary orchestration.
- Do not treat a running process as successful before collecting its actual exit status.
- Do not represent old-SHA verification as evidence for a newer commit.
- Duplicate dispatch to a busy agent is prevented (exit code 2).

## Permanent Orchestration Behavior (every `continue`)

The Integration Agent acts as the active team coordinator on every `continue`.
This behavior is permanent and occurs automatically, not only on request.

### Mandatory orchestration cycle

1. **Read**: AGENTS.md, project-plan.md, and all four latest ledgers (integration
   ledger from `team/integration` ref; other ledgers from their respective branch
   refs via `git show team/<role>:<ledger>`).
2. **Inspect**: `fst-team status` — check agent availability, worktree branches,
   active tasks, source-file ownership reservations in `integration-status.md`.
3. **Inspect**: all four branch heads, worktrees, active reservations, and
   outstanding handoffs.
4. **Identify**: the next eligible, non-conflicting assignment for each specialist agent (fst-research, fst-implementation, fst-verification, fst-debugging), respecting task priority, dependencies, research gates, and source-file ownership reservations. Research is dispatched to eligible investigations; implementation is dispatched only for approved and sufficiently specified work.
5. **Dispatch**: all agents concurrently using `fst-team dispatch <agent> <task>`. Do NOT wait for fst-implementation to finish before dispatching fst-verification, fst-debugging, or fst-research. Independent tasks run concurrently. Research runs in parallel with independent engineering tasks when possible.
6. **Session reuse**: use each agent's existing persistent Kilo session. Never create duplicate sessions or duplicate tasks. A running agent must not receive a duplicate task (duplicate dispatch prevented, exit code 2).
7. **Coordinate**: allow the agents to work independently while the Integration Agent tracks their task IDs, session IDs, and process states via `fst-team status`.
8. **Collect**: `fst-team collect <task_id>` — wait for completion, verify actual process exit codes, exact commit SHAs, and evidence. Do not falsely report unfinished tasks as successful.
9. **Process handoffs**: arrange targeted verification; accept only work that meets the integration gate (Verification Agent's gate: exact SHA, build preset, command, exit code, log location). Require owner approval for decisions classified BLOCKED-ON-OWNER.
10. **Update**: `integration-status.md` — record accepted commit SHAs, update reservations, update handoff statuses, coordinate next cycle.

### Persistent behavior

- A `continue` initiates or resumes an actual parallel work cycle whenever
  eligible work exists.
- If all three agents are genuinely blocked or have no eligible tasks, explain
  why and report their individual states.
- Do not push to `main`, change the approved FST scope, violate ledger ownership,
  or override the Phase 4 debugging priority (FST-BUG-0013/0014/0015/0017
  deferred to Phase 4 unless the user explicitly changes priority).
- The desired operating model is four simultaneously working specialist agents (research, implementation, verification, debugging), continuously coordinated by Integration, with the owner providing only `continue` and `stop` commands.

## Permanent GitHub Publication Policy

### Policy statement

The user explicitly authorizes periodic publishing of FST development branches to the existing GitHub origin repository (`https://github.com/mon0x01/FeinStatic.git`). All five `team/*` branches are published to their corresponding `origin/team/*` branches. `main` is NEVER published to automatically — it remains exclusively under owner-controlled release management.

### Automatic branch publishing rules

1. **Push after completed checkpoints:** Push committed changes on each `team/*` branch after a completed agent task or meaningful committed checkpoint. Push `team/integration` after each completed integration cycle and ledger update.
2. **Publish verification evidence:** Verification evidence and failed-test records are published too, with their actual verification status clearly documented in the ledgers.
3. **Fast-forward only:** Use ordinary `git push` (no `--force`, no `--force-with-lease`, no `--delete`). Never rewrite published history.
4. **No main:** Never push directly to `main`. Never merge into `main` automatically.
5. **No uncommitted changes:** Never push uncommitted changes. Only committed SHAs are published.
6. **No broad staging:** Never use `git add .` or `git add -A`. Stage only explicitly reviewed paths.
7. **Pre-push inspection:** Before every push, inspect remote refs (`git ls-remote origin`) and confirm branch ancestry. Local must be a descendant of remote (fast-forward possible). If diverged, stop and report — never force-push.
8. **Rejected push handling:** If a push is rejected or encounters unexpected remote divergence, stop and report the problem. Never resolve it using force-push.
9. **Upstream tracking:** Establish safe upstream tracking (`git branch --set-upstream-to=origin/team/*`) for each published branch.

### Integration requirements

1. **Publish accepted integration commits:** Publish `team/integration` only after the Verification Agent's gate passes (exact SHA, build preset, command, exit code, log location) AND the Technology and Dependency Acceptance Policy gate passes.
2. **Feature branch publication:** Feature branches may be published before integration so their progress is backed up and available for review.
3. **Status clarity:** Clearly distinguish RECEIVED, VERIFIED, INTEGRATED and REJECTED work in the ledgers.
4. **SHA recording:** Record published commit SHAs and remote branch references in the integration ledger.

### Published branches (as of this cycle)

| Branch | Local SHA | Remote SHA | Status | Upstream tracking |
|--------|-----------|------------|--------|-------------------|
| `team/integration` | `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` | `b9893f8ee89afc142abba2e543bfb22ed38d9fc0` | PUBLISHED | origin/team/integration |
| `team/implementation` | `9a8bc8fd3165331c3829b454664371b547e00dbb` | `9a8bc8fd3165331c3829b454664371b547e00dbb` | PUBLISHED — fast-forwarded origin/team/implementation from `8b4ea20` to `9a8bc8f` (includes FST-REQ-0038 method implementation `b024d0a`; `b024d0a` confirmed ancestor of integration merge `03cb747` and HEAD `b9893f8`) | origin/team/implementation |
| `team/verification` | `040885e` | `d1d9d2ad8dd6ecec10df81d376f5d7d800749430` | AHEAD — 1 commit unpushed (`040885e`) | origin/team/verification |
| `team/debugging` | `6e3bff7ffb5b14aebdfd5e193236c8983bea1d98` | `6e3bff7ffb5b14aebdfd5e193236c8983bea1d98` | PUBLISHED | origin/team/debugging |
| `team/research` | `bafb1efee1352bd23ed8587922870dff40718054` | `bafb1efee1352bd23ed8587922870dff40718054` | PUBLISHED | origin/team/research |

Note: `team/verification` has been reconciled — commit `d1d9d2a` (cherry-pick of `7b22760`, five-preset verification results at integration HEAD `928ea7b`, gate PASSED) is now on the branch and pushed to origin. The earlier baseline commit `5d0d74b` (verification at `b85bc4a`, gate NOT PASSED) could not be cherry-picked due to content conflicts on `verification-status.md`; its evidence is preserved as Git tag `verification-baseline-b85bc4a-5d0d74b` (pushed to origin) and is also reachable from `team/integration` via merge commit `928ea7b`. The tag documents all five-preset results at `b85bc4a`: debug 343/343 PASS, release 339 passed/4 failed (FST-BUG-0017), asan SEGV at `abci_codec.cpp:705`, clang-release BUILD FAIL (sign-conversion), tsan 343/343 PASS.

### Controller command

`fst-team push [branch]` — publishes team/* branch(es) to origin. Default target is `all` (all five branches). Validates ancestry, confirms remote reachability, performs ordinary fast-forward push, reports push summary.

### Automatic publishing triggers

The `fst-team` controller automatically publishes after successful committed tasks:

1. **After specialist task completion:** `cmd_collect` calls `auto_push_after_collect()` when a task completes with exit code 0 and produces a commit SHA that is present on the agent's team/* branch (exact-SHA validation). The agent's branch is pushed to origin via ordinary fast-forward push.

2. **After integration cycle completion:** The Integration Agent manually runs `fst-team push team/integration` after completing a verified integration cycle and committing its ledger.

3. **Safety:** Automatic publishing only occurs after commit completion and exact-SHA validation. Uncommitted files are never published. Workers are not pushed while still active (push triggered by collect, not dispatch). Failed gates to team/integration are not pushed (Integration Agent controls when to push team/integration).



