# FeinStatic benchmark results

## v1.0.0 development-container baseline

- Compiler: GCC 14.2.0.
- Hardware threads reported by the container: 5.
- Order operations: 1,000,000.
- Completed trades: 500,000.
- Elapsed: 4.278749 seconds.
- Throughput: 233,713.15 submissions/second.
- Average latency: 4.24 microseconds.
- p50: 1.84 microseconds; p95: 5.61 microseconds; p99: 15.93 microseconds.

This is the same synthetic in-memory Match workload used in earlier releases. It does not measure the v1.0.0 TCP/Raft path, signatures, SQLite writes, block validation, or Ledger application. It must not be presented as distributed transaction throughput. See [`verification-report.md`](verification-report.md) for the complete v1.0.0 test report.

