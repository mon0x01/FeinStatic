# Verification report — FeinStatic 0.3.0

This report describes the v0.3.0 source snapshot tested in the development container. These results are separate from validation on the user's Arch Linux machine; the included update script runs the same matrix on that machine after installing the source update.

## Build and test matrix

The v0.3.0 suite contains **50 test cases** in one CTest executable. The full local verification matrix passed:

| Compiler / configuration | Configure | Compile | Tests |
|---|---:|---:|---:|
| GCC 14.2 / Debug / Ninja | PASS | PASS | 50 passed, 0 failed |
| GCC 14.2 / Release / Ninja | PASS | PASS | 50 passed, 0 failed |
| GCC 14.2 / Debug + ASan/UBSan / Ninja | PASS | PASS | 50 passed, 0 failed; no sanitizer findings |
| Clang 17 / Release / Ninja with `-Werror` | PASS | PASS | 50 passed, 0 failed |

The suite covers POSIX pseudo-terminal secret-input behavior, Ledger/cryptography/storage, Match/Flow algorithms and deterministic stress, FinancialPlatform persistence/recovery, and the v0.3.0 exact Wallet A/Wallet B lifecycle. The new integration test also runs the actual `feinstatic verify <database>` command against its temporary test database.

## v0.3.0 automated lifecycle coverage

The end-to-end test uses a temporary SQLite database with two actual stored Ledger wallet identities and synthetic off-chain platform balances. It verifies:

- Match sell/buy crossing, partial fill, reservation release after cancellation.
- FST-to-USD and USD-to-FST AMM quotes and swaps, including exact fees and output atoms.
- A deliberately impossible minimum output is rejected without changing revision, balances, reserves, or swap history.
- Wallet B adds ratio-matched liquidity under maximum-amount caps, then partially and fully redeems its LP position.
- Final account balances, LP ownership, pool reserves, trade history, swap history, and settlement journal are exact.
- The database is reopened and validated repeatedly; the non-interactive verifier reports the expected recovered state.
- A missing database is reported as a failure and is not silently created.

The full Wallet B withdrawal leaves one atom of FST and one atom of USD in the pool due to floor rounding across two proportional redemptions. The test asserts these exact results. This is retained pool dust, not silently treated as a zero-difference redemption.

## Non-interactive verifier

`build/release/feinstatic verify runtime/feinstatic-fresh.db` opens the specified existing database, validates the recovered Ledger chain and the shared platform invariants, and reports engine record counts. It exits nonzero if either state domain cannot be opened or validated. This is an operational integrity check, not a replacement for unit, fuzz, security, or fault-injection testing.

## Application smoke checks and benchmark

The complete `scripts/verify-all.sh` run includes the Release application demo and the one-million-order Release Match benchmark. The benchmark excludes SQLite persistence and network I/O; see [`benchmark-results.md`](benchmark-results.md) for scope.

## Architectural boundary and limitations

The Ledger and off-chain FinancialPlatform use the same SQLite file but separate accounting domains. Linking a wallet address does not perform a deposit or cross-domain settlement. Platform test credits are synthetic. There is no on-chain AMM, bridge, smart-contract VM, P2P network, or distributed consensus.

The full platform state is serialized to one bounded SQLite snapshot after each mutation. Optimistic revision checking rejects stale writers; it does not merge concurrent sessions or provide a multi-process command service. SQLite SHA-256 snapshot checksums detect accidental corruption, not a malicious editor able to recalculate a checksum.

No formal verification, broad coverage-guided fuzzing campaign, independent security audit, production custody review, network threat model, or power-loss fault-injection proof is claimed. The terminal application remains line-oriented; FTXUI is not yet implemented.
