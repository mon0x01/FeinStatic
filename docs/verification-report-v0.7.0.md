# Verification report — FeinStatic 0.7.0

## Scope

v0.7.0 adds independent Ed25519 node identities, bounded canonical peer trust stores, a nonce-based mutual challenge-response bound to the chain parameters, and Ed25519 signatures on each authenticated application frame. Frame signatures cover a digest derived from the specific mutual-handshake transcript (both fresh nonces, both node IDs, and chain parameters), message type, payload, and strict per-direction sequence numbers. This prevents replaying a captured signed frame in a later session between the same two node identities. The legacy unauthenticated listener is restricted to loopback.

No SQLite schema migration is required in v0.7.0. Existing Ledger, durable mempool, and FinancialPlatform persistence formats remain unchanged.

## Test coverage

The final source tree contains **83 automated test cases**. The suite covers existing Ledger, Match, Flow, Bridge, shared settlement, durable mempool, TCP relay/sync and failure-atomicity behavior, plus new tests for:

- Node identity generation, secure file creation (mode 0600), owner/permission checks, and identity reload.
- Canonical node IDs derived from public keys and trust-store round trips.
- Bounded trust-store size and strict ID/public-key consistency.
- Successful mutual challenge-response between independently generated node identities.
- Rejection of an untrusted client before it can mutate the receiver's mempool or chain.
- Client rejection of a server that does not match the pinned expected peer ID.
- Rejection of plaintext clients by authenticated listeners.
- Restriction of the legacy unauthenticated listener to loopback.
- Signed frame sequence handling across authenticated status, transaction relay, error response, and multi-block synchronization sessions.
- Per-connection handshake transcript binding, with a direct cryptographic regression proving that a frame signature from a prior nonce-pair fails under a new session binding.
- CLI identity/trust provisioning smoke test: two independent node identities, mode-0600 private key files, bilateral trust files, and mismatched node-ID/public-key rejection.

## Build matrix

| Configuration | Result |
|---|---:|
| GCC Debug | 83/83 passed |
| GCC Release | 83/83 passed |
| GCC ASan/UBSan | 83/83 passed |
| Clang Release + `-Werror` | 83/83 passed |
| Release demonstration | Passed |
| One-million-order Match benchmark | Completed |

All four `scripts/verify-all.sh` build/test presets passed on the final working tree. The Release demonstration passed. The benchmark command was run separately and completed successfully.

## Benchmark

Development-container benchmark: GCC 14.2.0, 5 reported hardware threads, 1,000,000 synthetic same-price Match order operations, 500,000 completed trades, 4.562475 seconds elapsed, 219,179.29 submissions/second, 4.53 µs average, 1.87 µs p50, 11.36 µs p95, 22.30 µs p99. This is an in-memory Match benchmark and excludes TCP, SQLite, Bridge and distributed synchronization. It is not a measurement of the user's Arch machine.

## Security and correctness limits

- Peer authentication and per-frame integrity are implemented, including connection-specific frame signature binding against cross-session replay, but **traffic confidentiality is not**. There is no TLS. Use loopback or a trusted isolated network only; never expose this prototype to the public Internet or an untrusted LAN.
- The node identity file contains a raw Ed25519 private seed protected by mode 0600 and filesystem ownership checks, not application-level encryption. Full-disk encryption and secure backups are expected operational protections.
- Trust distribution is manual. There is no certificate authority, identity rotation protocol, peer discovery, rate limiting, reputation system or robust DoS mitigation.
- The node server remains single-threaded and processes one connection at a time.
- Chain synchronization remains append-only. Divergent chains are rejected; there is no fork choice, reorganization, quorum, leader election, distributed consensus or finality.
- Passing tests do not constitute a formal cryptographic audit, production key-custody review, adversarial Internet test, arbitrary power-loss proof, or production readiness sign-off.

## Environment

The development verification environment used GCC 14.2.0, Clang 17, Boost 1.83, OpenSSL 3.5.7, SQLite 3.46.1, CMake 3.31.1 and Ninja 1.12.1. `BOOST_ASIO_NO_DEPRECATED` remains enabled for the networking core. The user's Arch installation uses a newer Boost toolchain; the supplied updater runs the exact compiler/sanitizer matrix on that machine. The existing v0.6.0 SQLite database does not require a schema migration for v0.7.0.
