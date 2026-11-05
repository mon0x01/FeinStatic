# Development notes

- Keep domain logic independent of the terminal interface.
- Preserve integer atom accounting and checked overflow in every financial path.
- Validate proposed state transitions before mutating canonical state.
- Match and Flow must use one SettlementBook when hosted by FinancialPlatform; do not introduce per-engine balance caches.
- Preserve the distinction between on-chain Ledger balances and off-chain platform balances. Wallet-address linking is identity reuse only. Only the BridgeService may turn a confirmed native-FST deposit into an off-chain credit or initiate a collateral-backed withdrawal.
- Platform mutations must operate on candidate state, validate every engine and account-registration invariant, persist one revisioned SQLite snapshot, and publish memory state only after commit.
- Additive SQLite migrations must preserve the Ledger `blocks` and `wallets` tables; test migrations using a real previous-version database fixture.
- Snapshot deserialization must validate bounds, IDs/counters, enum values, cross-references, reservations, and accounting invariants. Preserve backward readers when appending snapshot fields and test real prior-version payload fixtures.
- Bridge changes must retain idempotent deposit IDs, exact signed withdrawal bytes, replay protection, collateral coverage over account balances plus Flow reserves and pending payouts, and retry/reconciliation behavior across process restart. Never persist vault private keys or passphrases. A pending signed withdrawal is not cancellable; resolve it through retry/mining/reconciliation.
- Update tests and docs with behavior changes. Never report a build, test, benchmark, or integration status without executing it against the current source.
- The full-state snapshot strategy is intentionally simple and failure-atomic for the bounded local MVP. Profile it before claiming it scales; consider event-log/normalized storage before large workloads.
