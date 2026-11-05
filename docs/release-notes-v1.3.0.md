# FeinStatic v1.3.0 — release notes

## Main milestone: Raft joint-consensus membership changes

v1.3.0 adds durable voter reconfiguration to the existing snapshot-capable, authenticated TCP Raft Ledger path.

- Adds checksummed membership state with stable, joint-pending, joint-committed, and final-pending phases.
- Implements the two-entry `C_old,new` then `C_new` transition. While joint, a decision requires a majority of both voter configurations.
- Adds authenticated non-voting learners. Learners replicate logs and snapshots but cannot vote, campaign, or lead.
- Requires a proposed new voter to be pre-provisioned and caught up to the leader's committed index before the joint transition can begin.
- Demotes removed voters to learners so they can continue to receive replication and may be promoted again after catch-up.
- Persists current membership and snapshot-boundary membership in the existing Raft state/snapshot encodings.
- Adds authenticated status and membership-change RPC/CLI operations.
- Adds simulator tests and a four-process authenticated TCP lab covering snapshot catch-up, voter promotion, removal of the active leader, restart as a learner, and a second reconfiguration.

## Compatibility and operation

- All nodes and operator clients in a cluster must run v1.3.0 together; the authenticated TCP protocol version is 5.
- No new SQLite schema migration is required beyond v1.2.0's schema v5.
- Membership can change only inside the pre-provisioned endpoint and trust roster. The total roster is limited to nine peers.
- A dedicated operator identity is recommended. Any identity trusted by a node can currently request membership changes; separate administrator and read-only trust roles are not implemented.

## Explicit limitations

This remains crash-fault-tolerant Raft, not Byzantine fault tolerance. Membership changes do not discover or distribute endpoints/keys. Signed frames are authenticated and replay-protected but not encrypted; TLS is not implemented. There is no public-chain finality, production custody guarantee, or production payment/exchange claim.
