# Chain selection and reorganization (v0.8.0)

## What this release implements

v0.8.0 makes the node able to compare two independently validated histories from the same genesis and select a canonical chain deterministically. It is a chain-selection/reorganization layer, not distributed consensus. There are no votes, quorums, leaders, Byzantine fault model, Sybil resistance, or distributed finality.

## Current chain-work rule

Every block in the current FeinStatic protocol uses one fixed `difficulty` configured for the entire chain. Every valid block therefore represents the same proof-of-work target. Under this explicit constraint, chain height is proportional to accumulated work:

1. The valid candidate with the greater height is preferred.
2. At equal height, the candidate with the lexicographically smaller 32-byte tip hash is preferred.
3. Identical histories are equivalent.

The equal-height tie-break exists to make two honest nodes presented with the same two candidates converge on one choice. It is **not a security score**: a miner can grind nonce values to influence the tip hash. It offers neither Sybil resistance nor Byzantine agreement. Do not enable variable per-block difficulty without replacing the height proxy with a true cumulative-work score and adding protocol/version compatibility checks.

## Synchronization protocol

The client sends a bounded locator containing the current tip, a short recent linear window, exponentially spaced older heights, and genesis. The peer returns the highest matching locator entry it has. The client downloads only the suffix after that match in bounded batches.

The client does not apply blocks as they arrive. It:

1. Fetches the candidate suffix and validates contiguous heights/previous hashes.
2. Rechecks the peer's advertised height and tip to detect a changing peer during the download.
3. Reconstructs the candidate chain from its validated local prefix plus the remote suffix.
4. Replays and validates the entire candidate chain (genesis, PoW, block links, transaction signatures, nonces, balances, supply and replay constraints).
5. Applies `DeterministicForkChoice`.
6. If the candidate wins, stages a new mempool: current pending transactions retain priority; detached-branch transactions are reconsidered in original block/transaction order; candidate-confirmed, invalid or conflicting transactions are removed.
7. Atomically replaces non-genesis block rows and the durable mempool snapshot in one SQLite transaction. The in-memory chain/mempool are published only after commit.

If any validation, allocation, or SQLite operation fails, canonical in-memory state remains unchanged and the SQLite transaction rolls back. A shorter divergent branch does not replace a longer local chain. A shorter exact prefix is reported as the local node already being ahead.

## Bridge and external transaction references

The off-chain FinancialPlatform stores durable references to native Ledger transactions:

- Confirmed deposits create Settlement credits and pin the transaction ID and block height.
- Confirmed withdrawals record the signed transaction ID and confirmation height.
- Pending withdrawals have a durable signed transaction ID and are expected to remain either confirmed on-chain or present in the durable mempool.

A chain reorganization that orphaned a confirmed deposit could leave an off-chain credit without its corresponding canonical vault transfer. A reorganization that orphaned a withdrawal could strand the platform debit. The operational `peer-sync` commands therefore load `FinancialPlatform::bridge_transaction_references()` and pass those references into the Ledger reorganization guard. A candidate is rejected if it removes a protected transaction, moves a height-pinned confirmation, or drops a pending withdrawal from the new durable mempool.

Programmatic users of `Ledger::consider_chain` or `PeerClient::synchronize` must supply all durable external transaction references themselves. The generic Ledger library cannot infer arbitrary external journals. Do not omit the references when Bridge or another external settlement system is active.

## Limits and safe operating assumptions

- All nodes must share identical genesis, fixed difficulty, and block reward parameters.
- The tie-break can be ground and is not consensus. Two authenticated peers can still show different tips until they exchange candidates; no quorum or economic security model is implemented.
- There is no difficulty retargeting, checkpoint/finality, fork-choice based on variable cumulative work, reorg depth policy, peer reputation, or defense against eclipse/Sybil attacks.
- A malicious trusted peer can still consume CPU/bandwidth within the protocol's frame/batch/time bounds. v1.7 token-bucket admission limits connection attempts globally and per source address, but is not a complete DoS defense.
- The v1.8.0 node accepts and performs bounded inbound socket I/O concurrently, but Ledger/Raft state transitions remain single-writer under one per-node state mutex. Each node must use its own SQLite database. Never share one writable database file across node processes.
- Protocol v8 encrypts authenticated peer application frames and supports signed trust-policy pushes using X25519/HKDF-SHA-256/AES-256-GCM; this is not TLS and requires independent cryptographic review before use outside controlled networks.
- A locally selected chain is not proof of irreversible or globally agreed state. No real funds or production custody should depend on this prototype.
