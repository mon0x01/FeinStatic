# FST Integration Status Ledger

This ledger is owned exclusively by the Integration Agent (fst-integration). No other agent may edit it.

## Current integration branch and baseline
- Integration branch: `team/integration`
- Baseline commit SHA: `6f8fa522adae39a7909bf7a426563b1bc77867dc` (HEAD of `team/integration`, verified against required baseline `6f8fa522adae39a7909bf7a426563b1bc77867dc`)
- Integration worktree: `/home/monil/Desktop/CXX/FeinStatic-agents/integration`
- Setup commit SHA: `e45824f54f607b73c79c1d49f71c23c0541bd6a0` (setup(fst): add four custom Kilo agents and integration-status ledger)

## Active tasks and assigned agents
| Task | Assigned agent | Branch | Worktree | Status |
|------|----------------|--------|----------|--------|
| IS-01 | Integration ledger initialization (baseline, reservations, sync protocol, YOLO verification) | `fst-integration` | `team/integration` | `/home/monil/Desktop/CXX/FeinStatic-agents/integration` | COMPLETE |
| IT-01 | Phase 3 completion: FST-REQ-0036 finality & fork choice, FST-REQ-0037 validator entry & exit, FST-REQ-0038 consensus networking, FST-REQ-0039 multi-node consensus test network (≥4 validators, Byzantine finality) — START NOW | `fst-implementation` | `team/implementation` | `/home/monil/Desktop/CXX/FeinStatic-agents/implementation` | IN_PROGRESS |
| VT-01 | Targeted verification of incoming feature handoffs (per-commit: exact SHA + build preset + command + exit code + log); comprehensive five-preset + full suite verification scheduled after implementation + integration complete | `fst-verification` | `team/verification` | `/home/monil/Desktop/CXX/FeinStatic-agents/verification` | IN_PROGRESS (targeted) |
| DT-01 | Defect FST-BUG-0017 Release-mode timing failures (harness tests) — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-02 | Defects FST-BUG-0014 / FST-BUG-0015 TSan Boost.Asio socket races — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |
| DT-03 | Defect FST-BUG-0013 SecureSession handshake_result_ cross-thread race — DEFERRED to Phase 4; no source modifications yet; inventory existing evidence only | `fst-debugging` | `team/debugging` | `/home/monil/Desktop/CXX/FeinStatic-agents/debugging` | DEFERRED |

## Source-file ownership reservations
| File/path | Reserved for | Reserved by | Active task | Since |
|-----------|--------------|-------------|-------------|-------|
| include/feinstat/consensus/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
| src/consensus/** | Phase 3 completion (IT-01) | `fst-implementation` | IT-01 (IN_PROGRESS) | 2026-11-18 |
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
| (none yet) | - | - | - | - | - |

## Integration and review status
- Last integrated commit: `6f8fa522adae39a7909bf7a426563b1bc77867dc` (integration baseline; no feature work integrated yet beyond baseline)
- Last review: (none)
- Pending review: (none — awaiting first implementation handoff)

## Build and verification gates
- Build presets: `debug`, `release`, `asan`, `clang-release`, `tsan` (CMakePresets.json)
- Build gate: all five presets must compile with exit code 0; debug suite 343/343 PASS
- Verification gate: Verification Agent reports exact commit SHA tested, build preset, command, exit code, and log location; gate passes only on PASS evidence
- Regression gate: not yet run (full unit + integration suite on each accepted integration commit)

## Accepted integration commit SHAs
- `6f8fa522adae39a7909bf7a426563b1bc77867dc` — integration baseline (accepted as starting point; no feature work integrated yet)

## Outstanding blockers
- (none yet) — Phases 3–4 unblocked by owner decisions D-015/D-016/D-017/D-037 (APPROVED via ADR-0002/0003/0004/0005)

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
