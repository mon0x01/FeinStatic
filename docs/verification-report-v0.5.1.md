# Verification report — FeinStatic 0.5.1

## Release purpose

v0.5.1 is a compatibility maintenance release for the v0.5.0 TCP networking foundation. It corrects use of the deprecated Boost.Asio `steady_timer::cancel(error_code&)` overload, which is no longer available in newer Boost releases such as the Boost 1.92 toolchain reported by the target Arch installation.

The networking implementation now uses `steady_timer::cancel()` and the core is compiled with `BOOST_ASIO_NO_DEPRECATED`. The latter deliberately removes deprecated Boost.Asio declarations from the compile surface so future API removals are caught at build time. There is no wire-protocol, database-schema, ledger-state, or financial-accounting change in this maintenance release.

## Automated test results

The final source tree ran 67 tests with zero failures in each configuration:

| Configuration | Result |
|---|---:|
| GCC Debug | 67/67 passed |
| GCC Release | 67/67 passed |
| GCC ASan/UBSan | 67/67 passed |
| Clang Release + `-Werror` | 67/67 passed |

The test executable reported `67 passed, 0 failed, 67 total`. The complete `scripts/verify-all.sh` run also completed, including Release demonstration and one-million-order Match benchmark.

## Build environment and compatibility qualification

The verification container used GCC 14.2.0, Clang 17.0.0, Boost 1.83, OpenSSL 3.5.7, and SQLite 3.46.1. To reproduce the newer Boost API surface, the project core was compiled with `BOOST_ASIO_NO_DEPRECATED`, which removes the deprecated timer overload and proves that source no longer depends on it.

The exact target Arch Boost 1.92 installation was not available in the build container. Therefore, this report does not claim a direct build on Boost 1.92; the target-machine confirmation is the first GCC/Clang build performed by the supplied Arch updater.

## Benchmark

The one-million-order in-memory Match benchmark completed at approximately 250,333 submissions/second in this verification container. This is host- and load-dependent and excludes networking, SQLite persistence, and distributed settlement. It must not be compared directly with the user's Arch laptop measurements.

## Scope and limitations

The v0.5.0 TCP functionality is unchanged: bounded/checksummed frames, genesis/parameter handshake, transaction relay, block relay, append-only chain synchronization, and rejection of divergent chains. It remains a networking foundation, not distributed consensus. There is no TLS, authenticated peer identity, peer discovery, fork-choice/reorganization, quorum or finality protocol. The listener is single-threaded. Do not expose it to untrusted networks.

## Database safety

The release archive excludes `.git`, `build`, and `runtime`. The updater backs up the existing checkout, merges source files into the existing directory, and checks that `runtime/feinstatic-fresh.db` has the same SHA-256 before and after source integration and database verification. No schema migration or customer-state mutation is introduced by v0.5.1.
