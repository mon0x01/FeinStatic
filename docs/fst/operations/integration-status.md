# FST Integration Status Ledger

This ledger is owned exclusively by the Integration Agent (fst-integration). No other agent may edit it.

## Current integration branch and baseline
- Integration branch: `team/integration`
- Baseline commit SHA: `a89ccd8` (HEAD of `team/integration`; merge of `bdb07fd` fst-debugging FST-BUG-0014/0015 fix; prior baseline `4c5e0880478c4e33eae9ae00cd9fe84ac420b0df` is an ancestor, verified)
- Integration worktree: `/home/monil/Desktop/CXX/FeinStatic-agents/integration`
- Setup commit SHA: `e45824f54f607b73c79c1d49f71c23c0541bd6a0` (setup(fst): add four custom Kilo agents and integration-status ledger)

## Active tasks and assigned agents
| Task | Assigned agent | Branch | Worktree | Status |
|------|----------------|--------|----------|--------|
| IS-01 | Integration ledger initialization (baseline, reservations, sync protocol, YOLO verification) | `fst-integration` | `team/integration` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | COMPLETE |
| IT-01 | Phase 3 completion: FST-REQ-0036 finality & fork choice, FST-REQ-0037 validator entry & exit, FST-REQ-0038 consensus networking, FST-REQ-0039 multi-node consensus test network (≥4 validators, Byzantine finality) — DISPATCHED to fst-implementation (task_1795032666) | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | IN_PROGRESS (dispatched) |
| VT-01 | Targeted verification of incoming feature handoffs (per-commit: exact SHA + build preset + command + exit code + log); comprehensive five-preset + full suite verification scheduled after implementation + integration complete — DISPATCHED to fst-verification (task_1795032669): re-run TSan suite in isolation | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | IN_PROGRESS (dispatched) |
| DT-01 | Defect FST-BUG-0017 Release-mode timing failures (harness tests) — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-02 | Defects FST-BUG-0014 / FST-BUG-0015 TSan Boost.Asio socket races — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-03 | Defect FST-BUG-0013 SecureSession handshake_result_ cross-thread race — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| VDEF-fix | VDEF-2026-11-18-001 ASan SEGV abci_codec.cpp:674 + VDEF-2026-11-18-002 TSan races abci_socket_server.cpp:270/:273 — DISPATCHED to fst-debugging (task_1795032671) | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | IN_PROGRESS (dispatched) |

## Source-file ownership reservations
| File/path | Reserved for | Reserved by | Active task | Since |
|-----------|--------------|-------------|-------------|-------|
| include/feinstat/consensus/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| src/consensus/** (excluding abci/abci_codec.cpp and abci/abci_socket_server.cpp) | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| src/consensus/abci/abci_codec.cpp | VDEF-2026-11-18-001 fix | `fst-debugging` | VDEF fix (IN_PROGRESS) | 2026-11-18 |
| src/consensus/abci/abci_socket_server.cpp | VDEF-2026-11-18-002 fix | `fst-debugging` | VDEF fix (IN_PROGRESS) | 2026-11-18 |
| include/feinstat/staking/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| src/staking/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| include/feinstat/monetary/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| src/monetary/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| tests/abci_tests.cpp | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| tests/cometbft_integration_tests.cpp | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| tests/network_harness_tests.cpp | Defect FST-BUG-0017 (DT-01) | `fst-debugging` | DT-01 (DEFERRED) | 2026-11-18 |
| tests/network_harness.cpp | Defect FST-BUG-0017 (DT-01) | `fst-debugging` | DT-01 (DEFERRED) | 2026-11-18 |
| src/network/secure_session.cpp | Defects FST-BUG-0013 / FST-BUG-0014 / FST-BUG-0015 (DT-02 / DT-03) | `fst-debugging` | DT-02 / DT-03 (DEFERRED) | 2026-11-18 |
| include/feinstat/network/secure_session.hpp | Defect FST-BUG-0013 (DT-03) | `fst-debugging` | DT-03 (DEFERRED) | 2026-11-18 |
| tests/temp_handshake_test.cpp | Defect FST-BUG-0013 (DT-03) | `fst-debugging` | DT-03 (DEFERRED) | 2026-11-18 |
| src/network/network_runtime.cpp | Defects FST-BUG-0014 / FST-BUG-0015 (DT-02) | `fst-debugging` | DT-02 (DEFERRED) | 2026-11-18 |
| docs/fst/operations/defect-register.md | Defect tracking (all DT tasks) | `fst-debugging` | DT-01 / DT-02 / DT-03 (DEFERRED) | 2026-11-18 |

## Incoming branch/commit handoffs
| Source branch | Commit SHA | From agent | Intended target | Status | Received at |
|---------------|------------|------------|-----------------|--------|-------------|
| team/debugging | bdb07fd482fc2094487fa1407f33e1397973dc99 | fst-debugging | team/integration | INTEGRATED | 2026-11-18T19:31:45Z |
| team/implementation | c573ef99dc2351972d447842d1bfcc93668caefa | fst-implementation | team/integration | RECEIVED | 2026-11-18T19:52:00Z |
| team/verification | 2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e | fst-verification | team/integration | RECEIVED | 2026-11-18T19:55:00Z |

## Integration and review status
- Last integrated commit: `a89ccd8` (merge commit merging `bdb07fd` fst-debugging FST-BUG-0014/0015 fix into `team/integration`; HEAD advances from `4c5e088` → `a89ccd8`)
- Last review: `bdb07fd` — Verification Agent gate PASSED; integrated via merge commit `a89ccd8`
- Pending review: none
- Pending handoffs (documentation-only ledger reconciliations, no source to merge into team/integration):
  - `c573ef99dc2351972d447842d1bfcc93668caefa` (fst-implementation, team/implementation) — FST-REQ-0036..0044 audit + implementation-status.md reconciliation. RECEIVED; no build gate required (docs-only); no source to integrate.
  - `2e4fb6636faf4e9e99126bb98b47a9cd145e4a6e` (fst-verification, detached at 0773794) — verification gate evidence for commit `077379458ed1847d704af89a7a5ae5520cfee42d`. RECEIVED; gate PASSED (see evidence below); no source to integrate.

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

## Build and verification gates
- Build presets: `debug`, `release`, `asan`, `clang-release`, `tsan` (CMakePresets.json)
- Build gate: all five presets must compile with exit code 0; debug suite 343/343 PASS
- Verification gate: Verification Agent reports exact commit SHA tested, build preset, command, exit code, and log location; gate passes only on PASS evidence
- Regression gate: not yet run (full unit + integration suite on each accepted integration commit)

## Accepted integration commit SHAs
- `6f8fa522adae39a7909bf7a426563b1bc77867dc` — prior integration baseline (accepted as starting point)
- `4c5e0880478c4e33eae9ae00cd9fe84ac420b0df` — orchestration commits 278fb5b, 501a97d, 8d0ca38, 4c5e088 on top of `6f8fa522`
- `bdb07fd482fc2094487fa1407f33e1397973dc99` — accepted and integrated (fst-debugging FST-BUG-0014/0015 fix; Verification Gate PASSED; merged via merge commit `a89ccd8`)
## Outstanding blockers

- (none for Phases 3–4 — unblocked by owner decisions D-015/D-016/D-017/D-037 APPROVED via ADR-0002/0003/0004/0005)
- VDEF-2026-11-18-001 → registered in defect-register.md by fst-debugging: ASan SEGV in ABCI framing (`src/consensus/abci/abci_codec.cpp:674`, read_framed_message → protobuf MergeFromImpl), HIGH, reproduced at SHA `0773794` (preset asan, exit 1, log /tmp/kilo/verify-0773794-asan-cometbft-single.log). Required action: fst-debugging to fix (Phase 4, not yet authorized). Reservation: `src/consensus/abci/abci_codec.cpp` assigned to fst-debugging (no conflict — IT-01 is QUEUED, not active).
- VDEF-2026-11-18-002 → registered in defect-register.md by fst-debugging: TSan data races in `src/consensus/abci/abci_socket_server.cpp:270/:273/:274` (AbciSocketServer stop()/run() racing run() thread), MEDIUM, reproduced at SHA `0773794` (preset tsan, exit 66, log /tmp/kilo/verify-0773794-tsan-cometbft-single.log). Required action: fst-debugging to fix (Phase 4, not yet authorized). Reservation: `src/consensus/abci/abci_socket_server.cpp` assigned to fst-debugging (no conflict — IT-01 is QUEUED, not active).
- FST-BUG-0017 (Release-mode timing failures) remains OPEN and DEFERRED to Phase 4.
- Cross-cutting architectural finding (from fst-implementation audit at c573ef9): Integrated consensus is FeinStatic Raft (crash-fault, D-024 PROVISIONAL) but ADR-0004/D-017 selected CometBFT (BFT). FST-REQ-0036 (BFT finality) cannot be satisfied by Raft. Owner confirmation may be needed before implementing FST-REQ-0036.

## Git synchronization protocol (ledger + accepted-commit propagation)

### Worktree layout (shared repo, shared object store, shared ref namespace)
| Worktree | Branch | Agent | Owns ledger | Path |
|----------|--------|-------|-------------|------|
| team/integration | `team/integration` | `fst-integration` | `docs/fst/operations/integration-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` |
| team/implementation | `team/implementation` | `fst-implementation` | `docs/fst/operations/implementation-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` |
| team/verification | `team/verification` | `fst-verification` | `docs/fst/operations/verification-status.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` |
| team/debugging | `team/debugging` | `fst-debugging` | `docs/fst/operations/defect-register.md` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` |

All four worktrees are linked worktrees of the repo at `/home/monil/Desktop/CXX/FeinStatic`. They share the object store and the ref namespace, so a commit on any `team/*` branch is immediately visible to the other worktrees (no `git fetch` required to obtain objects; `git fetch` is a harmless no-op for shared refs). Remote: `origin` = `https://github.com/mon0x01/FeinStatic.git`. `team/*` branches must never be pushed unless explicitly authorized; never push directly to `main`.

### Ledger ownership and read rules
1. **Each agent writes only its own ledger.** No agent may edit another role's ledger. The Integration Agent may not edit implementation/verification/defect ledgers; likewise for the other roles.
2. **The integration ledger's latest committed version is read from `team/integration`:** `git show team/integration:docs/fst/operations/integration-status.md`.
3. **Each other role's ledger is read from its respective team branch ref** (never assume the local worktree copy is current): `git show team/implementation:docs/fst/operations/implementation-status.md`, `git show team/verification:docs/fst/operations/verification-status.md`, `git show team/debugging:docs/fst/operations/defect-register.md`.
4. Worktree copies may be out of date relative to the branch ref. Always verify against the branch ref before editing or relying on a ledger. Re-read `integration-status.md` at every `continue` (cold-start) and before editing any source file (reservation check).

### Operating rules
1. Each agent commits only on its own `team/*` branch in its own worktree. Never commit directly to `main`.
2. No destructive Git operations: no force-push, no history rewrite, no destructive resets. No broad staging: no `git add .` / `git add -A`; stage only explicitly reviewed paths.
3. Before depending on another agent's work, confirm the exact commit SHA is present (`git log <branch> | grep <SHA>` or `git cat-file -t <SHA>`).
4. Baseline: `6f8fa522adae39a7909bf7a426563b1bc77867dc` is the common starting point. All `team/*` branches currently point at `501a97dc16abd4506c85055d1698a5d5472a618e` (which includes the baseline). New accepted integration commits advance `team/integration` only.

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

Agents: `fst-implementation`, `fst-verification`, `fst-debugging`.

### Continue workflow (Integration Agent)
When the user types `continue` in the Integration Kilo session:

1. **Read**: AGENTS.md, project-plan.md, and all four latest ledgers (read integration ledger from `team/integration` ref; read other ledgers from their respective branch refs via `git show team/<role>:<ledger>`).
2. **Inspect**: `fst-team status` — check agent availability, worktree branches, source-file ownership reservations in `integration-status.md`.
3. **Discover**: Identify next eligible tasks from project-plan.md respecting task priority and dependencies.
4. **Dispatch**: `fst-team dispatch <agent> <task>` — send implementation, targeted verification, and permitted debugging work to the correct agents. Never assign conflicting source-file ownership.
5. **Concurrency**: Independent tasks may run concurrently (dispatch to multiple agents simultaneously).
6. **Collect**: `fst-team collect <task_id>` — wait for completion, require exact 40-character commit SHA, validate verification evidence against the exact commit being submitted.
7. **Accept**: Only accept handoffs that pass the integration gate (Verification Agent's gate: exact SHA, build preset, command, exit code, log location).
8. **Update**: `integration-status.md` — record accepted commit SHA, update reservations, coordinate next cycle.

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
4. **Identify**: the next eligible, non-conflicting assignment for EACH of the
   three specialist agents (fst-implementation, fst-verification, fst-debugging),
   respecting task priority, dependencies, and source-file ownership reservations.
5. **Dispatch**: all three agents concurrently using `fst-team dispatch <agent>
   <task>`. Do NOT wait for fst-implementation to finish before dispatching
   fst-verification or fst-debugging. Independent tasks run concurrently.
6. **Session reuse**: use each agent's existing persistent Kilo session. Never
   create duplicate sessions or duplicate tasks. A running agent must not receive
   a duplicate task (duplicate dispatch prevented, exit code 2).
7. **Coordinate**: allow the three agents to work independently while the
   Integration Agent tracks their task IDs, session IDs, and process states via
   `fst-team status`.
8. **Collect**: `fst-team collect <task_id>` — wait for completion, verify actual
   process exit codes, exact commit SHAs, and evidence. Do not falsely report
   unfinished tasks as successful.
9. **Process handoffs**: arrange targeted verification; accept only work that
   meets the integration gate (Verification Agent's gate: exact SHA, build preset,
   command, exit code, log location).
10. **Update**: `integration-status.md` — record accepted commit SHAs, update
    reservations, update handoff statuses, coordinate next cycle.

### Persistent behavior

- A `continue` initiates or resumes an actual parallel work cycle whenever
  eligible work exists.
- If all three agents are genuinely blocked or have no eligible tasks, explain
  why and report their individual states.
- Do not push to `main`, change the approved FST scope, violate ledger ownership,
  or override the Phase 4 debugging priority (FST-BUG-0013/0014/0015/0017
  deferred to Phase 4 unless the user explicitly changes priority).
- The desired operating model is three simultaneously working specialist agents,
  continuously coordinated by Integration, with the owner providing only
  `continue` and `stop` commands.

