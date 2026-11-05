# Testing

The test executable exercises the compiled `feinstatic_core` library, POSIX terminal-secret reader through pseudo-terminals, FinancialPlatform persistence/migration, and actual loopback TCP connections between independent Ledger databases.

## Build and run

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
```

Release and sanitizer configurations:

```bash
cmake --preset release
cmake --build --preset release --parallel 2
ctest --preset release --output-on-failure

cmake --preset asan
cmake --build --preset asan --parallel 2
ctest --preset asan --output-on-failure
```

Clang Release with warnings treated as errors:

```bash
cmake --preset clang-release
cmake --build --preset clang-release --parallel 2
ctest --preset clang-release --output-on-failure
```

For the complete local matrix, including Debug, Release, ASan/UBSan, Clang `-Werror`, demo, benchmark, and both multi-process consensus laboratories:

```bash
./scripts/verify-all.sh
```

For race-focused validation of the v1.8.0 inbound worker pool, use a separate ThreadSanitizer build (do not combine it with ASan/UBSan):

```bash
./scripts/verify-concurrency-tsan.sh
```

For focused diagnosis, pass a substring or set `FEINSTATIC_TEST_FILTER`; only matching test cases run. The full suite remains the default.

```bash
FEINSTATIC_TEST_FILTER=endpoint_registry ./build/release/feinstatic_tests
```

## Current coverage (156 test cases)

- **Crypto:** Ed25519 signing/verification, X25519 ephemeral agreement, directional HKDF-SHA-256 session-key derivation, AES-256-GCM peer-frame encryption/AAD and tag rejection, AES-256-GCM wallet-secret encryption, hex encoding, KDF bounds, passphrase-size bounds, and new-wallet passphrase minimum.
- **Ledger:** signed transfers, mining/reward/fee accounting, invalid-signature rejection, sequential nonce handling, replay rejection, self-transfer behavior, block tamper detection, insufficient funds, and amount overflow.
- **Storage:** durable mempool restart/order, SQLite restart/replay, wallet unlock/wrong-passphrase rejection, file permissions, corrupted block payload and hash-index rejection, future schema rejection, missing-path behavior, corrupted wallet identity rejection, and snapshot checksum verification.
- **Terminal:** hidden input/no echo, echo restoration after normal completion, SIGINT and SIGTERM, and fail-closed non-TTY input.
- **Match:** price-time priority, maker-price execution, partial/multi-level fills, market IOC behavior, cancellation, self-trade prevention, insufficient funds, overflow, atomic multi-level failure, deterministic 600-order stress, snapshot sequence/status validation, and account/reservation/trade/book consistency.
- **Flow:** constant-product swaps, both directions, LP contribution/redemption, fees and price impact, minimum-output rejection, output-balance overflow, extreme valid 64-bit arithmetic, deterministic 400-swap invariant stress, malformed snapshot, pool/swap ID-counter, and asset-pair rejection.
- **FinancialPlatform:** shared settlement interaction between Match and Flow, reserved-fund isolation, internal transfers, persistence across multiple restarts, order cancellation/reservation release, LP redemption, optimistic stale-writer rejection, a real additive schema-v1-to-v3 migration preserving a Ledger block row, snapshot checksum corruption rejection, batch settlement rollback, and cross-engine account/invariant validation. The v0.3.0 end-to-end test reproduces the Wallet A/Wallet B scenario in exact integer atoms, including two swaps, slippage rollback, proportional liquidity addition, partial/full redemption, rounding dust, and repeated database recovery.

- **Network and peer security (v0.5.0–v1.9.0):** compatible-genesis handshake and status, transaction relay and duplicate rejection, receiver-restart recovery, block relay and atomic mempool reconciliation, multi-batch synchronization, bounded common-ancestor locators, divergent-suffix download, longer-branch adoption, equal-height fork convergence, shorter-peer prefix handling, Bridge external-reference protection, rollback under injected SQLite failure, wrong-genesis/invalid-block rejection, listener survival after rejected handshakes, timeouts, node identity permissions, canonical trust stores, mutual challenge-response, server identity pinning, untrusted/plaintext-client rejection, encrypted-frame sequence/AAD verification, cross-session replay resistance, authenticated transaction/block relay, operator-only membership authorization, role persistence, legacy trust-file loading, revocation and identity rotation. v1.5.0 adds passphrase-encrypted identity vault permissions/round-trip/wrong-passphrase/tamper tests, signed policy cluster binding and signature validation, monotonic epoch replay rejection, tombstone preservation, minimum active-operator enforcement, real TCP policy push, durable trust-store persistence across receiver restart, pre-provisioned member protection, and post-revocation handshake rejection. v1.6.0 adds endpoint-registry permission/checksum/cluster/roster validation, corruption fail-closed restart, endpoint persistence across restart, operator-only health/update access, active-voter disable rejection, and server lifecycle regression coverage for stop-before-run and concurrent run entry. v1.7.0 adds per-address token-bucket admission/refill tests and a three-node authenticated TCP transaction-gossip test that queues 130 transactions while receivers are offline, restarts the origin, then verifies durable-mempool rescan and convergence after receiver recovery. Another test verifies a failed gossip send remains queued and is delivered after the destination reconnects without restarting the origin. v1.8.0 adds inbound worker-pool tests: a slow handshake occupies only one worker while another serves a status request, twelve simultaneous status clients all complete, and queue saturation rejects excess sockets and recovers when capacity becomes available. v1.9.0 adds deterministic peer lifecycle state-machine tests for degraded/offline/half-open/healthy/disabled transitions, peer-specific jitter bounds, circuit reset, and an authenticated TCP assertion that both traffic classes respect one retry gate and recovery is reflected in operator health.
- **Raft consensus, Ledger adapter, snapshots, and joint membership (v0.9.0–v2.0.0):** initial odd-voter membership validation and joint-consensus transition invariants; randomized election timing; durable one-vote-per-term; up-to-date candidate rule; current-term leader no-op; majority replication and minority non-commit; conflict repair; committed-entry protection; ordered application index; checkpoint/member-set binding; fail-closed storage errors; SQLite revision/checksum validation; malformed-batch atomicity; aggregate message limits; timer-overflow protection; canonical bounded block-command encoding; follower block validation; atomic Ledger append + durable mempool reconciliation + Raft applied-index commit; startup Ledger/log consistency checks; rejection of direct-import bypasses; applied-prefix compaction; immutable snapshot objects; 512 KiB chunk transfer and 64 MiB limit; durable receiver restart between chunks, sender restart/offset renegotiation, recovery after the final durable chunk but before application installation, and legacy v1 encoding reads; full Ledger snapshot restore; restart after installation; and SQLite failure injection proving snapshot install rollback. The v1.1 multi-process lab covers leader death, minority isolation, majority progress, healing, catch-up and convergence. The v1.3 four-process lab provisions three voters and a learner, catches that learner up from a compacted snapshot, removes the current leader while promoting the learner, restarts the removed node as a learner, then promotes it in a reverse transition. The tests also gate promotion on learner replication progress and cover durable joint/final configuration recovery.
- **Crash-resumable snapshot staging (v2.0.0):** real SQLite schema-v5-to-v6 migration; per-chunk durable commit before acknowledgement; contiguous-offset and exact metadata binding; partial-prefix reload after receiver restart; sender restart from offset zero with receiver-provided expected offset; full-payload checksum validation; startup recovery when the final chunk was durable but application installation had not completed; staging cleanup after successful installation; and preservation of complete staging after injected install failure.
- **Durable Ledger mempool (v0.6.0):** sequential nonce restoration after restart, duplicate rejection after restart, transaction-ID/payload tampering detection, additive v2 -> v3 schema migration, submission rollback when SQLite insertion fails, atomic rollback when mempool deletion fails during local mining, and atomic block/mempool rollback when peer-import reconciliation fails.
- **Bridge (v0.4.0):** vault configuration/immutability, rejection of a customer vault, unconfirmed and wrong-recipient deposit rejection, registered sender requirement, confirmed deposit idempotency, FST pool-reserve collateral accounting, exact signed withdrawal persistence, prevention of multiple pending withdrawals, retry with a durable mempool, chain-confirmed-but-unreconciled crash recovery, collateral encumbrance while pending, detection of unrelated vault mempool outflows, chain-backed history verification with recomputed-checksum tampering, reconciliation after restart, and synthetic-credit undercollateralization rejection.

The v1.7.0 153-test suite, the v1.8.0 156-test suite, the v1.9.0 161-test suite, and the v2.0.0 163-test suite plus multi-process labs exercise the implemented crash-fault Raft protocol, snapshot recovery, joint-consensus voter reconfiguration, encrypted identity vaults, explicit signed trust-policy push, endpoint-registry persistence/corruption handling, operator-only endpoint management, the outbound dispatcher under tested loopback scenarios, and durable snapshot receiver restart/offset recovery. They do not prove safety under arbitrary adversarial schedules, Byzantine members, arbitrary filesystem/controller power-loss behavior, production key custody or hardware-backed vault protection, an external cryptographic audit, Internet-scale inbound DoS resistance, external exchange safety, or a full smart-contract security model. The v1.6.1 four-process membership lab waits for the current leader to acknowledge learner replication before retrying the promotion request; retries are limited to the explicit pre-append catch-up rejection and never bypass the consensus guard.


## Non-interactive database verification

After closing the interactive application, validate an existing database without entering the menus:

```bash
./build/release/feinstatic verify runtime/feinstatic-fresh.db
```

The command is a non-interactive integrity diagnostic: it validates the recovered Ledger chain and the shared Settlement/Match/Flow state without changing customer balances, submitting transactions, mining blocks, or reconciling Bridge records. It opens the database through the normal SQLite store path, however, so it is not a forensic read-only handle. When Bridge is configured, it also inspects the vault balance, aggregate off-chain FST liabilities, unconfirmed pending withdrawal reserve, required collateral, shortfall, and any withdrawal already mined but awaiting snapshot reconciliation. It prints a PASS/FAIL result and engine record counts, and exits nonzero on a failed or unopenable component. It does not create accounts or synthetic balances. The automated platform tests execute this same command against a temporary database fixture.


## TCP peer integration test notes

Non-Raft network integration tests use independent temporary SQLite databases and real loopback TCP sockets, not mocked frame exchange. The v1.0.0 Raft integration tests launch three independently configured node servers, each with its own SQLite database and Ed25519 identity. They exercise authenticated RequestVote/AppendEntries RPC delivery, proposal and majority commit, block application convergence, direct-import bypass rejection, member restart, leader process termination, replacement election, and catch-up. Proof-of-Work difficulty zero is used in fixtures so the tests remain deterministic without spending time mining.


## Fork-choice and reorganization tests (v0.8.0)

The v0.8.0 regression suite uses independent SQLite databases with identical genesis parameters and real loopback TCP sessions. It covers locator-based suffix download, prefix/no-op synchronization, equal-height deterministic tie convergence, greater-height branch adoption, shorter divergent peer rejection, full-chain replay validation, mempool preservation/requeue/drop behavior, restart after reorganization, and failure injection proving chain replacement plus mempool replacement roll back together. Bridge tests verify that the operational sync path obtains durable Bridge transaction references; `Ledger::consider_chain` rejects candidates that orphan a protected reference or move a height-pinned reference. Pending Bridge withdrawals must remain in the durable mempool or become confirmed.
