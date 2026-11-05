# Native FST Ledger / Settlement bridge (v0.4.0)

## Scope

The Bridge coordinates native `FST` transfers between the local Ledger and the off-chain `FinancialPlatform`. It is a single-node prototype feature, not a public chain bridge, smart contract, custodian, payment rail, or production financial service. `USD` is not bridgeable. A locally included block is the only confirmation signal; there is no peer network or distributed finality policy.

The bridge uses one dedicated, immutable vault wallet. The vault address cannot also be a customer SettlementBook account. The vault's encrypted wallet secret stays in the existing Ledger wallet store; the platform snapshot stores only the public address and signed pending transaction data. Passphrases are entered at withdrawal time and are not persisted.

## Deposit workflow

1. Create or select a dedicated Ledger vault wallet, and configure its address under `Shared financial platform -> Ledger / Settlement bridge -> Configure dedicated vault wallet`.
2. A registered Ledger customer wallet sends native FST to the vault using the ordinary Ledger transfer flow. The transaction must be mined into the local chain before it can be claimed.
3. Copy the transaction ID from the Ledger transfer output.
4. Claim that transaction ID through `Claim confirmed FST deposit`.
5. `BridgeService` retrieves the transaction from the validated local chain, checks its signature, native asset, positive amount, exact vault recipient, non-vault sender, and registered wallet identity, then records a `bridge-deposit` journal entry and credits the sender's FST settlement account.

Deposit credit is idempotent by 64-character transaction ID. Repeating a claim returns “already credited” and does not add another balance credit or journal revision. Unconfirmed transactions, transfers to a different recipient, vault self-transfers, and sender addresses without a stored Ledger wallet identity are rejected.

The deposit chain append and platform credit are separate commits. If the process stops after the on-chain transfer is mined but before the claim is committed, the same transaction ID can be claimed after restart. The bridge does not claim distributed atomicity.

## Collateral model

The vault must cover the total off-chain FST liabilities and any other outgoing transactions already waiting in the vault mempool:

- FST `total` balances in every SettlementBook account, including any Match-reserved amounts.
- FST reserves held by every Flow pool. Pool reserves are counted separately because liquidity has left account balances.

Match reservations are already part of account `total`; they are not counted a second time. A pool reserve is counted once in addition to account balances. Internal transfers, Match execution, Flow swaps, and liquidity movements transfer FST between these locations and should not change aggregate liabilities. Synthetic test credits create additional off-chain liabilities without adding on-chain collateral; a bridge can therefore become undercollateralized when synthetic FST is used.

For a new withdrawal with payout amount `A` and Ledger fee `F`, the vault must have at least:

`current off-chain FST liabilities + F`

The customer's off-chain account is debited by `A`. The exact signed vault transaction reserves `A + F` until it is included in the Ledger chain. While it remains unconfirmed, required collateral is:

`current off-chain FST liabilities + pending signed withdrawal amount + pending fee + unrelated outgoing vault mempool amount/fee`

This keeps funds committed to an in-flight transaction from being reported as free surplus. If the withdrawal transaction is already in a block but the bridge snapshot still says pending, status/verifier recognize the on-chain transaction as spent collateral and show it as awaiting reconciliation.

## Withdrawal workflow and recovery

1. The recipient must be a registered Ledger wallet and a SettlementBook account with enough available FST.
2. The operator unlocks the configured vault wallet using hidden terminal passphrase input.
3. `BridgeService` checks vault identity, available customer funds, the one-pending-withdrawal rule, aggregate liabilities, and vault collateral plus the fee.
4. The service creates and preflights a native-FST Ledger transaction.
5. Before mempool submission, one platform snapshot commits the customer FST debit and the exact signed transaction bytes as a pending withdrawal.
6. The transaction is submitted to the Ledger mempool. Mine a block to confirm it.
7. After the block append, the application reconciles the bridge record to `Confirmed`. On entry to the Bridge menu, it also reconciles a transaction that was mined immediately before a process interruption.

The Ledger mempool is durable from v0.6.0. After restart, a pending withdrawal transaction normally recovers from SQLite and `Retry pending withdrawals` reports it as already pending rather than submitting a duplicate. If a bridge debit/record was committed but the Ledger mempool insert did not complete, retry resubmits the exact persisted signed transaction, not a newly signed transaction. If the transaction is already present in a block, retry/reconciliation verifies the persisted payload against the confirmed transaction and marks it confirmed without a duplicate payout.

There is at most one pending withdrawal. This keeps vault nonce sequencing simple and avoids accepting a later withdrawal while an earlier signed transaction owns the next nonce. A signed pending withdrawal is not cancellable. Resolve it by retrying/rebroadcasting and mining it, or by investigating a durable transaction error; do not manually spend the vault nonce elsewhere.

## Operational constraints

- The configured vault is dedicated. Do not use it as a customer account, normal transfer sender, or routine mining wallet.
- Fund the vault using confirmed native-FST transfers before expecting withdrawals to work.
- Keep the vault wallet's local encrypted-secret backup and passphrase safe. FeinStatic does not provide a hardware security module, multisignature control, remote signer, or recovery service.
- Synthetic FST test credits are not backed deposits. The status command reports liabilities and collateral shortfall; withdrawals are blocked when the vault is undercollateralized. If an existing v0.3.0 database contains synthetic FST balances and the operator wants to collateralize those *existing* liabilities, send native FST on-chain into the dedicated vault but do **not** claim that funding transaction as a customer deposit. Claiming it always credits the sender's settlement account by the transaction amount and therefore creates an additional liability; it does not convert or replace an earlier synthetic credit. A claim is for a new deposit that should increase both vault collateral and the sender's off-chain balance.
- `feinstatic verify <database>` is a non-interactive integrity diagnostic that opens a fresh Ledger instance. It does not credit/debit settlement balances, submit transactions, mine blocks, or reconcile bridge records. It uses the normal SQLite store-open path (including WAL-mode enforcement and legacy schema initialization where applicable), so it is not a forensic read-only database handle. It cross-checks every durable deposit/withdrawal history entry against the exact transaction and block height in the chain, and detects a transaction already mined but awaiting reconciliation; it loads the durable mempool and includes pending unrelated vault outflows in its collateral calculation. Unconfirmed mempool transactions survive process exit but do not change confirmed chain balances. Entering the Bridge menu persists reconciliation of already-mined withdrawals.
- The bridge records a locally confirmed block, not irreversible distributed finality. v0.8.0 peer synchronization protects Bridge transaction IDs and pinned confirmation heights against local reorganization; this is not peer consensus or an external proof.

## Not implemented

No token wrapping, asset conversion, USD rails, smart contracts, cross-chain relay, multi-node consensus, proof aggregation, external wallet signing, multi-signature vault, production transaction finality, or real customer funds.
