# ADR-0002 UTXO Ledger and Transaction/State Architecture

## Metadata

| Field | Value |
|-------|-------|
| ID | ADR-0002 |
| Title | UTXO ledger and transaction/state architecture |
| Date | 2026-11-17 |
| Status | ACCEPTED |
| Classification | APPROVED |
| Author | FST project owner |
| Reviewed by | — |
| Supersedes | — |
| Superseded by | — |

## Problem statement

FST requires a canonical financial ledger that is the authoritative representation of financial ownership and state. The ledger must support native FST currency and settlement, atomic transactions consuming multiple inputs and producing multiple outputs, merchant/personal/batch/institutional payment workflows, deterministic double-spend prevention, domain-separated transaction signatures, network/chain-identifier replay protection, and bounded transaction size/resource limits. No general-purpose smart-contract virtual machine is to be introduced in the initial core protocol. Programmable spending conditions are to be investigated separately.

FeinStatic's existing ledger/account constructs were designed for a permissioned, crash-fault-tolerant prototype and do not satisfy the permissionless, deterministic, replay-protected UTXO requirements; they may be reviewed for component reuse only.

## Existing research and references

- S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," 2008, https://bitcoin.org/bitcoin.pdf — UTXO model, proof of work, chain-based ledger. (Primary source inspected via RS-015.)
- C. Cachin and M. Vukolic, "Blockchain Consensus Protocols in the Wild," arXiv:1707.01873, 2017 — ledger state representation models, UTXO vs account comparison. (Referenced via RS-015.)
- M. Al-Bassam et al., "Ledger: A data structure for efficient blockchain state," — state representation alternatives (referenced via RS-015).
- Ethereum yellow paper (account-based model) and Cardano eUTXO model — compared as alternatives in RS-015.
- FST research: docs/fst/research/transaction-model-research.md (RS-015), docs/fst/security/adversary-model-v0.1.md, docs/fst/scope/scope-v0.1.md.

Source verification status: sources cited above are recorded in RS-015 as inspected; exact verification status per source follows docs/fst/research/research-register.md. No source count is inflated.

## Alternatives evaluated

| Alternative | Advantages | Disadvantages | Classification |
|-------------|------------|---------------|----------------|
| UTXO (canonical ledger) | Deterministic double-spend prevention; atomic multi-input/multi-output transactions; natural parallel execution; simple state commitments; fits merchant/institutional workflows; straightforward replay protection via chain-ID + input consumption | UTXO-set growth; wallet complexity (balance abstraction required); fee calculation per-transaction size | APPROVED (D-037) |
| Account-based | Familiar balances; simpler wallet UX; lower per-transaction state growth | Non-deterministic nonce ordering complexity; double-spend requires global nonce serialization; harder atomic multi-party batching; state bloat from account trie | OPEN RESEARCH (not selected for initial core) |
| eUTXO / object-cell | Programmable spending conditions; parallel execution; richer smart-contract-like behavior | General-purpose VM pressure; complexity beyond initial scope; not yet specified for FST | OPEN RESEARCH (investigate programmable conditions separately) |
| Hybrid (account + UTXO) | Combines balance familiarity with UTXO atomicity | Increased implementation complexity; two state representations to maintain | OPEN RESEARCH (post-core) |

## Reasoning

The owner approved D-037: UTXO is the authoritative representation of financial ownership and state. UTXO gives deterministic validation — each output is consumed at most once, so double-spend prevention reduces to input-consumption uniqueness under deterministic finality. Atomic multi-input/multi-output transactions map naturally to payment workflows (merchant, personal, batch, institutional). Domain-separated signatures plus network/chain identifiers give fork replay protection consistent with D-036 (approved fork protection requirements). Avoiding a general-purpose VM keeps the initial core protocol small, auditable, and consistent with P12/P13/P14.

Wallets and applications may expose familiar account-based balances and payment experiences on top of the UTXO ledger; this is a presentation/layering concern, not a change to the canonical state representation. Programmable spending conditions are explicitly deferred to separate research (RS-015 § programmable conditions) before any extension of the transaction model.

## Selected design

**Canonical state:** UTXO set. Each UTXO is uniquely identified by an outpoint (transaction hash + output index), carries an amount (bounded integer), a spending condition (spending key / condition program), and a creation block height.

**Transaction:** canonical encoding of a list of input references (outpoint + spending witness) and a list of outputs (amount + spending condition). Transaction identifier is a domain-separated hash over the canonical encoding. Signatures are domain-separated (FST transaction signature domain) and bound the transaction to a network/chain identifier.

**Validation:** deterministic state-transition function. For each transaction: (1) syntactic checks (size, encoding, limits); (2) input existence and uniqueness (no duplicate input consumption within a block/mempool); (3) value conservation (sum of inputs >= sum of outputs + fee); (4) spending-condition and signature verification per input; (5) replay protection via chain-ID and single-consumption. Ordering is deterministic (canonical sort by transaction identifier or block inclusion order).

**Limits:** maximum transaction size, maximum inputs/outputs per transaction, maximum script/program size, bounded integer arithmetic (no floating point in consensus-critical paths).

**Wallet abstraction:** balance and payment-request abstractions built on top of the UTXO ledger; users operate in balance terms while the canonical state remains UTXO.

**State commitments:** reproducible state root (Merkle over UTXO set by outpoint) included in block headers; every correct node computes identical state and state hash given identical committed transactions and prior state.

**Storage:** persistent UTXO index (SQLite per D-031 review, or purpose-built bounded KV store), state commitments, reorganization/recovery semantics appropriate to deterministic finality (no reorg beyond finalized height), bounded mempool admission/eviction/recovery.

**Implementation locations:** new module `src/utxo/` and `include/feinstat/utxo/` (transaction encoding, validation, state transition, UTXO set, state commitment, mempool). Reuse of existing FeinStatic ledger files only after individual component review (D-022/D-023).

## Security assumptions

- Deterministic finality from Phase 3 consensus (D-017): a transaction input is considered consumed once included in a finalized block.
- Honest-signing-key model: wallet keys are generated and held by the owner; FST does not manage user keys.
- Bounded mempool admission prevents resource exhaustion.
- Replay protection depends on unique chain-ID and single-consumption enforcement.

## Limitations and trade-offs

- UTXO-set growth requires pruning/compaction strategy (deferred, post-core).
- Wallet balance abstraction adds complexity vs native account model.
- No programmable spending conditions in initial core; deferred to separate research.
- Fee calculation must be size/resource-based; fee market parameters (D-015) remain testnet-labelled until simulated.

## Verification evidence

None yet — implementation pending. Unit, integration, deterministic-replay, and property-based tests required for: transaction encoding/decoding, identifier stability, signature domain separation, value conservation, duplicate input rejection, atomic multi-input/multi-output, state root determinism, mempool admission/eviction, restart recovery.

## Cross-references

- [`decision-register.md`](../decisions/decision-register.md) — D-037 APPROVED.
- [`research-register.md`](../../research/research-register.md) — RS-015.
- [`scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — fork protection (D-036), financial functionality scope.
- [`architecture/feinstat-baseline.md`](../architecture/feinstat-baseline.md) — FeinStatic ledger reuse assessment.
