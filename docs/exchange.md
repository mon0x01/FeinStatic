# Match / Exchange Engine

Status: **implemented with persistent off-chain settlement through FinancialPlatform**.

## Market units

The integrated market is `FST/USD`:

- Quantity is a whole number of FST lots.
- One base lot is `kAtomsPerCoin` (100,000,000 FST atoms).
- Price is USD atoms per one whole FST lot.
- Trade notional is `price * quantity`, with checked `uint64_t` multiplication.
- No floating-point quantity, price, or settlement amount is used.

## Order book and priority

Bids are ordered by descending price; asks by ascending price. Each price level stores order IDs in FIFO submission order. The first active order at the best opposing price is the maker. A crossing taker executes at the resting maker's price.

## Order behavior

- Limit orders that do not fully execute rest in the book.
- Market buys take a quote-asset budget; market sells take a base-lot quantity.
- Market orders use immediate-or-cancel behavior; unfilled remainder is released and never rests.
- Partial fills update remaining quantity and reservations.
- Cancellation releases the unfilled reservation.
- Self-crossing orders are rejected before reservation/matching.

## Shared settlement and persistence

`FinancialPlatform` configures Match to use the same SettlementBook as Flow. A buy order reserves quote notional; a sell order reserves base atoms. A fill debits actual quote notional from the buyer, transfers base/quote assets, consumes reservations, updates the trade tape, and validates that active orders exactly account for shared reservations.

Flow and generic platform transfers see only available balances (`total - reserved`). They cannot spend funds reserved by an open Match order. Cancelling an order releases the reservation to the same shared balance view.

Every Match mutation is persisted with the SettlementBook, order records, trade tape, and sequence counters in one revisioned/checksummed SQLite platform snapshot. On reopen the order book and reservations are rebuilt and cross-validated against persisted balances. A failed operation does not advance the platform revision or publish candidate state.

The integrated Match engine is an off-chain simulation. It does not connect to brokers, real order-routing venues, or external market data and does not provide a production risk, surveillance, or clearing system.

## Benchmark

`feinstatic_bench [order_operations]` runs a deterministic same-price crossing workload and reports throughput plus average, p50, p95, and p99 submission latency. It measures one process and the engine's in-memory matching path. It does not measure FinancialPlatform snapshot serialization/SQLite commit latency, networking, concurrency, UI rendering, or market-data fan-out.
