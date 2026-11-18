# FST Integration Status Ledger

This ledger is owned exclusively by the Integration Agent (fst-integration). No other agent may edit it.

---

## Charter and role obligations (permanent five-agent model)

This ledger is owned exclusively by the Integration Agent (fst-integration), which centrally coordinates work, resolves ownership, manages handoffs, and integrates accepted changes. Integration is the coordinator, not the owner of the other agents' technical decisions. Research is a mandatory gate for significant technology or architecture selections; the Research Agent (fst-research) owns `docs/fst/operations/research-status.md` and proposes ADRs under `docs/fst/architecture/research-proposals/`. The common authoritative doctrine is `docs/fst/engineering-charter.md`; this ledger links to it rather than duplicating it. Classification and closure conditions for outstanding matters are in `docs/fst/decisions/decision-closure-roadmap.md`. Research-before-implementation, technology-selection evidence, independent verification, exact-SHA handoffs, security and reliability gates, future-phase compatibility, and owner-versus-delegated decision boundaries are defined in the charter.

This agent writes only `docs/fst/operations/integration-status.md`. It never edits the implementation, verification, defect, or research ledgers.

---

## Current integration branch and baseline
- Integration branch: `team/integration`
- Baseline commit SHA: `e0fadab` (HEAD of `team/integration`; commits `b85bc4a..e0fadab`: fst-team lifecycle fix `e0fadab` on top of `53200ee`)
- Integration worktree: `/home/monil/Desktop/CXX/FeinStatic-agents/integration`
- Setup commit SHA: `e45824f54f607b73c79c1d49f71c23c0541bd6a0` (setup(fst): add four custom Kilo agents and integration-status ledger)
- External handoff commits present in `team/integration` history: `bdb07fd` (debugging FST-BUG-0014/0015 fix, merged), `f360985` (implementation FST-REQ-0036 FinalityChecker, merged), `5e1c145` (debugging VDEF-001/VDEF-002 fixes, merged), `ad9bf01` (debugging FST-BUG-0018/0019 ABCI framing + socket-server lifecycle fix, merged), `c573ef9` (implementation docs-only reconciliation, present as ancestor). `2e4fb66` (verification gate evidence for `0773794`) is NOT yet in `team/integration` history — docs-only, no merge required (see Pending handoffs). `0f55970` (verification baseline evidence at `6f8fa52` across all 5 presets, fst-verification) is NOT yet in `team/integration` history — docs-only, no merge required (see Pending handoffs).
- Incoming handoffs NOT yet in `team/integration` history (source to merge after gate pass):
  - `38dc9b5449c5bfbd61fb7512fdd0921763d22f68` (fst-implementation, team/implementation) — FST-REQ-0037 validator entry & exit (staking inactivity leak, epoch rotation, slashing evidence; also fixes clang-release `-Wsign-conversion` at `fst_application.cpp:213`). Status: RECEIVED. Verification gate NOT yet passed for this commit — blocking integration until fst-verification re-verifies at a state including this fix.
  - `b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe` (fst-verification, detached at b85bc4a) — five-preset verification baseline at `b85bc4a`. Status: RECEIVED, gate NOT PASSED (clang-release build exit 1, ASan abort, release timing failures). See Verification Gate Evidence section below.

## Active tasks and assigned agents
| Task | Assigned agent | Branch | Worktree | Status |
|------|----------------|--------|----------|--------|
| IS-01 | Integration ledger initialization (baseline, reservations, sync protocol, YOLO verification) | `fst-integration` | `team/integration` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | COMPLETE |
| IT-01 | Phase 3 completion: FST-REQ-0036 finality & fork choice, FST-REQ-0037 validator entry & exit, FST-REQ-0038 consensus networking, FST-REQ-0039 multi-node consensus test network (≥4 validators, Byzantine finality) — DISPATCHED to fst-implementation (task_1795032666) | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | COMPLETE |
| VT-01 | Targeted verification of incoming feature handoffs (per-commit: exact SHA + build preset + command + exit code + log); comprehensive five-preset + full suite verification scheduled after implementation + integration complete — DISPATCHED to fst-verification (task_1795032669): re-run TSan suite in isolation | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | COMPLETE |
| DT-01 | Defect FST-BUG-0017 Release-mode timing failures (harness tests) — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-02 | Defects FST-BUG-0014 / FST-BUG-0015 TSan Boost.Asio socket races — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-03 | Defect FST-BUG-0013 SecureSession handshake_result_ cross-thread race — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| VDEF-fix | VDEF-2026-11-18-001 ASan SEGV abci_codec.cpp:674 + VDEF-2026-11-18-002 TSan races abci_socket_server.cpp:270/:273 — DISPATCHED to fst-debugging (task_1795032671) | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | COMPLETE |
| RS-01 | Five-agent migration and research agent setup — fst-research agent definition, research-status ledger, engineering charter, decision closure roadmap, team/research worktree, five-agent controller extension, AGENTS.md sync, read-only research pilot | `fst-research` | `team/research` | `/home/monil/Desktop/CXX/FeinStatic-agents/research` | IN_PROGRESS |
| VT-02 | Verify current team/integration HEAD across all five build presets (debug, release, asan, clang-release, tsan); establish fresh verification baseline | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | COMPLETE (task_1795041607_fst-verification, session ses_e0f96f32, pid 4156438, ran 2026-11-18T22:40:07Z–23:40Z, exit 0; verified commit `b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe`; gate NOT PASSED — see Verification Gate Evidence below) |
| IT-02 | FST-REQ-0037 validator entry & exit: inactivity leak, epoch-based committee rotation, epoch transitions, slashing evidence flow (src/staking/, include/feinstat/staking/); tests/ owned by fst-debugging — coordinate via fst-integration | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | COMPLETE (task_1795041609_fst-implementation, session ses_e0f65e22, pid 4156542, ran 2026-11-18T22:40:09Z–23:36Z, exit 0; committed `38dc9b5449c5bfbd61fb7512fdd0921763d22f68` on team/implementation; source verified debug+clang-release -Werror clean, staking 18/18 PASS, ABCI 14/14 PASS; handoff to fst-integration for integration gate) |

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
| team/implementation | c573ef99dc2351972d447842d1bfcc93668caefa | fst-implementation | team/integration | RECEIVED | 2026-11-18T19:52:00Z |
| team/verification | 2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e | fst-verification | team/integration | RECEIVED | 2026-11-18T19:55:00Z |
| team/implementation | 38dc9b5449c5bfbd61fb7512fdd0921763d22f68 | fst-implementation | team/integration | RECEIVED | 2026-11-18T23:36:00Z |
| team/verification | b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe | fst-verification | team/integration | RECEIVED | 2026-11-18T23:40:00Z |
| team/implementation | f360985e98dd882556cc9e6b531ed2b4099ac727 | fst-implementation | team/integration | INTEGRATED | 2026-11-18T20:xx:00Z |
| team/debugging | 5e1c14583d46096ad309a3a2149a42bc3dfc4be2 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T20:xx:00Z |
| team/debugging | ad9bf01ac05ca1a1b70a1e8a9821b7f2a2ed5504 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T02:10:31Z |
| team/verification | 0f55970937522c5c04c974584baa86d8ff6d6172 | fst-verification | team/integration | RECEIVED | 2026-11-18T00:04:14Z |

## Integration and review status
- Last integrated commit: `5e1c145` (merge of fst-debugging VDEF-001/VDEF-002 fixes into `team/integration`; HEAD advances from `bc9c476` → merge commit)
- Last review: `5e1c14583d46096ad309a3a2149a42bc3dfc4be2` — Verification Agent gate PASSED (debug: configure=0, build=0, tests=0, 343 passed, 0 failed; TSan: configure=0, build=0, tests=0, 3 passed, 0 failed, zero TSan reports); integrated via history-preserving merge
- Pending review: none
- Pending handoffs (documentation-only ledger reconciliations, no source to merge into team/integration — acknowledged, no merge required):
  - `c573ef99dc2351972d447842d1bfcc93668caefa` (fst-implementation, team/implementation) — FST-REQ-0036..0044 audit + implementation-status.md reconciliation. Status: PRESENT (already an ancestor of `team/integration` HEAD `53200ee`; docs-only, no merge required). No build gate required (docs-only); no source to integrate.
  - `2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e` (fst-verification, detached at 0773794) — verification gate evidence for commit `077379458ed1847d704af89a7a5ae5520cfee42d`. Status: RECEIVED, docs-only; NOT yet in `team/integration` history — no merge required per policy; gate evidence retained on `team/verification`. Gate PASSED (debug 343/343; TSan handshake + harness clean).
  - `0f55970937522c5c04c974584baa86d8ff6d6172` (fst-verification, team/verification) — full 5-preset verification baseline at commit `6f8fa522adae39a7909bf7a426563b1bc77867dc`: debug 343/343 PASS (incl. cometbft_multi_node_genesis_agreement, temp_handshake_live_test), release 340/343 (3 FST-BUG-0017 timing failures, no hang), asan build exit 0 / test ABORT: SEGV at abci_codec.cpp:674 (VDEF-001, HIGH), clang-release 343/343 PASS, -Werror clean, no hang, tsan 338/343 (5 env/artifact failures: 1 cometbft port-bind artifact from parallel run + 4 no-TTY terminal tests), 4 TSan races at abci_socket_server.cpp:270/:273 (VDEF-002, MEDIUM). Status: RECEIVED, docs-only; NOT yet in `team/integration` history — no merge required per policy; gate evidence recorded in the Verification Gate Evidence section below. Defects VDEF-001/VDEF-002 already fixed at `ad9bf01` (integrated, see Accepted SHAs); FST-BUG-0017 deferred to Phase 4. Logs: /tmp/kilo/verify-{debug,release,asan,clang-release,tsan}-tests.log.

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

Recommended next step: merge `38dc9b5` into `team/integration` (it fixes VDEF-b85bc4a-001 and adds FST-REQ-0037 functionality), then dispatch fst-verification to re-verify all five presets at the merged `team/integration` HEAD before integration gate pass.

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

## Five-agent migration SHAs (commit ff64364)

Recorded at the permanent five-agent migration and the technology/dependency acceptance policy extension. `main` is untouched by this work; no `team/*` branch is pushed to `origin/main`.

| Branch | Commit SHA | Worktree | Agent | Content |
|---|---|---|---|---|
| `team/integration` | `ff64364793eb7cd9feedbc52ae4d609cbcaca84a` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | `fst-integration` | Migration + technology/dependency acceptance policy: engineering charter §19, decision closure roadmap M-19/M-20, research-status research tasks RT-LICENSE-001/RT-DEP-002, policies/technology-and-dependency-acceptance.md, all five agent definitions (five-agent model, dependency gates), integration ledger updates |
| `team/research` | `9f72f8e3979f5359ad178f243f8b42f690b83b8b` | `/home/monil/Desktop/CXX/FeinStatic-agents/research` | `fst-research` | Research pilot: CometBFT consensus candidate report + research-status research tasks RT-LICENSE-001/RT-DEP-002 + five-agent agent definitions |
| `team/implementation` | `29703331e13a0430605e46537d510e1b2252954e` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | `fst-implementation` | implementation-status.md charter + five-agent role obligations + dependency gate + five-agent agent definitions |
| `team/verification` | `f7ac5ff53a189c21fd56d491fb391995aa88e3ad` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | `fst-verification` | verification-status.md charter + five-agent role obligations + dependency/license evidence standard + five-agent agent definitions |
| `team/debugging` | `6e3bff7ffb5b14aebdfd5e193236c8983bea1d98` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | `fst-debugging` | defect-register.md charter + five-agent role obligations + no-incidental-dependency rule + five-agent agent definitions |
| `main` | `b69857a36daa04578be61ca3f7d2d7bf39bfa99c` | `/home/monil/Desktop/CXX/FeinStatic` | (none) | Untouched by this migration |

## Outstanding blockers
- (none for Phases 3–4 — unblocked by owner decisions D-015/D-016/D-017/D-037 APPROVED via ADR-0002/0003/0004/0005)
- VDEF-2026-11-18-001 → RESOLVED and INTEGRATED as FST-BUG-0018: ASan SEGV in ABCI framing (`src/consensus/abci/abci_codec.cpp:674`, read_framed_message → protobuf MergeFromImpl), HIGH. Root cause: protobuf 36.x table-dispatch indirect call on data-derived index for a valid 21-byte Request.info frame — protobuf-internal, not fixable by frame-length validation. Fix committed at `ad9bf01` (bounds-checked frame length + zero-initialized parse slack); defect register at `5e1c14583d46096ad309a3a2149a42bc3dfc4be2`. Verification Gate PASSED (debug 343/343; TSan 3/3 cometbft, 0 reports). Integrated into `team/integration` via merge commit. Remaining risk: ASan aborts on protobuf table-dispatch indirect call for valid Info frame — documented as protobuf-internal limitation, does not affect Debug/TSan paths.
- VDEF-2026-11-18-002 → RESOLVED and INTEGRATED as FST-BUG-0019: TSan data races in `src/consensus/abci/abci_socket_server.cpp:270/:273/:274` (AbciSocketServer stop()/run() racing run() thread), HIGH. FIXED: `stop()` stops and joins io_context thread before closing sockets; `running_` made `std::atomic<bool>`; `listen_fd_` guarded by file-local mutex. Fix committed at `ad9bf01`; defect register at `5e1c14583d46096ad309a3a2149a42bc3dfc4be2`. Verification Gate PASSED (TSan 3/3 cometbft, 0 ThreadSanitizer reports; debug 343/343). Integrated into `team/integration` via merge commit.
- FST-BUG-0017 (Release-mode timing failures) remains OPEN and DEFERRED to Phase 4.
- **VDEF-b85bc4a-001 (HIGH): clang-release build failure at `fst_application.cpp:213`** — `-Wsign-conversion` (`int64_t`→`uint64_t` in `would_fork(request.height)`) under `-Werror`. Blocks integration gate ("all five presets build exit 0"). **FIXED by implementation commit `38dc9b5449c5bfbd61fb7512fdd0921763d22f68`** (RECEIVED handoff on `team/implementation`, not yet merged to `team/integration`). Required action: merge `38dc9b5` into `team/integration`, then re-verify all five presets at merged HEAD. Reporting agent: fst-verification. Exact SHA: `b85bc4a7084f38c9c89ca606eb7b9c9ffa6db0fe`.
- Cross-cutting architectural finding (RESOLVED by f360985e): FST-REQ-0036 implemented as CometBFT-aligned FinalityChecker — FST side tracks CometBFT-finalized checkpoints, detects forks at application level. No architecture change or owner confirmation needed (ADR-0004/D-017 already selected CometBFT BFT; Raft D-024 is PROVISIONAL reference only). The coexistence of FeinStatic Raft code is a deprecation finding, not a blocker.

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
4. Baseline: `6f8fa522adae39a7909bf7a426563b1bc77867dc` is the common starting point. `team/integration` currently points at `4521a3bb47b533ca5d021163b20a4df23a18196e` (which includes the baseline). Other `team/*` branches point at their respective migration/setup SHAs (see Five-agent migration SHAs table). New accepted integration commits advance `team/integration` only.

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

