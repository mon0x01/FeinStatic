# Match benchmark results — FeinStatic 0.3.0

## Current development-workspace run

One Release run was executed against the current source using the deterministic same-price crossing workload:

| Field | Result |
|---|---:|
| Compiler | GCC 14.2.0 |
| Runtime hardware threads reported | 5 |
| Order submissions | 1,000,000 |
| Completed trades | 500,000 |
| Elapsed benchmark time | 2.009148 s |
| Throughput | 497,724 submissions/s |
| Average submission latency | 1.98 µs |
| p50 submission latency | 1.84 µs |
| p95 submission latency | 3.21 µs |
| p99 submission latency | 4.27 µs |
| Peak process RSS (`/usr/bin/time -v`) | 860,280 KiB |

These figures were measured in the Debian 13 development container. They are **not** the user's Arch/i5 benchmark. The user's Arch machine must be benchmarked after local integration before these figures can be used for a resume claim.

## v0.3.0 verification-matrix run

A later one-million-order Release run was executed as part of the v0.3.0 verification script:

| Field | Result |
|---|---:|
| Compiler | GCC 14.2.0 |
| Runtime hardware threads reported | 5 |
| Order submissions | 1,000,000 |
| Completed trades | 500,000 |
| Elapsed benchmark time | 2.482266 s |
| Throughput | 402,857.65 submissions/s |
| Average submission latency | 2.45 µs |
| p50 submission latency | 1.84 µs |
| p95 submission latency | 3.23 µs |
| p99 submission latency | 4.35 µs |

Peak RSS was not captured in this v0.3.0 invocation. This run and the earlier run above are separate container measurements; their throughput difference should not be interpreted as a code regression without controlling for host load and runtime conditions. Neither result represents the user's Arch/i5 machine.

## What is measured

The benchmark runs one process, an in-memory matching engine, the shared settlement book, and its in-memory balance journal. It exercises alternating resting/matching orders at one price level and records per-submission latency. It excludes SQLite snapshot commits, file-system durability, network I/O, concurrency, market-data fan-out, and UI rendering.

## Memory and persistence trade-offs

The benchmark's journal and order/trade history grow with the operation count. The one-million-operation run used about 840 MiB peak RSS. This is a material memory cost, not a detail to hide. The current platform persistence model also serializes a bounded full-state snapshot into SQLite on each mutating operation; that cost is intentionally outside this engine microbenchmark and grows with stored platform history/state. For larger workloads, the next scaling design should consider normalized tables or an append-only durable event log plus periodic snapshots, and a compact journal representation.

## Reproduction

```bash
cmake --preset release
cmake --build --preset release --parallel 2
/usr/bin/time -v ./build/release/feinstatic_bench 1000000
```

Record compiler, build type, CPU, OS, workload size, elapsed time, throughput, latency percentiles, and peak memory. Do not compare runs without matching workloads and build settings. These measurements are not institutional/exchange-grade performance claims.
