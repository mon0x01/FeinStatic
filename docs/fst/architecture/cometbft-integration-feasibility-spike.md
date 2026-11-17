# CometBFT Integration Feasibility Spike

## Metadata

| Field | Value |
|-------|-------|
| ID | FST-SPIKE-COMETBFT-001 |
| Date | 2026-11-17 |
| Status | COMPLETE (findings recorded; prototype build blocked by missing Go toolchain — see Environment Prerequisite) |
| Related ADR | ADR-0004 (BFT consensus, finality and CometBFT integration) |
| Related Decision | D-017 (APPROVED — stake-weighted BFT consensus with deterministic finality; CometBFT preferred candidate) |

## Objective

Verify, against the actual CometBFT release, whether CometBFT is a viable production consensus dependency for FST:
- current stable release, maintenance status, security advisories and patch status (incl. CSA-2026-001/Tachyon),
- dependency and licensing obligations, Go runtime requirements,
- ABCI/ABCI++ compatibility, validator-set update / activation-height semantics,
- state synchronization, snapshots, light-client verification,
- upgrade and migration process, performance and operational overhead.

Per ADR-0004, CometBFT is an APPROVED candidate but not a committed production dependency until this spike is complete. If material incompatibilities are found, document evidence and a proposed alternative rather than silently switching consensus families.

## Findings

### 1. Current stable release

| Attribute | Value | Evidence |
|-----------|-------|----------|
| Release | v0.40.0 | github.com/cometbft/cometbft/releases/latest |
| Release date | 2026-07-27T21:19:49Z | GitHub release page |
| Commit | 0880b4d (GPG-signed, committer signature verified) | Release page |
| Tag signature | Verified (GPG key 3BFB0FD2F8A4E6BC) | Release page |
| Commits ahead on `main` | 43 (unreleased) | Release page comparison |
| go.mod `go` directive | 1.25.0 | raw go.mod at v0.40.0 |
| License | Apache-2.0 (redistributable, minimal restrictions) | pkg.go.dev |
| Maintainer | Cosmos Labs (wholly-owned subsidiary of Interchain Foundation, Swiss nonprofit) | pkg.go.dev README |
| Protocol | Fork of Tendermint Core; implements Tendermint BFT consensus (BFT SMR) | pkg.go.dev README |
| ABCI version | ABCI 2.0 / ABCI++ (since v0.38.0) | v0.38.0 changelog + ABCI spec |
| Distribution | Source, no pre-built binary required; `cometbft` CLI, docker images `cometbft/cometbft` | Release page, README |

**Release policy:** CometBFT explicitly states "Please do not depend on main as your production branch. Use releases instead." PATCH bumps are compatible with existing blockchain histories; MINOR bumps may be protocol-breaking and may require a new chain (per UPGRADING.md). v0.40.0 is the latest stable; v0.38.26 is the latest v0.38.x LTS patch.

### 2. Security advisories and patch status

| Advisory | ID | Severity | Published | Affected | Patched in | Fixed in v0.40.0? |
|----------|----|----------|-----------|----------|------------|-------------------|
| CSA-2026-001: Tachyon | GHSA-c32p-wcqj-j677 | Critical | 2026-01-23 | <= v0.38.20, <= v0.37.17 | v0.38.21, v0.37.18 | Yes (see below) |
| ASA-2025-003 | GHSA-hrhf-2vcr-ghch | High | 2025-10-14 | <= v0.38.18, <= v0.37.15 | v0.38.19 | Yes |
| ASA-2025-002 | GHSA-r3r4-g7hq-pq4f | High | 2025-02-03 | <= v0.38.16, v1.0.0 | v0.38.17 | Yes |
| ASA-2025-001 | GHSA-22qq-3xwm-r5x4 | Low→Informational | 2025-02-03 | <= v0.38.16, v1.0.0 | v0.38.17 | Yes |
| ASA-2024-011 | GHSA-p7mv-53f2-4cwj | High | 2024-11-06 | vote extensions | v0.38.15 | Yes |
| ASA-2024-009 | GHSA-g5xx-c4hv-9ccc | Moderate | 2024-09-03 | light client | v0.38.12 | Yes |
| ASA-2024-008 | GHSA-hg58-rf2h-6rr7 | Moderate→Informational | 2024-06-27 | blocksync | v0.38.8 | Yes |
| ASA-2024-001 | GHSA-qr8r-m495-7hc4 | High | 2024-01-17 | chain halt | v0.38.3 | Yes |

**CSA-2026-001/Tachyon verification:** The advisory states the fix is in v0.38.21 / v0.37.18 and that "the main unreleased branch is also patched" (patch completed 2026-01-12, published 2026-01-23). v0.40.0 was released 2026-07-27 from the patched main branch and post-dates the fix by ~6.5 months. All other listed advisories predate v0.40.0 and their fixes are ancestor releases of the v0.39/v0.40 lines. **Conclusion: v0.40.0 includes the CSA-2026-001 fix and all other listed security patches.** (Inference basis: release timeline + advisory's main-branch-patch statement; not re-verified by direct source inspection of the v0.40.0 tree.)

### 3. Go runtime and deployment requirements

- v0.40.0 requires **Go 1.25.0** (go.mod directive).
- v0.38.x requires Go 1.22+ (per minimum-requirements table).
- Building CometBFT requires a Go toolchain; FST itself is C++20 and does not link CometBFT.

### 4. Licensing and dependency obligations

- Apache-2.0. Redistributable, minimal restrictions, no copyleft.
- FST runs CometBFT as a **separate process**, not a linked library, so there is no Go/C++ linking obligation.
- CometBFT dependencies (Go modules) are CometBFT's concern, not FST's.
- Fork right: Apache-2.0 permits independent forking if Cosmos Labs ceases maintenance (supports P10).

### 5. ABCI / ABCI++ compatibility

- ABCI messages are protocol buffers: `proto/tendermint/abci/types.proto`.
- Language-agnostic: any language can implement the ABCI application over the wire protocol.
- Transport: TCP socket, Unix-domain socket, or gRPC. **Local Unix-domain socket is supported** and is the preferred FST deployment (app interface not exposed publicly).
- `abci-cli` tool available for testing.
- ABCI++ (ABCI 2.0) methods relevant to FST:
  - `InitChain` — genesis initialization, initial validator set and application state.
  - `PrepareProposal` / `ProcessProposal` — proposal construction/validation hooks.
  - `FinalizeBlock` — apply transactions, compute `app_hash`, return `ValidatorUpdates`.
  - `Commit` — persist state, return application hash (state commitment).
  - `ExtendVote` / `VerifyVoteExtension` — vote extensions (optional; FST may disable).
  - Snapshot methods for state sync: `ListSnapshots`, `OfferSnapshot`, `LoadSnapshotChunk`, `ApplySnapshotChunk`.
  - `ListInfo` — application info.
- CometBFT initiates all calls; the application responds. CometBFT drives the lifecycle.

### 6. Validator-set updates and activation-height semantics

- Validator updates are returned by the application in `ResponseFinalizeBlock.ValidatorUpdates`.
- CometBFT applies them per documented **activation-height semantics**: a validator-set change proposed at height H becomes part of the next validator set and takes effect as the active set at height H+2 (standard Tendermint semantics — change signed into the commit at H+1, active at H+2). FST's staking module (ADR-0003) must emit validator updates consistent with this, and the deterministic application state must be the sole source of voting power.

### 7. State synchronization, snapshots, light client

- State sync supported via ABCI snapshot methods (list/offer/load/apply chunks) plus block sync.
- `light` package provides a light-client implementation with provider (HTTP/mock).
- Blockstore and state store for persistence.
- `inspect` tool for investigating failed-node state.

### 8. Upgrade and migration process

- UPGRADING.md documents upgrades. PATCH versions compatible; MINOR versions may be protocol-breaking (may require new chain or custom migration).
- No automatic mainnet migration tooling provided by CometBFT; FST owns its genesis/upgrade procedure.

### 9. Performance and operational overhead

- Marketing claim: up to 10k TPS — treat as indicative, not a guarantee; real BFT throughput depends on topology, hardware, transaction size, signature scheme.
- Operational overhead: separate Go process per validator, Go 1.25 toolchain, CometBFT config management, key management (privval for external signing or embedded key), metrics/RPC exposure, monitoring.
- CometBFT P2P handles consensus messaging between validator nodes; FST's Phase 2 P2P networking (TLS sessions, peer store, gossip) remains available for non-consensus networking (clients, transaction submission, light peers) but is not required for the consensus prototype.
- Default p2p transport is the legacy transport; v0.40.0's libp2p is experimental and should NOT be enabled for consensus networking.

### 10. P1/P2 organizational-control consideration

- CometBFT is maintained by a single organization (Cosmos Labs / Interchain Foundation). Using it as a dependency does not by itself give that organization unilateral control over FST (FST owns genesis, staking, transactions, fees, upgrades; CometBFT only orders transactions). Apache-2.0 + separate-process deployment means FST can fork CometBFT or replace the consensus engine if needed (ADR-0004 fallback: HotStuff/Casper/Narwhal). Documented as a managed dependency risk, not a P1/P2 violation.

## Environment prerequisite (not an architectural blocker)

- **Go is not installed in this environment** (`go: command not found`). CometBFT v0.40.0 requires Go 1.25.0. The **minimal working prototype (one CometBFT process + one C++ FST ABCI application over a Unix-domain socket) cannot be built or executed here until Go 1.25.0 is available.**
- GCC 16.2.1, Clang 23.1.5, CMake 4.5.0, OpenSSL 3.x, SQLite3, Boost are all present — the C++ FST ABCI application and UTXO/staking/monetary modules can be implemented and built without Go.
- Next step when Go is available: initialize CometBFT v0.40.0 genesis, run one CometBFT process, connect the C++ FST ABCI app over a local Unix-domain socket, exercise InitChain → transaction submission → FinalizeBlock → Commit → app_hash, then persistence/restart recovery, then expand to >=4 validator nodes.

## Selection and risks

- **Candidate selected:** CometBFT v0.40.0 (latest stable; all security patches including CSA-2026-001 included; Apache-2.0; actively maintained; ABCI++ stable interface).
- **Conservative LTS alternative:** v0.38.26 (latest v0.38.x patch, explicitly listed as supported, Go 1.22).
- **Excluded:** `main` (unreleased, 43 commits ahead, not a production dependency per CometBFT policy).
- **No material architectural incompatibility identified** for running CometBFT as a separate process with a deterministic C++ ABCI application over a local Unix-domain socket. The dominant integration risks are (a) Go toolchain availability, (b) guaranteeing deterministic C++ application execution (no wall clock, unordered iteration, floating point, or filesystem input in state transitions), and (c) correct activation-height handling of validator updates. These are addressed in ADR-0004 and the determinism requirements.

## Verification evidence

- Release page, changelog, go.mod, pkg.go.dev, GitHub Advisory Database entries for GHSA-c32p-wcqj-j677 and ASA-* advisories (fetched 2026-11-17).
- Prototype build/test results: NONE (Go unavailable in environment). Recorded as a prerequisite, not a pass/fail.

## Cross-references

- [`adr/adr-0004-bft-consensus-cometbft-integration.md`](../adr/adr-0004-bft-consensus-cometbft-integration.md)
- [`decisions/decision-register.md`](../decisions/decision-register.md) — D-017 APPROVED.
- [`operations/verification-status.md`](../operations/verification-status.md) — verification ledger.
- [`operations/implementation-status.md`](../operations/implementation-status.md) — implementation ledger.
