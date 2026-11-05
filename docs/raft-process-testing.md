# Raft multi-process failure lab (v1.2.0)

The original v1.0 integration tests exercised live authenticated TCP sessions with the Raft cluster hosted by three `NodeServer` objects in one test process. v1.1 added process isolation: each member is a separately spawned `feinstatic raft-node` OS process with an independent SQLite database, Ed25519 identity, trust store, listener, and log. v1.2 extends that process-level lab to prove real TCP snapshot transfer into an offline-and-restarted member.

## Run

Build the normal project first, then run:

```bash
python3 scripts/raft-process-chaos.py \
  --binary build/release/feinstatic \
  --prepare-helper build/release/feinstatic_raft_lab_prepare
```

The script uses only Python's standard library. It creates a temporary workspace outside the project runtime directory. To preserve a failed run for inspection, supply `--workspace /absolute/path/to/new-directory`.

## Scenarios

1. Start three independent node processes and wait for a unique leader.
2. Propose a Ledger block and verify majority commit, application, and identical tips.
3. Send `SIGKILL` to the leader and verify the remaining majority elects a replacement.
4. Commit another block while the former leader is down.
5. Restart the former leader from its own durable SQLite database and verify catch-up.
6. Isolate the current leader using four directed TCP proxies (both directions to both peers).
7. Verify the isolated minority leader cannot advance its commit index, while the connected majority elects a leader and commits a different block.
8. Heal the links and verify the isolated node discards/reconciles its uncommitted suffix and converges on the majority's committed Ledger tip and term.
9. Stop one process while the other two commit 136 blocks, verify the leader has compacted at Raft index 128, then restart the lagging process and verify durable snapshot installation over the authenticated TCP transport followed by convergence.

The proxies are per-directed-link TCP relays. One path receives a deterministic 10 ms delay on the first payload of each TCP connection. This adds latency without turning ordinary TCP read fragmentation into repeated artificial delays. Stopping a proxy closes active sockets; restarting it restores that path. This is deterministic connection-level fault injection, not a simulation of every packet-level loss pattern, congestion profile, kernel failure, Byzantine behavior, or arbitrary timing schedule.

## Safety and cleanup

- The helper creates synthetic test wallets and ephemeral identities. It does not read or modify the user's runtime database, wallet secrets, identity files, or existing project state.
- Every node has its own SQLite file. Never point multiple node processes at one database.
- The lab binds only to `127.0.0.1`.
- Node stdout/stderr is written to per-node logs in the workspace.
- The default workspace is created under the system temporary directory and retained for inspection. An explicit `--workspace` path can be supplied. On failure, the workspace is always preserved and its path is printed.
- The snapshot scenario exercises the actual TCP sender/receiver across independent processes and SQLite files. Snapshot-specific in-memory tests additionally inject a receiver restart between chunks and a SQLite transaction failure. These scenarios do not establish Byzantine fault tolerance, encrypted transport, dynamic membership, every filesystem power-loss mode, or production readiness.
