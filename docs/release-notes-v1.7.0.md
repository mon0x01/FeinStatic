# FeinStatic 1.7.0 release notes

## Summary

FeinStatic 1.7.0 strengthens the authenticated Raft network with bounded inbound admission and automatic best-effort transaction gossip. It also fixes outbound retry semantics and separates consensus and gossip scheduling so transaction dissemination cannot consume the capacity reserved for Raft control messages.

## Inbound connection admission

- Adds global and per-source-address token buckets before handshake parsing.
- Adds configurable refill rates and burst sizes, a maximum tracked-address count, and idle bucket expiry.
- Defaults tolerate a nine-member permissioned cluster and several local processes sharing one loopback address. These defaults are not an Internet-facing security recommendation.

## Transaction gossip

- Accepted transactions are propagated to every other enabled pre-provisioned Raft member/learner. The source peer is excluded.
- Recipient validation and durable mempool persistence remain authoritative. Duplicate deliveries are treated as successful delivery.
- On startup and periodically thereafter, pending durable mempool transactions are scanned and queued. This reconstructs gossip after process restart and recovers from bounded queue pressure.
- Gossip is best-effort dissemination, not consensus, canonical inclusion, or finality. A Raft leader must still propose a block and the cluster must commit it.

## Outbound scheduling and reliability

- Separate per-peer Raft and transaction-gossip FIFO queues use a bounded shared worker pool.
- Raft capacity is reserved; gossip cannot fill the entire control queue.
- Raft receives priority, with an eight-message streak limit so a permanently busy Raft queue cannot starve gossip. One message per peer may be in flight.
- Retryable failures are requeued at the head of the original queue. Queue accounting retains the in-flight reservation until completion, so retries do not depend on newly available capacity.
- Raft and gossip use independent capped exponential retry backoff. Invalid transactions rejected by a recipient are terminal and are not retried indefinitely.

## Compatibility and data

- The authenticated wire protocol advances to version 8. Upgrade every Raft member and authenticated operator client together; mixed protocol-v7/v8 clusters are not supported.
- No SQLite schema or persisted consensus-state format change is introduced. Pending transactions remain in the existing durable mempool.
- The inbound processing loop and Ledger/Raft state transitions remain serialized and single-writer. The outbound dispatcher is concurrent, but it sends already-created messages and does not mutate Ledger state.

## Validation in the development environment

The final v1.7.0 source revision was built and tested in the development container:

- GCC Debug: 153/153 automated test cases passed.
- GCC Release: 153/153 automated test cases passed.
- GCC ASan/UBSan: 153/153 automated test cases passed.
- Clang Release with `-Werror`: 153/153 automated test cases passed.
- Three-node authenticated TCP gossip/restart test: passed with 130 transactions queued while both receivers were offline, origin restart, receiver recovery, and convergence.
- Destination-outage retry test: passed; a failed outbound gossip remains queued and is delivered after the receiver reconnects without restarting the sender.
- Inbound token-bucket test: passed; verifies per-address burst rejection and recovery after token refill.
- Three-process Raft chaos lab: all nine stages passed, including leader failure, majority progress, minority isolation, partition healing, conflict repair, and authenticated snapshot installation at index 128 after 136 blocks were committed while a receiver was offline.
- Four-process joint-consensus membership lab: all eight stages passed, including offline learner snapshot catch-up and two membership transitions.
- Release demo: passed.
- One-million-order synthetic Match benchmark: completed at approximately 452,879 order operations/second in this container. This is an in-memory single-process benchmark, not a network or persistence benchmark.

The development container uses GCC 14.2 and Clang 17; the target Arch system has a different toolchain. The user-machine verification script must still be run on the target Arch environment before considering the update installed there.

This is an educational/private financial-infrastructure prototype. The encrypted protocol has not received an independent cryptographic audit; token-bucket admission is not a complete DDoS defense; Raft is crash-fault tolerant, not Byzantine fault tolerant. Do not use real customer funds or expose nodes to untrusted networks.
