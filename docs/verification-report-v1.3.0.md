# FeinStatic v1.3.0 — verification report

Date: 2026-11-04

## Build and unit/integration matrix

The final source tree was built and tested in four configurations in the development container:

| Configuration | Compiler / instrumentation | Result |
|---|---|---|
| Debug | GCC 14.2.0 | PASS — 138/138 cases |
| Release | GCC 14.2.0 | PASS — 138/138 cases |
| ASan/UBSan | GCC 14.2.0 | PASS — 138/138 cases |
| Clang Release | Clang 17.0.0, `-Werror` | PASS — 138/138 cases |

The 138 test cases cover membership-state validation and serialization, quorum intersections, learner catch-up gating, joint/final commit ordering, leader step-down, durable configuration recovery, snapshot-boundary membership, and existing Ledger, networking, storage, Match, Flow, settlement, and bridge behavior.

A freshly extracted copy of the actual release archive was also configured and built from scratch with GCC 14.2.0 in Release mode; its CTest suite passed 138/138 cases.

## Real-process tests on the clean extracted package

Both process labs were run against the binaries built from a fresh extraction of the release archive. Both completed with exit status 0 and cleaned up their node processes.

### Three-process Raft crash/snapshot lab — PASS

- Three separate FeinStatic processes, independent SQLite databases, and Ed25519 node identities.
- Leader process `SIGKILL`, replacement election, and commits while the former leader is offline.
- Restart and catch-up, minority partition prevention, majority progress, partition healing, and conflict repair.
- An offline process misses 136 committed blocks; the majority creates a durable snapshot at Raft index 128.
- The restarted process installs the snapshot over authenticated TCP and converges at Ledger height 139.

### Four-process joint-consensus membership lab — PASS

- Three voters and one pre-provisioned learner run as four independent OS processes.
- The learner remains offline while the majority commits 136 blocks and compacts at index 128.
- The learner restarts, installs the snapshot, and catches up before promotion.
- The cluster commits the joint and final configurations while replacing/removing the current leader.
- The removed leader steps down, restarts from SQLite as a learner, catches up, and is re-promoted in a second transition.
- Final state converges with one leader, three voters, and one learner.

## Demonstration and benchmark

- Release Ledger/Match/Flow demonstration: PASS.
- One-million-order synthetic in-memory Match benchmark: 305,479 operations/second on the development container (GCC 14.2.0, 5 reported hardware threads), p95 latency 3.33 microseconds.
- The benchmark excludes networking, SQLite, and durable settlement; it is not a production throughput claim and is not directly comparable across machines.

## Scope and limits

- Raft handles crash/recovery failures only; Byzantine or malicious trusted peers are outside the safety model.
- Voter changes require identical pre-provisioned endpoint and trust rosters on all nodes, with no more than nine total peers.
- Learners must replicate committed history before promotion; removed voters remain learners.
- Any peer identity trusted by a node may submit membership changes. Separate operator roles are not implemented.
- Authenticated frames are signed and replay-protected but not encrypted. TLS, automatic peer discovery, automatic transaction gossip, rate limiting, and production finality are not implemented.
- The process labs cover controlled loopback schedules, not arbitrary adversarial schedules, WAN partitions, malicious nodes, or every storage-controller/power-loss scenario.

## Database and release integrity

v1.3.0 does not require a new SQLite schema migration beyond v1.2.0's schema v5. The updater backs up the entire existing checkout and verifies that pre-existing SQLite table schemas and row data are unchanged by the update. It preserves `.git` and `runtime/` and removes only the generated `build/` cache before rebuilding.
