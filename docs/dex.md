# Flow / Automated Market Maker

Status: **implemented with persistent off-chain pools, LP positions, and shared settlement through FinancialPlatform**.

## Pool model

Each pool stores two distinct asset IDs, reserves, total LP shares, and a fee in basis points. Duplicate and reversed asset pairs are rejected. Fees are capped at 10 percent.

## Swap arithmetic

The constant-product quote with basis-point fee scaling is:

```text
weighted_input = amount_in * (10_000 - fee_bps)
denominator   = reserve_in * 10_000 + weighted_input
amount_out    = floor(reserve_out * weighted_input / denominator)
```

The fee retained by the pool is `amount_in - floor(weighted_input / 10_000)`. Boost.Multiprecision fixed-width `uint256_t` intermediates protect valid `uint64_t` reserve/input values from intermediate overflow; stored balances remain checked 64-bit integer atoms. No binary floating-point value controls a state transition.

Price impact is measured in basis points against pre-swap spot output and includes both curve impact and the configured swap fee. The caller supplies `minimum_amount_out`; execution rejects a quote below it before changing account or pool state.

## Liquidity

- Initial shares are `floor(sqrt(amount_x * amount_y))`.
- Later contributions mint the limiting proportional share amount; exact consumed amounts are calculated with integer rounding and unused maxima stay in the provider account.
- Redemption is proportional and floors outputs to integer atoms. Full-supply redemption returns all remaining reserves.
- LP positions are tracked by `(pool_id, account_id)` and their sum must equal total LP shares.

## Shared settlement and persistence

Flow reads/writes the same SettlementBook as Match. Swaps debit available input assets and credit output assets; add-liquidity debits available assets; removal credits the provider. Match reservations remain unavailable to Flow. This shared accounting makes off-chain Match proceeds usable by Flow and Flow output available to Match, subject to each engine's validation and order reservations.

Pools, reserves, LP positions, swap history, sequence counters, and shared balances are committed in the same revisioned/checksummed SQLite platform snapshot. A failed quote/slippage/balance/accounting operation does not advance the revision or partially change platform state.

These are application-level off-chain transitions, not deployed smart contracts. Linking a Ledger wallet address does not prove funds were deposited into a pool. v0.4.0 adds a separate native-FST Ledger/Settlement bridge, but it does not make Flow on-chain, provide oracle pricing, or create production custody.
