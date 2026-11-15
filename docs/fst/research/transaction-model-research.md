# FST Transaction Model Research — v0.1

**Status:** DRAFT — research survey and architecture assessment.
**Classification:** PROVISIONAL (research direction requiring validation; not an
approved production design).
**Milestone:** FST Milestone 2F — Transaction Model Research and Architecture Assessment.
**Related research:** RS-015 (this document), RS-001, RS-002, RS-003, RS-006, RS-007,
RS-009, RS-010, RS-012, RS-013, RS-014.
**Related decisions:** D-015 (OPEN RESEARCH), D-016 (PROVISIONAL), D-017 (OPEN
RESEARCH), D-018 (PROVISIONAL), D-020 (PROVISIONAL), D-036 (APPROVED).
**Related specifications:** `spec/core-protocol-specification-v0.1.md` (Section 8).

This document records the foundational transaction-model research required before FST
transaction processing, mempool, transaction dissemination, and consensus integration can
be designed. It is a **research and architecture milestone, not a transaction
implementation milestone**. No transaction format, nonce model, fee policy, validation
algorithm, mempool behavior, or consensus mechanism is selected or approved here.

## 1. Executive summary

FST is an independent, globally decentralised financial network whose initial on-network
asset is FST only (decision D-014, APPROVED). The transaction model — how state is
represented, how transactions are structured and authorised, and how double-spends and
replays are prevented — is the foundational application-layer concern. It gates
transaction processing, mempool, dissemination, fee markets, light-client proofs, wallet
design, offline operation, and monetary policy.

This research evaluates four architectural alternatives:

1. Traditional UTXO model (Bitcoin).
2. Account-based model (Ethereum).
3. Extended UTXO (eUTXO) model (Cardano, Nervos CKB Cell model).
4. Hybrid / alternative state-based models, including object/cell approaches.

For each alternative it examines state representation, transaction structure, ownership
and authorisation, signature and key requirements, validation complexity, double-spend
prevention, replay protection, transaction dependencies, parallel validation and
execution, determinism, state growth and storage, scalability, mempool complexity,
propagation, fee calculation, privacy, light-client compatibility, offline implications,
institutional/high-volume suitability, implementation complexity, and long-term
extensibility.

It then defines the transaction lifecycle from creation through confirmation, separating
protocol-level validity rules from node-local mempool admission policy, block-inclusion
policy, and consensus-dependent rules. It assesses security and privacy properties,
compares fee-market alternatives, evaluates financial-use-case suitability, and records
storage and scalability implications.

The document provides two credible candidate designs for FST and an evidence-backed
**provisional recommendation**. The recommendation remains PROVISIONAL: the owner retains
authority over final model selection. No model is approved or silently selected by this
milestone.

### Headline findings

- **UTXO** offers native double-spend prevention (each output spent once), straightforward
  parallel validation of non-conflicting transactions, strong privacy through address
  non-reuse, and precise off-chain fee prediction. Its disadvantages are the change-output
  pattern, UTXO-set growth, poor fit for multi-input institutional batching without
  interaction, and the absence of a natural global balance.
- **Account-based** offers a simple global balance, natural support for multiple-input
  institutional and batch payments, simple wallet UX, and straightforward replay protection
  via nonce plus chain ID. Its disadvantages are a globally shared mutable state (serialization
  bottleneck, state-dependent validation), nonce-management complexity, front-running and
  ordering sensitivity, and weaker privacy because account histories are public and stable.
- **eUTXO** combines UTXO-style double-spend prevention and local validation with
  programmable locks and data-carrying outputs, enabling deterministic off-chain validation
  and parallelism. Its disadvantages are larger transactions (full new state must be
  transmitted), higher implementation complexity, and weaker fit for shared mutable state
  (e.g. a central order book).
- **Hybrid/object-cell** models (e.g. Nervos CKB Cell model) offer first-class assets,
  flexible fee payment, and parallel execution, but add state-rent / upkeep complexity and
  are the least battle-tested for a pure payment network.

For FST's stated scope — native FST transfers, person-to-person, merchant, institutional,
batch, and high-volume payments, settlement, independent wallets — **a traditional UTXO
model is the evidence-backed provisional candidate**, with an **account-based model with
nonce-plus-chain-ID replay protection as the credible alternative**. The choice between
them hinges on whether FST prioritises parallelism/privacy (UTXO) or simple multi-input
batching and wallet UX (account-based). Neither is selected.

## 2. FST requirements and constraints

### 2.1 Approved decisions and principles that bound this research

| Source | Classification | Requirement relevant to transaction model |
|--------|----------------|-------------------------------------------|
| D-001 / P1 | APPROVED | FST is an independent global financial network; no single organisation with unilateral control. |
| D-003 / P2 | APPROVED | No permanent privileged protocol authority. |
| D-006 / P5 | APPROVED | Participants can reject proposed upgrades. |
| D-008 / P8 | APPROVED | No central emergency administrator or unilateral master key. |
| D-009 / P9 | APPROVED | Security from protocol design and independent verification. |
| D-010 / P10 | APPROVED | Network survives disappearance of founding developers. |
| D-011 / P12 | APPROVED | Research-first engineering. |
| D-013 / P14 | APPROVED | Small, tested, documented releases. |
| D-014 | APPROVED | Initial on-network asset scope: FST only. |
| D-015 | OPEN RESEARCH | Monetary policy, issuance, supply, market-value model unresolved. |
| D-016 | PROVISIONAL | Permissionless validator participation (direction only). |
| D-017 | OPEN RESEARCH | BFT research direction, not selected. |
| D-018 | PROVISIONAL | Bounded offline spending authority (direction only). |
| D-020 | PROVISIONAL | Strong cryptographic privacy (direction only). |
| D-021 | PROVISIONAL | Provisional fork and migration design. |
| D-036 | APPROVED | Fork protection: unique network identity, replay protection across networks, distinct currency identifiers. Mechanisms OPEN RESEARCH. |
| P4 | APPROVED | Independent teams must be able to maintain the software. |

### 2.2 Financial scope (initial asset: FST only)

The initial on-network asset is FST only. The intended financial functionality includes
native FST transfers, person-to-person payments, merchant payments, multiple-input
institutional payments, batch payment processing, high-volume transfers, settlement, and
independent wallets and clients. Additional financial services (smart contracts, tokens,
fiat on/off-ramps) are explicitly future work and must not be assumed in this model
selection.

Smart-contract execution is treated as a separate architectural question. This research
does not assume FST requires a general-purpose smart-contract platform. Each model's
complexity if smart contracts were later added is noted where relevant.

### 2.3 Constraints on this milestone

- No final transaction encoding, nonce model, transaction-identifier format, fee policy,
  validation algorithm, or mempool behavior may be specified before the underlying
  decision is approved (D-015/D-016/D-017 remain unresolved).
- No implementation code is required or permitted in this milestone.
- No OPEN RESEARCH or PROVISIONAL decision is converted to APPROVED.
- D-036's mechanisms (chain-ID format, signing scheme, token symbols) remain OPEN
  RESEARCH; this research records the options but does not select one.

## 3. Technical explanation of each transaction model

### 3.1 Alternative 1 — Traditional UTXO model

**Primary example:** Bitcoin (developer.bitcoin.org devguide, inspected).

#### State representation

The ledger state is the **UTXO set**: a collection of unspent transaction outputs. Each
output carries an amount (satoshis) and a locking script (pubkey script) that defines the
conditions under which it may be spent. A user's balance is the sum of all UTXOs whose
locking script they can satisfy. There is no global account balance; state is a graph of
outputs linked by transactions.

#### Transaction structure

A transaction has one or more **inputs** and one or more **outputs**. Each input references
a previous output by `(txid, output index)` and provides a **unlocking script** (signature
script) with data satisfying that output's locking script. Each output creates a new UTXO
with an amount and a locking script. A transaction also carries a version number, a
locktime, and per-input sequence numbers. The transaction identifier (txid) is the hash of
the serialized transaction (excluding witness data in SegWit). The sum of input values
must equal the sum of output values plus fees; any surplus is returned to the spender as a
**change output**.

#### Ownership and authorisation

Ownership is the ability to satisfy an output's locking script, typically by producing a
valid digital signature over the transaction under the referenced public key. The locking
script is a predicate; anyone who satisfies it may spend. This is a capability-based model:
possession of the satisfying witness authorises the spend.

#### Signature and key requirements

Bitcoin uses ECDSA over secp256k1, with signature hash types (SIGHASH_ALL, SIGHASH_NONE,
SIGHASH_SINGLE, plus SIGHASH_ANYONECANPAY) that let signers select which parts of the
transaction to sign. Signatures are malleable because the signature script (which holds the
signature) is not covered by the signature hash; SegWit moves witness data out of the
txid to reduce malleability. Each input is typically signed by its owner; multi-input
transactions require multiple signatures (or multisig scripts such as m-of-n CHECKMULTISIG).

#### Validation complexity

Validation is **local and stateless with respect to the rest of the ledger**: a transaction
is valid if (a) each referenced input exists in the UTXO set and is unspent, (b) each
unlocking script satisfies its locking script, (c) input values >= output values + fee,
(d) the transaction is final (locktime / sequence rules), and (e) script evaluation
succeeds. No global account state is consulted. Scripts are deliberately stateless and
Turing-incomplete (no loops/gotos), making validation predictable.

#### Double-spend prevention

Each UTXO can be spent exactly once. Spending an input marks it spent; a second transaction
attempting the same input conflicts and only one can be confirmed. Double-spends are
resolved by consensus ordering: the first-confirmed transaction wins; the other is
invalidated because its input is no longer unspent.

#### Replay protection

Bitcoin has no native chain-ID binding in the transaction signature. Transactions are tied
to the network implicitly through the UTXO set they reference; a transaction built on one
chain's UTXOs cannot spend on a fork that lacks those UTXOs, but replay across a clean
fork (or across networks with identical UTXO history) is possible unless an additional
mechanism (e.g. chain ID in signature, or fork-specific UTXO tagging) is added. D-036
requires replay protection across networks; for UTXO this must be an added mechanism.

#### Transaction dependencies

Transactions form a directed acyclic graph: an input depends on the transaction that
created the referenced output. Chains of dependent transactions (a transaction spending an
output of an unconfirmed transaction) are possible. Spending a change output immediately
creates a dependency. Malleability (without SegWit) can break descendant transactions
because the txid changes.

#### Parallel validation and execution

Transactions that do not share inputs are independent and can be validated and executed in
parallel. Conflicting transactions (same input) are serialized by consensus. This is a
well-documented scalability advantage of UTXO.

#### Determinism

Validation of a transaction depends only on the transaction and its referenced inputs, not
on global state. A transaction valid off-chain remains valid unless an input is spent by a
conflicting confirmed transaction. Fee can be predicted precisely before submission
(fee = sum(inputs) - sum(outputs)).

#### State growth and storage

The UTXO set grows with every unspent output and shrinks with every spend. Change outputs
tend to increase UTXO count. Pruning spent outputs is possible; full nodes track the
UTXO set, archival nodes track all history. State size is proportional to unspent output
count, not transaction count.

#### Scalability

Good for payment-scale parallelism; limited on-chain expressiveness (no general smart
contracts without extensions). Layer-2 channels (Lightning) build on UTXO. Block space is
the scarce resource; fees are paid per byte/vbyte.

#### Mempool complexity

The mempool holds unconfirmed transactions keyed by txid. Conflict handling: a new
transaction spending an already-mempooled input conflicts; replacement policy (e.g. RBF)
determines whether the replacement is accepted. Duplicate detection by txid. Fee-rate
prioritisation is per-byte.

#### Transaction propagation

Transactions are gossiped between peers; each peer validates before forwarding (or uses
policy limits). Orphan transactions (referencing unknown inputs) may be held temporarily.

#### Fee calculation and fee markets

Fees are paid per byte (or vbyte with SegWit weight). Miners select by fee rate. A minimum
relay fee exists. Fee-rate auctions occur during congestion. Replace-by-Fee (BIP-0125,
inspected) allows opt-in replacement paying a higher absolute fee plus bandwidth cost.
High-priority (old-coin) transactions were historically exempt; this is now 0 by default.

#### Privacy implications

UTXO graphs are public. Address reuse enables tracking of receiving and spending habits.
Best practice: use a fresh address per payment and per change output (key non-reuse),
combined with CoinJoin / merge avoidance. Public keys are hidden until first spend (in
P2PKH), providing some protection against future public-key attacks. Privacy is
pseudonymous, not anonymous.

#### Light-client compatibility

Light clients can verify payments via Merkle proofs of inclusion in a block plus the
UTXO set (or by querying full nodes for the relevant UTXOs). SPV-style verification works
natively. Filtering (BIP-157/BIP-158 style) lets light clients retrieve relevant
transactions.

#### Offline transaction implications

A transaction can be constructed and signed offline as long as the spender knows the
referenced UTXOs are unspent (risk: the UTXO may be spent by the time the transaction is
confirmed). Offline spending with global double-spend prevention under arbitrary partitions
is impossible without additional bounded-authority mechanisms (scope limitation noted).

#### Institutional and high-volume payment suitability

Multiple inputs in one transaction are supported (multiple UTXOs consolidated), but
coordination is required because each input is signed by its owner. Batch payments are
supported via multiple outputs. Institutional multi-party payments require multi-signature
or coordinated signing; atomicity across parties is not native.

#### Independent implementation complexity

Low-to-moderate. The script language is simple and deterministic; the UTXO model is
well-documented with multiple independent implementations. Easy to implement correctly for
payments; harder for complex smart-contract-like logic.

#### Long-term extensibility

Extensible via new script opcodes (soft forks), SegWit, Taproot, and layer-2 protocols.
Adding stateful smart contracts requires extending or replacing the model (e.g. eUTXO).

---

### 3.2 Alternative 2 — Account-based model

**Primary example:** Ethereum (ethereum.org developers docs, EIP-155, EIP-1559; inspected).

#### State representation

The ledger state is a **global key-value mapping of accounts**. Each account has a
balance (in the native token), a nonce (transaction counter), optional code, and optional
storage. A user's balance is a single mutable value stored under their address. The state
is a globally shared mutable state (typically a Merkle Patricia Trie).

#### Transaction structure

A transaction is a signed instruction from an externally-owned account (EOA): fields
include `from` (sender), `to` (recipient or contract), `value` (amount), `nonce`
(sender's transaction counter), `gasLimit`, fee parameters (`maxFeePerGas`,
`maxPriorityFeePerGas` under EIP-1559), `data` (calldata), and a signature (`v`, `r`, `s`).
Transactions are RLP-encoded; EIP-2718 defines a typed envelope supporting multiple
transaction types (legacy, access-list, EIP-1559, blob, etc.). The transaction hash is
keccak256 of the RLP encoding (with type prefix for typed envelopes).

#### Ownership and authorisation

Ownership is control of the private key corresponding to the sender address. Authorisation
is a digital signature over the transaction. The sender's nonce is incremented on each
successful transaction. Contract accounts are controlled by code, not a private key, and
cannot initiate transactions (only EOAs can).

#### Signature and key requirements

Ethereum uses ECDSA over secp256k1. EIP-155 binds the chain ID into the signature
(`v = {0,1} + CHAIN_ID * 2 + 35`) so a transaction signed for one chain is invalid on
another. Signature recovery yields the sender address. Signature malleability exists in
raw ECDSA (s vs -s) but is managed by canonical-s rules in clients.

#### Validation complexity

Validation is **state-dependent**: the validator must look up the sender's account (balance
and nonce) in global state, check nonce == current account nonce, check balance >= value +
max fee, verify the signature, then execute the transaction (which mutates global state).
Execution can fail mid-script (out of gas, revert), so a transaction's success is not
known until executed. Validation is inherently sequential per account (nonce ordering) and
depends on global state.

#### Double-spend prevention

Double-spends are prevented by the nonce: each nonce is used once, in order. A sender
cannot spend more than their balance because balance is checked at execution. The global
state enforces single-spending of each nonce and balance decrement.

#### Replay protection

EIP-155 provides replay protection across chains by binding the chain ID into the signed
data and `v`. A transaction signed for chain ID 1 (Ethereum mainnet) is invalid on chain
ID N. Nonce ordering also prevents replay within a chain (a replayed transaction would
have a stale nonce). Cross-fork replay between two chains sharing genesis and chain ID is
not prevented by EIP-155 alone; distinct chain IDs are required (D-036).

#### Transaction dependencies

Dependencies arise through the sender nonce: transactions from the same account must be
executed in nonce order, creating a strict per-account sequence. Cross-account
transactions are independent except for shared-state effects. Dependent transactions (e.g.
spending funds just received) require the funding transaction to be confirmed first.

#### Parallel validation and execution

Limited. Transactions from different accounts can execute in parallel only if they do not
touch overlapping state; in practice the shared global state and nonce ordering force
largely sequential execution per account. Parallel execution requires optimistic
concurrency control and conflict detection (complex). This is a well-known scalability
limitation of account-based models.

#### Determinism

Non-deterministic from the submitter's perspective: a transaction valid when submitted may
fail by the time it is executed (nonce consumed by another transaction, balance changed,
gas cost differs from estimate). Fees cannot be precisely predicted before submission
(gas used depends on execution). This presents monetary-loss and adversarial-behavior
risks.

#### State growth and storage

Every account and its storage is persisted indefinitely unless pruned. State grows with
the number of accounts and contract storage. The state tree (MPT) must be maintained by
full nodes; state size grows continuously and is harder to prune than a UTXO set because
accounts are mutable and referenced by contracts.

#### Scalability

State-dependent validation and nonce ordering limit layer-1 throughput; layer-2 solutions
(rollups, channels, sidechains) are required for scaling. Block space is the scarce
resource; gas is the unit of computation/storage cost.

#### Mempool complexity

The txpool holds pending transactions. Transactions are ordered by nonce per sender and by
fee rate. Replacement: a transaction with the same nonce and higher fee can replace the
pending one (account-based nonce replacement). Duplicate detection by (sender, nonce).
Fee prioritisation combines gas price and gas limit. Mempool state is more complex than
UTXO because of nonce sequencing and state-dependency.

#### Transaction propagation

Transactions are gossiped; validators execute before including. Orphan/dependency handling
relies on nonce ordering. A transaction with a future nonce (gap) may be held until prior
nonces are filled.

#### Fee calculation and fee markets

EIP-1559 (inspected): base fee per block (burned, dynamically adjusted by congestion) plus
priority fee (tip to validator). User sets `maxFeePerGas` and `maxPriorityFeePerGas`;
effective price = min(maxPriorityFeePerGas, maxFeePerGas - baseFee) + baseFee. Miner
receives only priority fee; base fee is burned. First-price auctions (legacy gasPrice) are
inefficient and require complex fee estimation.

#### Privacy implications

Account histories are public and stable: an address's balance and all transactions are
visible indefinitely. Address reuse is the norm (a stable payment address), which
simplifies UX but enables comprehensive tracking. Balances leak through the public state
tree. Privacy is weaker than UTXO-with-non-reuse without additional privacy layers.

#### Light-client compatibility

Light clients verify via state proofs (MPT proofs) over account balance/nonce/storage and
transaction receipts. More complex than UTXO SPV because proofs must cover the mutable
state tree and account existence/non-existence.

#### Offline transaction implications

A transaction can be signed offline but the nonce must be known and correct; if the
sender's nonce advances offline (another transaction confirmed), the offline transaction
becomes invalid. Offline spending requires bounded authority and later reconciliation
(scope D-018 limitation: unrestricted local spending and global double-spend prevention
under arbitrary partitions cannot both be guaranteed).

#### Institutional and high-volume payment suitability

Strong fit: a single transaction can transfer value from one account; multiple inputs are
natural (the account holds the aggregate balance). Batch payments are supported via
multiple transactions or contract-based batching. Institutional multi-party payments
benefit from a single mutable balance and simple nonce management. However, shared mutable
state creates contention under high volume.

#### Independent implementation complexity

Moderate-to-high. Requires a state tree (MPT), a virtual machine (if smart contracts),
nonce management, fee-market logic, and state-dependent validation. More complex to get
right than UTXO for payments; state root correctness is critical.

#### Long-term extensibility

Highly extensible: new transaction types (EIP-2718), fee markets (EIP-1559), account
abstraction (EIP-7702), and smart contracts are additive. Native support for stateful
applications.

---

### 3.3 Alternative 3 — Extended UTXO (eUTXO) model

**Primary examples:** Cardano (developers.cardano.org eUTXO explainer and technical
concepts; inspected), Nervos CKB Cell model (docs.nervos.org; inspected).

#### State representation

The ledger state is a collection of **extended unspent outputs** (cells/UTXOs). Each
output carries a value, a locking script (validator/lock script), and optionally arbitrary
data (datum). The locking script is a validator that receives the transaction context
(redeemers) and returns yes/no. Cells are immutable: updating a cell consumes it and
creates a new one. State is a graph of outputs; there is no global mutable account state.
(Nervos adds "state rent" / upkeep for on-chain storage.)

#### Transaction structure

A transaction consumes live cells (inputs) and creates new cells (outputs), plus optional
witnesses/redeemers and cell dependencies (references to cells needed for validation but
not consumed). Each input is unlocked by satisfying its lock script with a redeemer. The
transaction is validated by executing the lock/type scripts of consumed cells with the
transaction context.

#### Ownership and authorisation

Ownership is the ability to satisfy an output's lock script (typically a signature plus
conditions). Authorisation is decentralized: any transaction whose inputs' lock scripts
validate may spend. The lock script is the authority; there is no central issuer.

#### Signature and key requirements

Depends on the lock script; typically digital signatures (e.g. secp256k1/Ed25519) over
transaction data, with the chain/network bound into the signing message where replay
protection is needed. Scripts can require multiple signatures, thresholds, or arbitrary
conditions.

#### Validation complexity

Validation is **local**: a transaction's validity depends only on the transaction and its
referenced input cells (and cell dependencies), not on global state. Scripts execute
off-chain-style validation on-chain. Validation is deterministic and predictable: if all
inputs are live, the transaction is guaranteed to succeed. Smart-contract execution is
off-chain; on-chain nodes only verify.

#### Double-spend prevention

Each cell is consumed exactly once; spending marks it dead. Conflicts (two transactions
spending the same cell) are resolved by consensus ordering. This is the same native
double-spend prevention as UTXO.

#### Replay protection

Like UTXO, eUTXO transactions reference specific cells from a specific chain. Replay across
forks/networks requires an added mechanism (chain ID in signing, distinct cell
identifiers). D-036 mechanisms unresolved. eUTXO does not inherently provide cross-chain
replay protection.

#### Transaction dependencies

Transactions form a DAG via cell references. Dependencies exist when a transaction spends
a cell created by an unconfirmed transaction. Cell dependencies (non-consumed references)
allow a transaction to read state without consuming it. Parallelism is high for
non-conflicting transactions.

#### Parallel validation and execution

High. Transactions that do not consume the same cell are independent and can be validated
and executed in parallel. Each transaction runs in its own verification context (Nervos:
each transaction in its own VM). This is a key scalability advantage cited for eUTXO.

#### Determinism

Deterministic: initial and final states are known before submission; validity can be
checked off-chain. Fees are predictable. No mid-execution failure (unlike account-based).
This predictability is a unique advantage (Cardano docs, inspected).

#### State growth and storage

Every live cell is stored; dead cells are pruned. State grows with live cell count. Cells
carry arbitrary data (datum), which can bloat state; Nervos uses state rent to address
this. Full nodes track the live-cell set.

#### Scalability

Good parallelism and deterministic validation suit layer-1 scaling. Sharding is considered
easier than account-based because there is no global mutable state (research [4] notes
inability to shard account-based L1; eUTXO sharding proposed). Smart-contract state
bloat (full state in transactions) is a bottleneck for contracts with large dynamic state
(DiSCo_sim research [5]).

#### Mempool complexity

Similar to UTXO: conflict handling on shared cell inputs, duplicate detection by txid,
fee-rate prioritisation. Additional complexity from cell dependencies and datum size
limits.

#### Transaction propagation

Gossiped like UTXO; validation before forwarding. Larger transactions (full new state)
increase propagation cost.

#### Fee calculation and fee markets

Typically size/resource-based: fee proportional to transaction size and computational
validation cost. Nervos allows any party to pay fees (flexible fee coverage). Predictable
fees (deterministic). Fee-market design (auction vs base fee) is unresolved for FST.

#### Privacy implications

Similar to UTXO: pseudonymous, address non-reuse recommended. Cell graphs are public.
Data-carrying outputs (datums) can leak application state if not encrypted. Privacy
depends on usage patterns and optional privacy layers.

#### Light-client compatibility

Light clients verify via proofs of cell inclusion/existence and lock-script validation.
Compatible with UTXO-style SPV plus cell-data proofs.

#### Offline transaction implications

Like UTXO: signable offline if input cells are known live; risk of double-spend by
confirmation time. Bounded offline authority requires additional mechanisms (D-018).

#### Institutional and high-volume payment suitability

Good: multiple cells can be consolidated in one transaction; batch outputs supported.
Multi-party payments require coordinated signing. First-class assets make token-like
representations natural. However, shared mutable application state (e.g. a central order
book) is expensive in eUTXO (spectrum.fi research [2]).

#### Independent implementation complexity

Moderate-to-high. Requires a script validator, cell dependency handling, datum
management, and deterministic execution. More complex than plain UTXO; less complex than
a full account VM for payments.

#### Long-term extensibility

Extensible via new lock/script types and data fields. Supports native assets and smart
contracts within the UTXO paradigm. Adding shared mutable state requires design care.

---

### 3.4 Alternative 4 — Hybrid / alternative state-based models (object/cell)

**Primary example:** Nervos CKB Cell model (inspected) as an object/cell hybrid;
academic work on chimeric ledgers (eprint.iacr.org/2018/262; abstract only) and hybrid
UTXO/account simulators (arxiv 2406.07700; abstract only).

#### State representation

State is represented as **objects or cells** — addressable, versioned, first-class state
units, each with an owner and an associated behaviour (lock script/type script). Objects
can be mutable (new version created on update) and can hold assets natively. Some designs
allow assets and code to be separate first-class objects.

#### Transaction structure

Transactions reference objects by ID, specify the operations (create, update, delete,
transfer), and provide authorization witnesses. Multiple objects may be touched in one
transaction with atomicity across them. Fee payment may be delegated to any party.

#### Ownership and authorisation

Ownership is attached to each object (owner key or lock script). Authorisation requires
the owner's signature (or script satisfaction) for each object modified. Object-level
capabilities provide fine-grained control.

#### Signature and key requirements

Per-object signatures or threshold signatures. Key rotation and recovery are object-level
concerns. Chain binding for replay protection is required (D-036).

#### Validation complexity

Validation touches only the referenced objects (local state access), enabling parallelism
for non-overlapping object sets. Conflicting updates to the same object are serialized.
Execution model varies (some execute off-chain and verify on-chain).

#### Double-spend prevention

Object versioning prevents double-spends: an object can be updated from one known version;
a conflicting update to a stale version is rejected. Equivalent to UTXO consumption at the
object level.

#### Replay protection

Requires explicit chain/network binding in transaction signatures (D-036 mechanisms
unresolved). Object IDs may be namespaced by network.

#### Transaction dependencies

Dependencies via object versions: updating an object requires the latest version. Chains
of dependent updates are possible. Atomic multi-object transactions bundle dependencies.

#### Parallel validation and execution

High for transactions touching disjoint object sets; serialized for shared objects. This is
the core scalability argument for object models.

#### Determinism

Depends on design: object updates are deterministic if validation is pure over the
referenced objects and their versions.

#### State growth and storage

Object state grows with object count and version history. State rent/upkeep (Nervos) or
pruning policies bound growth. More state per object than a simple UTXO (metadata, code
refs).

#### Scalability

Object-level parallelism is a scalability advantage; shared hot objects become
contention points. Sharding by object ID space is feasible.

#### Mempool complexity

Conflict detection on shared objects; nonce/version sequencing per object; duplicate
detection by transaction hash; fee prioritisation.

#### Transaction propagation

Gossip with validation; larger objects increase message size.

#### Fee calculation and fee markets

Flexible: sender, receiver, or third party may pay (Nervos). Size/resource-based plus
state-rent options. Fee market unresolved for FST.

#### Privacy implications

Object graphs are public; object reuse enables tracking. First-class assets and flexible
fee payment can improve UX but add metadata. Privacy depends on design; not inherent.

#### Light-client compatibility

Object-state proofs (existence/non-existence, version) support light clients. More complex
than UTXO SPV due to object versions and references.

#### Offline transaction implications

Signable offline with known object versions; bounded authority and reconciliation needed
(D-018). Object version staleness is the offline risk.

#### Institutional and high-volume payment suitability

Strong: first-class assets, multi-object atomic transactions, delegated fee payment suit
institutional and batch workflows. Complexity is the drawback.

#### Independent implementation complexity

High. Object model, versioning, authorization scripts, state rent, and sharding are
substantial engineering. Least battle-tested for a pure payment network.

#### Long-term extensibility

Highly extensible: new object types, behaviours, and fee models are additive. Well suited
to future financial services beyond simple payments.

---

## 4. Comparative architecture matrix

The matrix below compares the four models across the criteria required by the mission.
Scoring rationale is explained per criterion rather than presenting arbitrary numbers.
Key: `+` = strength, `–` = weakness, `~` = mixed/neutral, `?` = unresolved / depends on
design choices (particularly replay protection and fee market, which FST has not decided).

| Criterion | UTXO | Account-based | eUTXO | Hybrid / object-cell |
|-----------|------|---------------|-------|----------------------|
| State representation | UTXO set (output graph) | Global account/balance map | Extended output graph (cells) | Object/cell graph, versioned |
| Transaction structure | Inputs reference (txid, vout); outputs | Signed instruction: from/to/value/nonce/gas/data | Consumes cells, creates cells, witnesses | Object references + ops + witnesses |
| Ownership & authorisation | Capability: satisfy locking script | Private key controls account | Satisfy lock script | Owner key / lock script per object |
| Signature & key reqs | ECDSA/secp256k1, SIGHASH types | ECDSA/secp256k1, nonce, EIP-155 chain ID | Script-dependent signatures | Per-object signatures/threshold |
| Validation complexity | Local, stateless, predictable | State-dependent, nonce-ordered | Local, deterministic | Local per object, parallelizable |
| Double-spend prevention | Native (spend once) | Native (nonce + balance) | Native (consume once) | Native (object versioning) |
| Replay protection | Not inherent (needs chain-ID binding) | EIP-155 chain ID + nonce | Not inherent (needs binding) | Needs explicit chain binding |
| Transaction dependencies | DAG via txid/vout | Per-account nonce sequence | DAG via cell refs | Object version dependencies |
| Parallel validation/execution | High (non-conflicting inputs) | Low (shared state, nonce order) | High (non-shared cells) | High (disjoint objects) |
| Determinism | High (off-chain checkable) | Low (state-dependent) | High (off-chain checkable) | Depends on design |
| State growth/storage | UTXO set, prunable | Global state tree, harder to prune | Live cells, datum bloat | Objects + versions, rent possible |
| Scalability | Good for payments | Limited L1; needs L2 | Good; sharding-friendly | Good; hot-object contention |
| Mempool complexity | Moderate (conflicts by input) | Higher (nonce seq, state-dep.) | Moderate (cell conflicts) | Moderate-High (object conflicts) |
| Propagation | Gossip, small txs | Gossip, state-aware | Gossip, larger txs (datum) | Gossip, object-size dependent |
| Fee calculation | Per-byte/vbyte | Gas (compute+storage) | Size/resource-based | Flexible payer + size/rent |
| Fee markets | Fee-rate auction | Base fee + priority (EIP-1559) | Unresolved for FST | Unresolved for FST |
| Privacy | Pseudonymous; non-reuse helps | Weak (stable addresses, public history) | Pseudonymous; datum leakage | Pseudonymous; object reuse |
| Light-client compatibility | SPV-friendly | MPT state proofs | Cell proofs | Object-state proofs |
| Offline implications | Signable offline; double-spend risk | Nonce risk; offline spending bounded | Signable offline; double-spend risk | Version staleness risk |
| Institutional / batch | Multi-input coordination needed | Natural aggregate balance | Multi-cell consolidation | Multi-object atomic, strong fit |
| Implementation complexity | Low-moderate | Moderate-high | Moderate-high | High |
| Long-term extensibility | Via scripts/soft forks/L2 | High (tx types, acct abstraction) | High (script types) | High (object types) |

### Reasoning behind the comparisons (not arbitrary)

- **Validation complexity / determinism:** UTXO and eUTXO validity depends only on the
  transaction and its referenced inputs, which is an established property of the model
  (Bitcoin devguide; Cardano eUTXO explainer, both inspected). Account-based validity
  requires global state look-up and execution, which is state-dependent and can fail
  mid-execution (Ethereum docs, inspected). This is a structural difference, not a score.
- **Parallel validation:** Transactions sharing no input/cell/object are independent by
  construction in UTXO/eUTXO/object models; account-based nonce ordering forces
  per-account serialization. This is a consequence of the state representation.
- **Replay protection:** UTXO/eUTXO/object models do not inherently bind transactions to a
  chain ID; EIP-155 (inspected) shows how account-based binds chain ID into the signature.
  D-036 requires cross-network replay protection; the mechanism is OPEN RESEARCH for all
  models, so this is marked as needing added binding rather than scored as present/absent.
- **Privacy:** UTXO/eUTXO privacy relies on address non-reuse (Bitcoin devguide,
  inspected); account-based public stable addresses leak history (Ethereum docs,
  inspected). Neither model provides anonymous privacy; both require additional layers.
- **State growth:** UTXO sets are prunable (spent outputs removed); account state trees
  grow monotonically and are referenced by contracts, making pruning harder. This is a
  structural difference observed in the inspected sources.
- **Institutional/batch:** Account-based aggregate balance naturally supports multiple
  inputs from one party; UTXO/eUTXO require consolidating multiple inputs with multiple
  signatures. This is a structural trade-off.

## 5. Transaction lifecycle comparison

This section defines the architectural implications of a transaction from creation through
confirmation, and separates four layers:

1. **Protocol-level validity rules** — consensus-enforced conditions for a transaction to
   be part of a valid state transition (format, authorization, double-spend, fee).
2. **Node-local mempool admission and relay policy** — local rules for accepting a
   transaction into the mempool and forwarding it (not consensus; varies by node).
3. **Block-inclusion policy** — the proposer/validator's choice of which mempool
   transactions to include and in what order (consensus-adjacent).
4. **Consensus-dependent rules** — finality, fork choice, ordering, and reorganisation
   handling (depends on the unresolved consensus algorithm, D-017).

| Lifecycle stage | UTXO | Account-based | eUTXO | Hybrid/object |
|-----------------|------|---------------|-------|---------------|
| Creation | Select UTXOs, build outputs incl. change, sign | Set nonce, value, gas, sign | Select cells, build cells, sign | Select objects, build ops, sign |
| Canonical serialization | Byte-level deterministic serialization | RLP / typed envelope (EIP-2718) | Canonical cell serialization | Object-canonical serialization |
| Transaction identifier | Hash of serialized tx (txid) | Hash of RLP/typed envelope | Hash of cell transaction | Hash of object transaction |
| Signing & authorization | Signature per input over tx parts | Single signature over tx; EIP-155 chain ID | Signature satisfying lock script | Per-object signatures |
| Network/chain binding | Not inherent; added mechanism needed | EIP-155 chain ID in signature | Added mechanism needed | Explicit chain binding required |
| Stateless vs state-dependent | Stateless (inputs only) | State-dependent (balance, nonce) | Stateless (cells only) | Stateless per object |
| Mempool admission | Syntactic + UTXO-availability check | Nonce + balance + fee check | Syntactic + cell-availability | Object-version + auth check |
| Conflict handling | Same-input conflict; RBF optional | Same-nonce replacement (fee) | Same-cell conflict | Same-object conflict |
| Duplicate detection | By txid | By (sender, nonce) | By txid | By tx hash |
| Expiration / validity window | Locktime | None native (nonce only) | Optional validity bounds | Optional |
| Replacement / cancellation | RBF (BIP-125 opt-in) | Same-nonce higher-fee replacement | Model-dependent | Model-dependent |
| Fee calculation | Sum(inputs) - sum(outputs) | Gas limit × effective gas price | Size/resource-based | Flexible payer + resource |
| Prioritization | Fee rate (sat/vbyte) | Gas price / priority fee | Fee rate | Fee rate |
| Propagation | Gossip; validate before forward | Gossip; nonce-gated | Gossip; validate before forward | Gossip; validate before forward |
| Confirmation | Included in block; input spent | Included; nonce incremented, balance changed | Included; cells consumed | Included; objects versioned |
| Finality / reorg | Consensus-dependent; re-spend on reorg | Consensus-dependent; nonce replay risk on reorg | Consensus-dependent | Consensus-dependent |

### 5.1 Canonical serialization and transaction identifier generation

- **UTXO:** A fixed byte-level serialization (version, inputs, outputs, locktime, optional
  witness). The txid is the hash of this serialization. SegWit separates witness data so
  the txid excludes the malleable signature script. Serialization must be deterministic
  across independent implementations for txid stability.
- **Account-based:** RLP encoding of ordered fields; EIP-2718 typed envelope prefixes a
  type byte. The transaction hash is keccak256 of the encoding. Multiple transaction types
  require versioning discipline.
- **eUTXO / object:** Canonical serialization of cell/object fields including scripts and
  data; the txid is the hash. Determinism is required for off-chain validation to match
  on-chain validation.

### 5.2 Signing and authorization

- UTXO: each spending input signed; SIGHASH types select covered portions; multisig
  (m-of-n) supported via scripts.
- Account-based: one signature over the whole transaction; sender recovered from
  signature; nonce binds ordering.
- eUTXO/object: signature satisfies the lock script; arbitrary conditions possible.

### 5.3 Network and chain identity binding

D-036 (APPROVED) requires transactions protected against replay across networks and
distinct currency identifiers. The mechanism (chain-ID format, signing scheme) is OPEN
RESEARCH. Options observed:
- **Account-style chain ID in signature** (EIP-155, inspected): chain ID included in
  signed data; `v` encodes chain ID. Simple, proven on Ethereum/ETC split.
- **UTXO-style:** bind chain ID into the signing message or into output identifiers;
  requires added mechanism since UTXO has no native chain field.
- **Separate network identifier** (already in FST signed node records and session context,
  established implementation): network separation at the P2P layer exists; transaction-level
  replay protection is separate and unresolved.

### 5.4 Replay protection across forks (respecting D-036)

D-036 is APPROVED: transactions must be protected against replay across networks. The
mechanism is OPEN RESEARCH. Key considerations:
- A hard fork creating two independent networks must prevent a transaction valid on one
  from being replayed on the other.
- Chain-ID binding (EIP-155 style) is the clearest account-based mechanism.
- For UTXO/eUTXO, replay protection can be achieved by (a) chain ID in the signing
  message, (b) fork-specific output/cell identifiers, or (c) requiring transactions to
  reference post-fork UTXOs/cells (fragile).
- Nonce-based replay protection (account-based) does not by itself prevent cross-chain
  replay if both chains share genesis and nonce space; distinct chain IDs are required.
- Scope limitation: no claim of complete replay protection during arbitrary network
  partitions; offline operation (D-018) is bounded and reconciled later.

### 5.5 Stateless validation versus state-dependent validation

- **Stateless (UTXO, eUTXO, object):** validity is a pure function of the transaction and
  its referenced inputs. Enables off-chain pre-validation, parallel validation, and
  deterministic fee prediction. This is the research consensus across inspected sources.
- **State-dependent (account-based):** validity requires reading and mutating global state
  (balance, nonce, storage). Introduces serialization, ordering sensitivity, and
  non-determinism for the submitter. This is the central scalability/usability trade-off.

### 5.6 Transaction admission and rejection

Admission rules must be split by layer to avoid conflating policy with consensus:
- **Protocol validity:** malformed format, bad signature, double-spend, insufficient
  value/fee → invalid everywhere.
- **Mempool admission (node-local):** non-standard scripts, below minimum relay fee,
  mempool-full, replacement policy → a node may reject locally without consensus impact.
- **Block inclusion:** proposer selects from mempool; not a validity rule.
- **Consensus:** ordering and finality determine which conflicting transaction wins.

### 5.7 Mempool state and conflict handling

- **UTXO:** mempool keyed by txid; conflict = same input spent twice; replacement via
  RBF (BIP-0125, inspected: opt-in, replacement pays higher absolute fee + bandwidth,
  max 100 transactions evicted). Duplicate detection by txid.
- **Account-based:** mempool keyed by (sender, nonce); conflict = same nonce; replacement =
  same nonce with higher fee. Future-nonce transactions held until prior nonces fill.
- **eUTXO/object:** conflict = same cell/object consumed; replacement policy model-dependent.

### 5.8 Transaction dependency graphs

Transactions form a DAG. Descendant transactions (spending unconfirmed outputs) create
chains. Malleability (non-SegWit UTXO) breaks descendants by changing txid. Account-based
nonce gaps create waiting periods. Orphan/dependency handling is a mempool design concern,
not consensus.

### 5.9 Duplicate detection

By transaction identifier (txid/tx hash). For account-based, also by (sender, nonce).
Duplicate detection prevents rebroadcast of identical transactions; double-spend attempts
with different identifiers are conflicts, not duplicates.

### 5.10 Expiration and validity windows

- UTXO: locktime (block height or Unix time) defines earliest inclusion; sequence numbers
  disable locktime. No native expiry (UTXOs persist until spent).
- Account-based: no native expiry; nonce is the ordering mechanism. Some L2s add
  deadlines.
- eUTXO/object: optional validity bounds (start/end block) are a design option.
- Expiration is relevant to mempool eviction and offline spending; not selected for FST.

### 5.11 Replacement and cancellation mechanisms

- **Bitcoin RBF (BIP-0125, inspected):** opt-in full-RBF via nSequence < 0xfffffffe.
  Replacement must pay >= original absolute fee + bandwidth (min relay fee × size), and
  evict <= 100 transactions including descendants. Inherited signaling via unconfirmed
  ancestors. Receiving wallets should treat opt-in-RBF transactions as unconfirmed until
  confirmed.
- **Account-based nonce replacement:** same nonce, higher gas price replaces pending tx.
  No explicit "cancel" except a same-nonce zero-value self-transfer.
- Neither is an approved FST policy; both are research examples only. FST must not adopt
  a replacement policy before owner decision.

### 5.12 Fee calculation and prioritization

- UTXO: fee = input value - output value; prioritized by fee-per-byte/vbyte.
- Account-based: fee = gas used × effective gas price (base + priority under EIP-1559).
- eUTXO/object: size/resource-based; payer may be delegated (Nervos).
- D-015 (monetary policy) and fee parameters are OPEN RESEARCH; no fee market is
  selected here.

### 5.13 Propagation between authenticated peers

FST already establishes authenticated TLS 1.3 sessions with node-record binding (Milestone
2D). Transactions propagate as application-layer payloads over these sessions. A node
should not forward a transaction it has not validated for syntactic well-formedness
(per core protocol spec Section 8, proposed). Propagation scope (gossip vs. direct to
proposers) is unresolved (RS-013 component 2.5).

### 5.14 Confirmation, finality and reorganization implications

Confirmation = inclusion in a block. Finality and reorg handling depend on the unresolved
consensus algorithm (D-017). Reorganization implications:
- UTXO: reorg re-unspends outputs; transactions in reorged blocks return to mempool; a
  double-spend that was confirmed may become valid again on the other chain.
- Account-based: reorg reverts nonce/balance changes; replay risk across the reorg
  boundary if chain ID is shared.
- Cross-fork replay (D-036) must prevent a confirmed transaction on one fork from being
  replayed on the other.

## 6. Security and privacy analysis

### 6.1 Transaction authorization and signature verification

- Authorization rests on digital signatures over transaction data. The security of the
  model depends on the signature scheme (ECDSA/secp256k1 or Ed25519), key management, and
  domain separation (FST uses domain-separated signing in established implementation).
- Model-provided: unforgeability under the signature assumption. Additional protocol
  mechanisms required: key storage, rotation, recovery, threshold/multisig (RS-004,
  OPEN RESEARCH).

### 6.2 Replay attacks and fork separation

- D-036 (APPROVED) requires replay protection across networks and distinct currency
  identifiers. Mechanisms OPEN RESEARCH.
- Model-provided: account-based EIP-155 chain-ID binding prevents cross-chain replay where
  chain IDs differ. UTXO/eUTXO/object require added binding.
- Additional required: distinct chain ID per fork (D-036), currency identifier
  separation, wallet/exchange awareness of distinct currencies.
- Limitation: no complete replay protection under arbitrary network partitions claimed;
  offline operation bounded (D-018).

### 6.3 Double-spend attacks

- Model-provided: native single-spending (UTXO consumption, account nonce, eUTXO cell
  consumption, object versioning).
- Additional required: consensus ordering to resolve conflicts; confirmation finality to
  make double-spends economically infeasible.
- Limitation: no double-spend prevention during network partitions without sacrificing
  liveness (CAP/FLP; scope limitation noted).

### 6.4 Nonce and sequence manipulation

- Account-based nonce: replay within a chain prevented by nonce ordering; nonce gaps
  cause mempool waiting; nonce exhaustion is not an issue (64-bit) but nonce management
  is a UX burden (offline transactions become invalid if nonce advances).
- UTXO sequence numbers: originally for replacement, now mostly disable locktime; not a
  security mechanism.
- Model-provided vs additional: nonce ordering is model-provided; replay protection across
  forks requires chain ID (additional).

### 6.5 Malformed transactions

- Model-provided: syntactic validation rejects malformed transactions (format, signature,
  value conservation).
- Additional required: mempool limits, per-type size limits (FST already has these for
  baseline message types), error handling that does not leak internals (spec Section 10,
  proposed).

### 6.6 Signature malleability

- UTXO: signature script not covered by signature hash → txid malleability (SegWit
  mitigates by excluding witness from txid).
- Account-based: raw ECDSA s-value malleability (s vs -s) mitigated by canonical-s
  enforcement in clients.
- Model-provided vs additional: malleability resistance requires SegWit-style separation
  or canonical-signature rules (additional design choice).

### 6.7 Resource-exhaustion through transaction submission

- Mempool flooding: attackers submit low-fee or high-complexity transactions.
- Model implications: account-based gas limits bound computation per transaction; UTXO
  byte fees bound size; script complexity must be bounded (Turing-incomplete scripts in
  Bitcoin prevent unbounded computation).
- Additional required: min relay fee, mempool size limits, eviction policy, per-peer rate
  limiting (threat model T-05, partially mitigated).

### 6.8 Mempool flooding and adversarial fee behavior

- Fee-rate auctions can price out honest users during congestion; spam with low fees can
  fill mempools.
- EIP-1559 base-fee burn addresses miner fee manipulation but does not eliminate spam.
- Replacement policies (RBF) can be abused for fee bumping; opt-in signaling limits
  recipient risk.
- No fee market selected for FST (D-015 OPEN RESEARCH).

### 6.9 Front-running and transaction ordering

- Account-based: public mempool and ordering by gas price enable front-running and MEV.
- UTXO: less MEV surface for simple payments but miner ordering of conflicting
  transactions exists.
- Additional required: ordering policy, commitment schemes, or private transaction
  channels if front-running is a concern (not selected).

### 6.10 Transaction metadata leakage

- Amounts, sender, recipient, timestamps, and fees are public on-chain in all four models
  (without privacy layers).
- UTXO graph analysis can link addresses; account-based public balances leak holdings.
- D-020 (strong cryptographic privacy) is PROVISIONAL; mechanisms OPEN RESEARCH (RS-003).
- No claim of transaction privacy; authenticated encryption is not financial privacy
  (scope).

### 6.11 Address and identity reuse

- UTXO: address reuse enables tracking; best practice is fresh addresses (Bitcoin devguide,
  inspected).
- Account-based: stable addresses are the norm; reuse is inherent and leaks history.
- eUTXO/object: similar to UTXO; object/cell reuse enables tracking.
- Key rotation and recovery (RS-004, OPEN RESEARCH) affect long-term address management.

### 6.12 Privacy: account histories vs UTXO graphs

- Account-based: a single address reveals complete balance and transaction history;
  strongest linkability.
- UTXO/eUTXO/object: graph analysis required to link outputs; non-reuse improves privacy
  but change-output patterns leak.
- Both are pseudonymous, not anonymous; privacy layers (mixing, confidential
  transactions, ZK proofs) are future work (RS-003).

### 6.13 Key rotation and recovery implications

- UTXO: keys are per-output; rotation means sending to new outputs; recovery from lost key
  = loss of funds (no central authority, P8).
- Account-based: one key controls the account; rotation = new address or key-change
  mechanism; recovery = same loss model.
- No central emergency administrator (P8) means lost keys are unrecoverable at the
  protocol level; wallet-level recovery is out of scope.

### 6.14 Offline spending and reconciliation limitations

- Offline signing is possible in all models, but validity at confirmation time is not
  guaranteed (UTXO may be spent; account nonce may advance).
- Scope limitation: unrestricted local spending and global double-spend prevention under
  arbitrary partitions cannot both be guaranteed (scope-v0.1.md).
- Reconciliation after reconnection requires consensus recovery (D-018, RS-007).

## 7. Mempool and fee-market comparison

### 7.1 Mempool design considerations by model

| Aspect | UTXO | Account-based | eUTXO | Hybrid/object |
|--------|------|---------------|-------|---------------|
| Keying | txid | (sender, nonce) | txid | tx hash |
| Conflict | Same input | Same nonce | Same cell | Same object |
| Replacement | RBF (opt-in, BIP-125) | Same-nonce higher fee | Model-dependent | Model-dependent |
| Duplicates | By txid | By (sender, nonce) | By txid | By tx hash |
| Dependency | Output chains | Nonce gaps / future nonce | Cell chains | Object versions |
| Orphan handling | Hold by missing inputs | Hold by missing prior nonces | Hold by missing cells | Hold by missing objects |
| Complexity | Moderate | Higher (state-dependent) | Moderate | Moderate-High |

### 7.2 Fee-market alternatives

| Approach | Description | Economic implication | Computational implication | DoS implication |
|----------|-------------|---------------------|---------------------------|-----------------|
| Fixed / minimum fee | Constant fee per transaction or byte | Predictable cost; may underprice congestion | Simple validation | Low-fee spam possible if below cost |
| Size/resource-based | Fee proportional to bytes or compute units | Cost aligns with resource consumption | Requires resource metering | Bounded by fee per resource unit |
| Fee-rate auction | Users bid; miners pick highest bids | Volatile prices; miner extractable value | Fee estimation complexity | High-fee spam displaces honest txs |
| Dynamic base fee | Protocol-adjusted fee burned (EIP-1559) | More predictable; burn reduces supply | Base-fee adjustment logic | Base fee sets spam floor |
| Priority fee + cap | Tip for inclusion + max total fee | User cost certainty; priority market | Cap enforcement logic | Cap may prevent inclusion during congestion |

- D-015 (monetary policy, issuance, rewards, fee parameters) remains OPEN RESEARCH. No
  fee market is selected. The table records alternatives only.

### 7.3 Fee-market recommendation status

No fee market is recommended as approved. If an account-based model is selected,
EIP-1559-style base fee + priority fee is a plausible starting point; if UTXO, a
per-byte/vbyte fee with optional RBF is plausible. Both require owner decision and
economic analysis (outside this milestone).

## 8. Financial use-case suitability

| Use case | UTXO | Account-based | eUTXO | Hybrid/object |
|----------|------|---------------|-------|---------------|
| Native FST transfers | Strong | Strong | Strong | Strong |
| Person-to-person payments | Strong (fresh addresses) | Strong (stable addresses) | Strong | Strong |
| Merchant payments | Strong | Strong | Strong | Strong |
| Multiple-input institutional payments | Moderate (multi-sig coordination) | Strong (aggregate balance) | Strong (multi-cell) | Strong (multi-object atomic) |
| Batch payment processing | Strong (multi-output) | Strong (multiple txs or contract batch) | Strong | Strong |
| High-volume transfers | Strong (parallelism) | Moderate (state contention) | Strong (parallelism) | Strong (parallelism, hot-object contention) |
| Settlement | Strong | Strong | Strong | Strong |
| Independent wallets/clients | Strong | Strong | Moderate-High (complexity) | Moderate-High (complexity) |
| Future financial services | Moderate (needs extensions) | Strong (native smart contracts) | Strong (programmable locks) | Strong (extensible objects) |

### Additional complexity per model for FST's scope

- **UTXO:** change-output management, UTXO set maintenance, address non-reuse UX, multi-input
  institutional coordination. Low additional complexity for pure payments.
- **Account-based:** nonce management, state tree, state-dependent validation, front-running
  exposure, weaker privacy. Moderate additional complexity for pure payments; low for
  batch/institutional.
- **eUTXO:** datum/cell management, script validation, larger transactions, off-chain
  service needs for shared state. Moderate-High additional complexity.
- **Hybrid/object:** object versioning, state rent, authorization scripts, sharding. High
  additional complexity for pure payments.

FST's initial scope is payments and settlement, not smart contracts. For this scope,
UTXO and account-based are the most directly suitable; eUTXO and object models add
complexity that is only justified if programmable money or shared on-chain state is
required.

## 9. Storage and scalability implications

### 9.1 State size

- **UTXO:** proportional to unspent output count. Change outputs increase count; spending
  decreases it. Prunable (spent outputs removed). Well-understood scaling behaviour.
- **Account-based:** proportional to account count + contract storage. Grows monotonically;
  pruning is harder because accounts are mutable and referenced by contracts. State tree
  (MPT) maintenance overhead.
- **eUTXO:** proportional to live cell count. Datum storage can bloat; state rent (Nervos)
  can bound growth.
- **Hybrid/object:** proportional to object count + versions + metadata. State rent or
  version pruning needed to bound growth.

### 9.2 Storage requirements for full vs light nodes

- Full nodes: maintain the complete state (UTXO set / account state / live cells /
  objects) plus history (archival).
- Light nodes: verify via proofs (Merkle inclusion of UTXO/account/cell/object). UTXO SPV
  is simplest; account-based MPT proofs are more complex.

### 9.3 Scalability summary

- **Parallel execution:** UTXO, eUTXO, and object models support parallel validation of
  non-conflicting transactions; account-based does not (shared state, nonce order).
- **Sharding:** easier in non-global-state models (UTXO/eUTXO/object); research notes
  difficulty sharding account-based L1.
- **Layer-2:** all models support channels/rollups; UTXO has Lightning as precedent.

## 10. Implementation complexity and interoperability

### 10.1 Independent implementation complexity (P4)

- **UTXO:** lowest-to-moderate. Simple deterministic script language; multiple independent
  implementations (Bitcoin Core, etc.); well-documented. Easiest for independent teams to
  implement correctly for payments.
- **Account-based:** moderate-to-high. Requires state tree, VM (if contracts), nonce and
  fee-market logic; state-root correctness is critical and error-prone.
- **eUTXO:** moderate-to-high. Script validator, cell dependencies, datum handling.
- **Hybrid/object:** high. Object model, versioning, state rent, authorization.

### 10.2 Interoperability and test vectors

- Independent implementations require canonical serialization, transaction identifier
  format, and validation rules to be precisely specified (test vectors, conformance
  harness — RS-012 / D-035, threat model T-013).
- UTXO serialization is byte-level and straightforward to specify; account-based RLP/typed
  envelopes require versioning discipline; eUTXO/object require canonical cell/object
  serialization.
- No test vectors exist yet; this is future work, not part of this milestone.

## 11. Candidate architectures for FST

Two credible candidates are presented. Neither is selected; both remain PROVISIONAL
pending owner decision (RS-015 OPEN RESEARCH).

### Candidate 1 — Traditional UTXO model with chain-ID binding

**Description:** State is a UTXO set. Transactions consume unspent outputs and create new
outputs (including change). Authorization is by satisfying locking scripts (signatures).
Replay protection across networks is added via chain-ID binding in the signing message
(D-036 mechanism OPEN RESEARCH). Fees are per-byte/vbyte.

**When preferable:**
- FST prioritizes parallel validation, deterministic off-chain checking, predictable fees,
  and privacy through address non-reuse.
- The scope remains payments and settlement without general smart contracts.
- Independent implementation simplicity is valued (P4).
- UTXO-set growth and change-output UX are acceptable trade-offs.

**Key unresolved items:** chain-ID replay-protection mechanism, fee-per-byte rates, RBF
policy, UTXO-set pruning policy, light-client proof format.

### Candidate 2 — Account-based model with nonce plus chain-ID

**Description:** State is a global account/balance map with per-account nonces.
Transactions are signed instructions (from/to/value/nonce/fee/data). Authorization is a
signature over the transaction with chain-ID binding (EIP-155 style) for replay protection
(D-036 mechanism OPEN RESEARCH). Fees are gas-based.

**When preferable:**
- FST prioritizes simple multiple-input institutional and batch payments, simple wallet UX
  (stable addresses, aggregate balance), and native support for high-volume transfers from
  a single party.
- Implementation complexity and state-contention trade-offs are acceptable.
- Future extensibility to account abstraction or smart contracts is desired.

**Key unresolved items:** nonce management UX, state-tree design, gas/fee market,
front-running exposure, privacy weaknesses of stable addresses, state pruning.

### Other evaluated alternatives (not primary candidates)

- **eUTXO:** credible if FST later requires programmable money or shared on-chain state
  within a UTXO paradigm. Adds datum/cell complexity not justified by the initial
  payments-only scope. Kept as a fallback if smart-contract needs emerge.
- **Hybrid/object-cell:** credible for a future asset-rich network with first-class assets
  and delegated fee payment. Highest complexity; not justified for Milestone 2F scope.

## 12. Advantages, disadvantages and unresolved risks

### 12.1 UTXO

Advantages: native double-spend prevention; parallel validation; deterministic off-chain
validation; predictable fees; prunable state; SPV-friendly; privacy via non-reuse;
mature independent implementations.

Disadvantages: change-output pattern; UTXO-set growth; multi-input coordination; no
natural global balance; malleability (mitigated by SegWit-style design); weaker fit for
shared mutable state.

Unresolved risks: cross-fork replay protection mechanism (D-036); UTXO-set bloat under
high volume; fee-market economics (D-015).

### 12.2 Account-based

Advantages: simple global balance; natural multi-input/batch; simple wallet UX;
straightforward replay protection (nonce + chain ID); highly extensible; native smart
contract support.

Disadvantages: global shared mutable state; state-dependent validation; limited
parallelism; nonce management; front-running/MEV; weaker privacy; harder state pruning;
higher implementation complexity.

Unresolved risks: nonce UX under offline operation; state bloat; fee-market (D-015);
front-running; cross-fork replay (D-036).

### 12.3 Cross-cutting unresolved risks

- **No model provides anonymous privacy or partition-safe double-spend prevention** (scope
  limitation explicitly noted; D-018, P9).
- **D-036 mechanisms** (chain-ID format, signing scheme, currency identifiers) are OPEN
  RESEARCH and apply to all candidates.
- **D-015 monetary policy** (fees, issuance, rewards) is OPEN RESEARCH; no fee market
  selected.
- **D-017 consensus** is OPEN RESEARCH; finality and reorg handling are model-dependent
  but not specified.
- **Offline spending** (D-018) requires bounded authority and reconciliation; not solved
  by any model alone.
- **Independent implementation** (RS-012 / D-035) requires test vectors and conformance
  harnesses after the model is selected.

## 13. Evidence-backed provisional recommendation

**Provisional recommendation (NOT approved, owner decision required):**

Adopt **Candidate 1 — a traditional UTXO model with chain-ID binding for replay
protection** as the baseline direction for FST, with **Candidate 2 — account-based with
nonce plus chain-ID — as the credible alternative** to be re-evaluated if institutional
multi-input batching or future smart-contract needs dominate.

**Reasoning:**
1. FST's initial scope is payments and settlement with FST only (D-014). UTXO is a
   natural, minimal fit for payments with the lowest implementation complexity (P4),
   supporting independent teams (P4, P5).
2. UTXO provides native double-spend prevention, parallel validation, and deterministic,
   predictable fees — valuable for a financial network where settlement certainty matters
   (P9).
3. Privacy via address non-reuse aligns with the PROVISIONAL privacy direction (D-020)
   better than account-based stable addresses (RS-003).
4. Account-based remains the better fit if FST later prioritizes complex multi-party
   batching or programmable money; eUTXO/hybrid are fallbacks if smart-contract needs
   emerge.

**This recommendation is PROVISIONAL.** It does not select a transaction format, nonce
model, fee policy, or consensus mechanism. RS-015 remains OPEN RESEARCH until the owner
resolves it.

## 14. Questions requiring owner decisions

1. **RS-015 (this research):** Select transaction model (UTXO / account-based / eUTXO /
   hybrid) or confirm provisional direction. Required before transaction serialization,
   mempool, and dissemination can be specified.
2. **D-036 mechanisms:** Chain-ID format, signing scheme, and distinct currency identifiers
   for replay protection across forks. Required for any transaction model.
3. **D-015 monetary policy / fee market:** Fee calculation, fee rates, minimum relay fee,
   replacement policy. Required before mempool design.
4. **D-017 consensus / finality:** Consensus algorithm and finality model; affects
   reorganisation and confirmation semantics.
5. **D-018 offline operation:** Bounded offline spending authority and reconciliation
   mechanism; affects offline transaction validity.
6. **RS-003 privacy:** Specific transaction-confidentiality mechanisms; affects privacy
   model selection.
7. **Transaction serialization format** and **transaction identifier format** (core
   protocol spec Section 8 open questions 2, 7).
8. **Replacement / cancellation policy** (RBF vs nonce replacement vs none) — must not be
   adopted before owner decision.

## 15. References and source-verification status

Sources independently inspected (full text read via official documentation) are marked
**INSPECTED**. Sources encountered only via search-result snippets/abstracts (not
independently inspected) are marked **NOT INSPECTED — metadata only**. No sources are
fabricated.

| Ref | Title | Author(s) | Date | Publisher / venue | URL | Status | What it supports | Limitations / bias |
|-----|-------|-----------|------|-------------------|-----|--------|------------------|--------------------|
| R1 | Bitcoin Developer Guide — Transactions | Bitcoin Project | 2009–2020 (page) | bitcoin.org / Bitcoin documentation | https://developer.bitcoin.org/devguide/transactions.html | INSPECTED | UTXO model, transaction structure, P2PKH, fees, change, malleability, RBF context, locktime/sequence, key reuse/privacy | Project-maintained; Bitcoin-specific; describes `IsStandard()` policy not consensus |
| R2 | BIP-0125: Opt-in Full Replace-by-Fee Signaling | D. A. Harding, P. Todd | 2015-12-04 | Bitcoin BIPs (github.com/bitcoin/bips) | https://github.com/bitcoin/bips/blob/master/bip-0125.mediawiki | INSPECTED | Opt-in RBF rules, replacement fees, inherited signaling, receiving/spending wallet policy | Describes a mempool policy, not consensus; deployment status historical |
| R3 | Ethereum.org — Transactions | Ethereum Foundation / contributors | 2026 (page) | ethereum.org | https://ethereum.org/developers/docs/transactions/ | INSPECTED | Account-based transaction fields, gas, EIP-1559 fees, typed envelopes, lifecycle | Project-maintained; describes current Ethereum including post-1559 mechanics |
| R4 | EIP-155: Simple replay attack protection | V. Buterin | 2016-10-14 | Ethereum EIPs | https://eips.ethereum.org/EIPS/eip-155 | INSPECTED | Chain-ID binding in signatures for cross-chain replay protection | Chain-specific (Ethereum/ETC); `v` formula details |
| R5 | EIP-1559: Fee market change | V. Buterin et al. | 2019-04-13 | Ethereum EIPs | https://eips.ethereum.org/EIPS/eip-1559 | INSPECTED | Base fee + priority fee, burn, dynamic adjustment, security considerations | Proposes burning (monetary implication); Ethereum-specific |
| R6 | Cardano — Extended UTXO model (explainer) | IOHK / Cardano Foundation | 2026 (page) | docs.cardano.org | https://docs.cardano.org/about-cardano/learn/eutxo-explainer | INSPECTED | EUTXO extensions (scripts as locks, datum), local validation, determinism, parallelism, fee predictability | Project-maintained; advocacy-oriented; advantages listed by the project |
| R7 | Cardano developers — eUTXO technical concepts | IOHK | 2026 (page) | developers.cardano.org | https://developers.cardano.org/docs/get-started/technical-concepts/eutxo/ | NOT INSPECTED (page returned no content on fetch) | eUTXO technical concepts | Not independently verified; listed from mission starting references only |
| R8 | Nervos CKB — Cell Model | Nervos Foundation | 2026-11-04 (page) | docs.nervos.org | https://docs.nervos.org/docs/ckb-fundamentals/cell-model | INSPECTED | Cell model, first-class assets, flexible fee payment, parallel execution, state rent | Project-maintained; CKB-specific |
| R9 | Key Features and Formation of Transactions in the Case of Using UTxO, EUTxO and Account Based Data Storage Models | (conference paper) | 2022-10 | MONeTec (IEEE) | https://doi.org/10.1109/monetec55448.2022.9960753 | NOT INSPECTED (search snippet only) | Comparison of account-based vs (E)UTxO storage/transaction models | Abstract only; not independently read |
| R10 | Extended UTxO in production: techniques, trade-offs and a search of better balance | (spectrum.fi) | (undated) | spectrum.fi | https://spectrum.fi/eutxo_in_production.pdf | NOT INSPECTED (search snippet only) | eUTxO vs programmable accounts, determinism vs data-transmission trade-off | Abstract only; not independently read |
| R11 | DeFi vulnerabilities in the eUTxO model | (University of Athens) | (undated) | pergamos.lib.uoa.gr | https://pergamos.lib.uoa.gr/uoa/dl/object/3417728/file.pdf | NOT INSPECTED (search snippet only) | eUTxO vs account model formal comparison | Abstract only; not independently read |
| R12 | Blockchain scalability for smart contract systems | (arxiv) | 2022 | arXiv | https://arxiv.org/pdf/2202.00561 | NOT INSPECTED (search snippet only) | UTXO vs account scalability, sharding difficulty for account-based | Abstract only; not independently read |
| R13 | UTxO- vs account-based smart contract blockchain programming paradigms | (Gabbay / University of Kent) | 2020-03-31 | arXiv | https://arxiv.org/abs/2003.14271 | NOT INSPECTED (search snippet only) | Imperative (account) vs functional (UTXO) programming paradigms; error classes | Abstract only; not independently read |
| R14 | Chimeric Ledgers: Translating and Unifying UTXO-based and Account-based Cryptocurrencies | (IACR ePrint) | 2018 | IACR ePrint | https://eprint.iacr.org/2018/262 | NOT INSPECTED (search snippet only) | Formal definitions of both styles, translations, hybrid transaction types | Abstract only; not independently read |
| R15 | DiSCo_sim: a simulator for a hybrid UTXO-/account-based blockchain | (arxiv) | 2024-06-11 | arXiv | https://doi.org/10.48550/arxiv.2406.07700 | NOT INSPECTED (search snippet only) | eUTxO state distribution across UTXOs; on-chain vs off-chain trade-off | Abstract only; not independently read |

**Source-verification note:** R1–R6 and R8 were independently inspected via their official
documentation. R7 was referenced in the mission but returned no content on fetch and is
not independently verified. R9–R15 were identified via web search and are recorded from
snippets/abstracts only; their full texts were not inspected. No conclusions in this
document depend on an uninspected source alone; where uninspected sources are cited, the
claim is attributable to the inspected sources (R1–R6, R8) or framed as general
architectural reasoning.

## Cross-references

- `docs/fst/research/research-register.md` — Research register (adds RS-015).
- `docs/fst/research/research-dependency-assessment.md` — Dependency assessment (Priority 1 corrected to RS-015).
- `docs/fst/spec/core-protocol-specification-v0.1.md` — Core protocol specification
  Section 8 (transaction dissemination boundaries) references this research.
- `docs/fst/decisions/decision-register.md` — Decision register (D-015, D-016, D-017,
  D-018, D-020, D-036).
- `docs/fst/scope/scope-v0.1.md` — FST project scope (D-014, D-036, offline operation,
  privacy).
- `docs/fst/principles/engineering-principles.md` — Approved principles (P1–P14).
- `docs/fst/security/threat-model-v0.1.md` — Threat model (T-06 replay, T-05 DoS).
