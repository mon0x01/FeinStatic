# FeinStatic 1.8.0 release notes

## Summary

FeinStatic 1.8.0 removes the single-connection I/O bottleneck while preserving the existing single-writer Ledger/Raft state model. It adds a bounded inbound socket worker pool and connection queue, with explicit serialization of all stateful requests and response writes outside the state lock. The authenticated wire protocol remains v8 and the SQLite schema remains unchanged.

## Bounded inbound worker pool

- A single accept loop continues to apply the v1.7.0 global and per-address token-bucket admission checks.
- Admitted sockets enter a bounded FIFO queue serviced by a fixed worker pool. `InboundAdmissionConfig::worker_count` defaults to 4 and supports 1–16 workers. `maximum_queued_connections` defaults to 64 and supports 1–4096 queued sockets.
- Every worker owns its own Asio `io_context` and processes at most one connection at a time. Handshake reads, challenge exchange, and request frame reads are concurrent across workers.
- Queue saturation is fail-closed: a new accepted socket is closed rather than allowing the pending connection count to grow without bound. Clients should retry using bounded backoff.

## State ownership and slow-client isolation

- A per-node state mutex serializes all reads and mutations involving Ledger, Raft, trust policy, and peer endpoints. The project does not execute Ledger state transitions concurrently.
- Each handler builds the response while it holds the state mutex, then releases the mutex before writing response bytes. A slow/non-reading remote peer therefore does not hold the state lock during its response transfer.
- Socket reads and writes remain timeout-bounded. Shutdown closes queued native sockets and joins active workers; active operations terminate when the peer disconnects or an I/O timeout expires.

## Build-system consistency

- Boost.Asio's `BOOST_ASIO_NO_DEPRECATED` setting is consistent in the core and test translation units that instantiate Asio socket templates. This prevents inconsistent inline/template definitions within the test executable.

## Compatibility and limitations

- Authenticated protocol v8 is unchanged; v1.8.0 nodes can communicate with v1.7.0 nodes using the same wire protocol.
- No SQLite schema or persistent consensus-state format change is introduced. Existing endpoint registries, trust stores, identity vaults, Git history, and runtime databases remain separate from the source archive.
- This is bounded I/O concurrency, not concurrent Ledger execution and not a complete DoS defense. TCP SYN handling, upstream bandwidth limits, per-request CPU budgets, Byzantine fault tolerance, peer discovery, TLS interoperability, and independent cryptographic audit remain out of scope.

## Validation

Validated against the v1.8.0 source tree:

| Configuration | Result |
|---|---:|
| GCC Debug | 156/156 passed |
| GCC Release | 156/156 passed |
| GCC ASan/UBSan | 156/156 passed |
| Clang Release with `-Werror` | 156/156 passed |
| GCC ThreadSanitizer focused concurrency suite | 6/6 selected tests passed, no race reports |
| Three-process Raft chaos laboratory | 9/9 stages passed |
| Four-process joint-consensus membership laboratory | 8/8 stages passed |

The ThreadSanitizer build emits GCC/Boost.Asio's known compile-time warning that `atomic_thread_fence` is not modeled by TSan; the focused runtime tests nevertheless completed without a TSan race report. This focused TSan run is additional evidence, not a claim that all third-party library internals are race-free.

The Release demonstration passed. The synthetic one-million-order in-memory matching benchmark completed at 421,889.75 order operations/second, with 4.73 μs p99 latency in this development container. This benchmark excludes persistence and network work.
