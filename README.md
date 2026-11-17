# FST

**FST** is an independent, globally decentralised financial network. FST is the
network's native currency, operating token, and settlement asset. It is being
developed from the existing FeinStatic codebase (release v2.1.0) and redesigned
as a permissionless, Byzantine fault-tolerant financial network.

FST is **not** a bank plugin, a banking application, a system controlled by an
existing financial institution, or a centrally controlled financial application.

## Current architecture

FST separates consensus from the deterministic financial application. CometBFT
runs as a separate consensus process per validator node. The C++ FST application
connects to CometBFT over a local Unix-domain socket using the ABCI v0.40.0
protocol and executes transactions deterministically.

```text
                                    CometBFT consensus process
                           (stake-weighted BFT, deterministic finality)
                                       |
                                       | ABCI v0.40.0 over Unix-domain socket
                                       v
                    +-----------------------------------------------+
                    |        FST ABCI application (C++, deterministic)|
                    |                                               |
                    |  InitChain  CheckTx  FinalizeBlock  Commit    |
                    |                                               |
                    +----------+----------+----------+--------------+
                               |          |          |
                    +----------v----------v----------v----------+
                    |   UTXO ledger   |   Staking state   | Money|
                    |   (src/utxo/)   |   (src/staking/)  | eng. |
                    |   + mempool     |   validator set   |(src/ |
                    |                 |   voting power    |monet |
                    |                 |   slashing        |ary/) |
                    +-----------------+-------------------+------+
                                       |
                    +------------------v------------------+
                    |   P2P networking foundation         |
                    |   TLS 1.3 authenticated sessions   |
                    |   Node identity, peer store,       |
                    |   bootstrap, message dispatch,     |
                    |   dynamic peer admission           |
                    +------------------------------------+
```

### Architecture principles

- Consensus and application state are decoupled: CometBFT orders transactions;
  the FST application validates and executes them.
- Voting power is derived from deterministic FST application state (bonded
  stake), not from CometBFT internals.
- All state transitions are deterministic: no wall clock, no unordered
  iteration, no floating point, and no filesystem input in consensus-critical
  paths.
- The ABCI socket is a local interface, protected by filesystem permissions;
  it is not exposed to the public network.

## Project status

| Area | Status | Evidence |
|------|--------|----------|
| Project identity, principles, decisions | Implemented & documented | `docs/fst/` |
| P2P networking foundation | Implemented & tested | 29/29 multi-node harness tests pass (debug + ASan/UBSan) |
| UTXO ledger & transaction layer | Implemented & tested | 20/20 UTXO tests pass |
| Staking / permissionless PoS | Implemented & tested | 18/18 staking tests pass |
| Monetary issuance & fees | Implemented & tested | 17/17 monetary tests pass |
| Deterministic ABCI application | Implemented & tested | 14/14 ABCI tests pass |
| CometBFT socket server + codec | Implemented & tested | Length-delimited protobuf framing over Unix-domain socket |
| Real multi-validator CometBFT integration | Integrated (prototype) | 3 tests pass against actual CometBFT v0.40.0: socket connection + Echo, single-node genesis + commit, 4-node genesis agreement |
| Byzantine fault / partition / recovery testing | Planned | Not yet implemented |
| Transaction flow across nodes (UTXO conservation) | Planned | Not yet implemented |
| Financial functionality (accounts, payments, settlement) | Deferred | Phase 5 — after core network |
| Privacy & offline operation | Deferred | Phase 6 |
| Multi-transport (mesh, radio, satellite, DTN) | Deferred | Phase 7 |
| Governance, upgrades & fork safety | Deferred | Phase 8 |
| Independent implementations & conformance | Deferred | Phase 9 |
| Production hardening & mainnet readiness | Deferred | Phase 10 |

**Important disclosures:**

- Real multi-validator integration is **not** complete. Current evidence covers
  genesis agreement across 4 CometBFT validator nodes with 4 FST ABCI
  applications. Byzantine fault testing, partition/recovery testing, and
  cross-node transaction flow with UTXO conservation verification remain
  outstanding.
- Independent security validation has **not** been performed. No external
  audit, formal verification, or fuzzing campaign has been completed.
- FST is **not** mainnet-ready. Mainnet launch criteria (security review
  complete, stable test network, independent implementations verified,
  governance operational) are deferred to Phase 10 and must not be claimed
  complete until their acceptance criteria are satisfied.

## Implemented capabilities

### Deterministic financial application (ABCI)

The FST ABCI application implements the ABCI v0.40.0 lifecycle
(`InitChain`, `CheckTx`, `FinalizeBlock`, `Commit`, `PrepareProposal`,
`ProcessProposal`, `ExtendVote`, `VerifyVoteExtension`, state-sync methods,
`Echo`). It combines:

- **UTXO ledger** — canonical transaction encoding, signature verification,
  double-spend prevention, state-root reproducibility, bounded mempool.
- **Staking** — validator registration, bonding/unbonding, voting-power
  derivation from bonded stake, evidence-based slashing, delegation, key
  rotation.
- **Monetary engine** — deterministic declining issuance approaching a tail
  emission, congestion-adjusted base fee (burned), bounded priority fee (to
  block-producing validator), supply conservation with overflow/underflow
  checks.

All monetary and staking parameters (issuance schedule, tail rate, fee
constants, minimum stake, unbonding period, validator count) are
**testnet-labelled research values**, not production policy.

### P2P networking foundation

- Node identity (Ed25519, domain-separated signing), signed node records with
  validation rules.
- Persistent peer store (SQLite WAL), bootstrap discovery, peer exchange.
- TLS 1.3 authenticated sessions (mutual auth, Ed25519 binding over cert hash,
  replay protection, forward secrecy).
- Length-type-checksum-payload message framing with per-type and global size
  caps.
- Dynamic peer admission with connection limits and capped exponential
  backoff.
- Network runtime orchestrating identity, TLS, peer store, bootstrap, admission,
  and dispatch.

## Requirements

- GCC 12+ or Clang 15+ (C++20)
- CMake 3.25+ and Ninja
- OpenSSL 3.x development headers/libraries
- SQLite 3.24+ development headers/libraries
- Boost 1.74+ headers (`Boost.Multiprecision` is header-only)
- Protobuf
- CometBFT (separate process; required only for CometBFT integration tests,
  not for building or running the FST application)

On Arch Linux:

```bash
sudo pacman -S --needed base-devel cmake ninja git openssl sqlite boost protobuf
```

## Build and test

Configure and build (Debug):

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug --output-on-failure
```

Available presets: `debug`, `release`, `asan` (ASan/UBSan),
`clang-release` (Clang + `-Werror`), `tsan` (ThreadSanitizer).

Run the full test suite:

```bash
./build/debug/feinstatic_tests
```

Run a specific test group:

```bash
./build/debug/feinstatic_tests utxo
./build/debug/feinstatic_tests staking
./build/debug/feinstatic_tests monetary
./build/debug/feinstatic_tests abci
./build/debug/feinstatic_tests cometbft
./build/debug/feinstatic_tests harness_
```

Run the complete verification matrix (Debug, Release, ASan/UBSan, Clang
Release + `-Werror`, plus demonstration and benchmark):

```bash
./scripts/verify-all.sh
```

**Test results (verified):** 343/343 tests pass under GCC Debug, GCC Release,
GCC ASan/UBSan, Clang Release with `-Werror`, and ThreadSanitizer.

## Documentation

- [`docs/fst/scope/scope-v0.1.md`](docs/fst/scope/scope-v0.1.md) — Project
  scope and approved boundaries
- [`docs/fst/principles/engineering-principles.md`](docs/fst/principles/engineering-principles.md) —
  Engineering principles P1–P14
- [`docs/fst/decisions/decision-register.md`](docs/fst/decisions/decision-register.md) —
  Decision classifications (APPROVED / PROVISIONAL / OPEN RESEARCH)
- [`docs/fst/architecture/feinstat-baseline.md`](docs/fst/architecture/feinstat-baseline.md) —
  FeinStatic baseline (historical reference only)
- [`docs/fst/adr/adr-0002-utxo-ledger-transaction-architecture.md`](docs/fst/adr/adr-0002-utxo-ledger-transaction-architecture.md) —
  UTXO transaction architecture
- [`docs/fst/adr/adr-0003-permissionless-pos-validator-eligibility.md`](docs/fst/adr/adr-0003-permissionless-pos-validator-eligibility.md) —
  Permissionless PoS validator eligibility
- [`docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md`](docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md) —
  BFT consensus and CometBFT integration
- [`docs/fst/adr/adr-0005-monetary-issuance-fee-architecture.md`](docs/fst/adr/adr-0005-monetary-issuance-fee-architecture.md) —
  Monetary issuance and fee architecture

## Limitations and security boundary

- Consensus is stake-weighted BFT via CometBFT with deterministic finality
  under partial synchrony (safety holds while fewer than 1/3 of voting power
  is Byzantine). This is a prototype integration; real multi-validator
  integration, Byzantine fault testing, and partition/recovery testing remain
  outstanding.
- No external security audit or independent security validation has been
  performed.
- FST is not mainnet-ready. There is no production custody, payment rail, or
  real customer funds.
- All monetary and staking parameters are testnet-labelled research values and
  must not be treated as production policy.
- The P2P networking layer uses TLS 1.3 authenticated sessions; the ABCI socket
  is a local interface and must not be exposed publicly.
- Existing FeinStatic source (Raft consensus, Match/Flow engines,
  BridgeService, line-oriented CLI) is preserved as historical reference only
  and is not part of the current FST architecture.

## License

[MIT](LICENSE)
