# FeinStatic v1.6.1 release notes

## Purpose

This is a verification-hardening patch for v1.6.0. It does not change the Raft wire protocol, SQLite schema, persistent Raft encoding, or application behavior.

## Joint-consensus process lab correction

A follower can apply an entry after receiving `leader_commit`, while the leader has not yet processed the follower's corresponding `AppendResponse`. Therefore, the follower's `Applied index` can reach the leader's `Commit index` shortly before the leader's local `match_index` reaches that same value.

The Raft core correctly refuses to promote a new voter until the leader's acknowledged replication index reaches the committed index. v1.6.1 preserves that guard. The four-process lab now:

- Selects the currently elected leader immediately before each transition.
- Retries only the explicit pre-append `new Raft voter is not caught up` rejection.
- Uses a bounded deadline and reports the last rejection if the learner never becomes caught up.
- Recomputes the target voter set if leadership changes while waiting.
- Fails immediately for every other error.

No membership command is appended during a rejected attempt; no safety check is weakened.

## Verification

- GCC Debug, GCC Release, GCC ASan/UBSan, and Clang Release with `-Werror`: 150 tests each.
- Three-process crash/partition/snapshot lab.
- Four-process joint-consensus promotion, leader removal, restart, and re-promotion lab.
- Clean source archive extraction and Release CTest.

This patch is not a new production-readiness claim. Existing v1.6.0 limitations remain.
