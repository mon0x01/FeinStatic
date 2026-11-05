# FeinStatic v1.6.0 — managed peer endpoints and bounded outbound dispatch

v1.6.0 adds live management of endpoint state within the already-provisioned Raft roster and replaces the per-remote-peer outbound thread model with a fixed-size shared worker pool. It retains authenticated/encrypted protocol v7 and does not change the SQLite schema.

## Endpoint registry

Each node stores a separate `<database>.endpoints` file. The registry is versioned, checksummed, bound to the node's cluster ID, and validated against the exact pre-provisioned peer-ID roster. It is owner-only and atomically replaced. A corrupt, foreign, unsafe-permission, or roster-mismatched registry causes startup to fail closed. The initial registry is seeded from the members file on first startup.

Authenticated active operators can query peer dispatch health and update the host/port/enabled state of an existing remote peer. Updates advance a durable generation. Peer IDs cannot be added or removed by these RPCs. An active Raft voter cannot have its endpoint disabled; it must first be demoted through joint consensus. Each node's endpoint registry is local, so apply the desired update to every node that needs the new route.

## Outbound worker pool

- Fixed shared worker pool, default four and maximum eight workers.
- At most one in-flight RPC per peer preserves per-peer FIFO ordering.
- Bounded global queue by message count and encoded bytes, plus bounded per-peer queue (default 128).
- Heartbeat work may be coalesced/dropped under pressure; non-heartbeat queued work is retained within limits.
- Failed sends use capped exponential retry backoff up to eight seconds; Raft heartbeat/election behavior remains the protocol-level recovery mechanism.
- Operator health reports include endpoint state, queue depth, attempts, successes, failures, drops, consecutive failures, retry delay, timestamps, and last error.

## CLI

```text
feinstatic raft-peer-health-auth db host port identity.key trust.txt expected-peer-id
feinstatic raft-peer-endpoint-set-auth db host port identity.key trust.txt expected-peer-id target-peer-id endpoint-host endpoint-port enabled|disabled
```

## Limits

- Endpoint management only changes already-provisioned remote peers. There is no peer discovery or online peer-ID roster expansion/removal.
- Disabling an active voter is rejected. Complete joint-consensus demotion first.
- Only outbound dispatch is concurrent. The inbound request loop and Ledger/Raft state transitions remain serialized.
- Queue bounds and retry backoff are not inbound rate limiting, Internet-scale admission control, or DoS protection.
- Endpoint registries are node-local and require backup/management on each node.
- Protocol v7 and the SQLite schema remain unchanged. The endpoint registry is an additional owner-only file beside each node database.

## Verification

The release suite contains 150 automated test cases. The v1.6-specific tests cover endpoint-registry creation and permissions, checksum/roster validation, fail-closed corruption handling, persistence across restart, operator-only health and endpoint updates, rejection of disabling an active voter, and server lifecycle races (stop-before-run and concurrent run rejection). The real-process labs exercise dispatch during the existing consensus workloads, but they are not a dedicated queue-saturation or retry-backoff stress test. The release also includes the three-process crash/partition/snapshot lab and four-process joint-consensus membership lab.

## Server lifecycle correctness

`NodeServer::run()` is a one-shot operation. A stop request made before the run thread is scheduled is retained; `run()` never clears it. Concurrent or repeated entry while running is rejected, and an already-stopped server does not restart its outbound dispatcher. This closes a shutdown race where the run thread could previously erase an early stop request and continue indefinitely. The regression suite checks stop-before-run and concurrent run entry.
