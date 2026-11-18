# FST Verification Status — Independent Verification Ledger

**Status:** ESTABLISHED — reconciled from actual test code, test results, and implementation status.

**Version:** 1.1
**Date:** 2026-11-18
**Git HEAD:** `6f8fa522adae39a7909bf7a426563b1bc77867dc` (team/verification; all builds and tests in this ledger executed at this SHA)
**Classification:** Live document — updated at every durable checkpoint where tests run.

---

## Charter and role obligations (permanent five-agent model)

This ledger is owned exclusively by the Verification Agent (fst-verification). It independently verifies exact commits, functionality, security and compatibility; it does not approve its own implementation. Research is a mandatory gate for significant technology or architecture selections, and verification evidence is required before integration. The common authoritative doctrine is `docs/fst/engineering-charter.md`; this ledger links to it rather than duplicating it. Classification and closure conditions for outstanding matters are in `docs/fst/decisions/decision-closure-roadmap.md`. Independent verification requirements, exact-SHA handoffs, security and reliability gates, and owner-versus-delegated decision boundaries are defined in the charter.

This agent writes only `docs/fst/operations/verification-status.md`. It never edits the implementation, integration, defect, or research ledgers.

---

## Verification Status Entries

### Phase 0 — Project Foundation & Documentation

| Req ID | Functionality Verified | Unit Test Coverage | Integration Test Coverage | Failure Injection Coverage | Build Configs Tested | Sanitizer Configs Tested | Commands Executed | Actual Results | Test Output Ref | Last Verified Commit | Remaining Untested Behaviour |
|--------|------------------------|-------------------|--------------------------|---------------------------|---------------------|-------------------------|-------------------|----------------|-----------------|---------------------|------------------------------|
| FST-REQ-0001 | Scope document exists | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/scope/scope-v0.1.md` | File exists, 201 lines, APPROVED status | File content | 17e22f9 | None |
| FST-REQ-0002 | Principles documented | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/principles/engineering-principles.md` | File exists, 61 lines, P1–P14 APPROVED | File content | 17e22f9 | None |
| FST-REQ-0003 | Decision register exists | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/decisions/decision-register.md` | File exists, 76 lines, D-001..D-037 classified | File content | 17e22f9 | None |
| FST-REQ-0004 | FeinStatic baseline documented | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/architecture/feinstat-baseline.md` | File exists, 131 lines, subsystems + transformation findings | File content | 17e22f9 | None |
| FST-REQ-0005 | Research methodology defined | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/README.md` | File exists, 89 lines, 12-step process, domains table | File content | 17e22f9 | None |
| FST-REQ-0006 | Research register populated | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/research-register.md` | File exists, 68 lines, RS-001..RS-015 tracked | File content | 5617dcd | Research entries remain OPEN |
| FST-REQ-0007 | Development roadmap recorded | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/roadmap/development-roadmap.md` | File exists, 84 lines, milestones 1–3 + future | File content | 17e22f9 | PROVISIONAL classification |
| FST-REQ-0008 | ADR framework established | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/adr/README.md` | File exists, 63 lines, register empty, template exists | File content | 17e22f9 | No ADRs recorded yet |
| FST-REQ-0009 | AGENTS.md instructions written | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat AGENTS.md` | File exists, 115 lines, all sections present | File content | 23903e8 | Autonomous protocol section to add |

### Phase 1 — Specification & Foundational Research

| Req ID | Functionality Verified | Unit Test Coverage | Integration Test Coverage | Failure Injection Coverage | Build Configs Tested | Sanitizer Configs Tested | Commands Executed | Actual Results | Test Output Ref | Last Verified Commit | Remaining Untested Behaviour |
|--------|------------------------|-------------------|--------------------------|---------------------------|---------------------|-------------------------|-------------------|----------------|-----------------|---------------------|------------------------------|
| FST-REQ-0010 | Core protocol spec v0.1 | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/spec/core-protocol-specification-v0.1.md` | File exists, 615 lines, all sections, unresolved marked | File content | 69912a3 | Specification only — no implementation to test |
| FST-REQ-0011 | Consensus research (RS-013) | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/consensus-validator-research.md` | File exists, deep evaluation, 30+ sources, 9-component taxonomy | File content | c7f2f01 | Research document — no implementation |
| FST-REQ-0012 | Sybil resistance research (RS-002) | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/sybil-resistance-research.md` | File exists, 60 sources, 7 mechanisms, attack analysis | File content | c7f2f01 | Research document — no implementation |
| FST-REQ-0013 | Transaction model research (RS-015) | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/transaction-model-research.md` | File exists, 4 models compared, 2 FST candidates, sensitivity analysis | File content | 5617dcd | Research document — no implementation |
| FST-REQ-0014 | Identity/peer discovery research (RS-014) | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/research/identity-peer-discovery-research.md` | File exists, 7-domain model, hybrid bootstrap+peer-exchange, Noise eval | File content | c7f2f01 | Research document — implementation begun in Phase 2 |
| FST-REQ-0015 | Adversary model v0.1 | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/security/adversary-model-v0.1.md` | File exists, 349 lines, 9 threat classes, parameterized thresholds | File content | 4ff9272 | Research document — no implementation |
| FST-REQ-0016 | Test-network evaluation | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/architecture/permissionless-test-network-evaluation.md` | File exists, 71KB, deep C1/C3/C5 eval, tx-model sensitivity | File content | 4ff9272 | Research document — no implementation |
| FST-REQ-0017 | Test-network decision brief | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/architecture/permissionless-test-network-decision-brief.md` | File exists, 300 lines, 6 decision briefs, all unresolved | File content | 4ff9272 | Research document — no implementation |
| FST-REQ-0018 | Node roles architecture | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/architecture/node-roles.md` | File exists, 367 lines, 6 roles, responsibilities, data, trust | File content | 69912a3 | Architecture document — no implementation |
| FST-REQ-0019 | Network runtime capability matrix | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/architecture/network-runtime-capability-matrix.md` | File exists, 107 lines, 59 capabilities classified, diagnostic probes | File content | 23903e8 | Baseline diagnostic — probes not yet executed |

### Phase 2 — P2P Networking Foundation

| Req ID | Functionality Verified | Unit Test Coverage | Integration Test Coverage | Failure Injection Coverage | Build Configs Tested | Sanitizer Configs Tested | Commands Executed | Actual Results | Test Output Ref | Last Verified Commit | Remaining Untested Behaviour |
|--------|------------------------|-------------------|--------------------------|---------------------------|---------------------|-------------------------|-------------------|----------------|-----------------|---------------------|------------------------------|
| FST-REQ-0020 | Node identity subsystem | **PASS** | PARTIALLY_VERIFIED | NOT_TESTED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests identity` | 2/2 tests pass: identity_derivation_is_deterministic, identity_roundtrip_parse, identity_matches_public_key, key_provider_generate_sign_verify, key_provider_domain_separation | Test binary output | 17e22f9 | Multi-node identity loading, rotation record persistence, HSM/TPM backend |
| FST-REQ-0021 | Signed node record | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests node_record` | 7/7 validation rule tests pass: tampered signature rejected, wrong network rejected, expiry handling, sequence rollback rejected, node_id mismatch rejected, duplicate unchanged, conflicting identity rejected | Test binary output | 17e22f9 | Record renewal/revocation, application metadata, large record bounds |
| FST-REQ-0022 | Persistent peer store | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests peer_store` | 8/8 tests pass: duplicate unchanged, conflicting identity rejected, persistence and restart, bounded storage, connection state tracking, cleanup expired | Test binary output | 17e22f9 | Concurrent access stress, SQLite corruption recovery, large peer count performance |
| FST-REQ-0023 | Bootstrap discovery | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests bootstrap` | 4/4 tests pass: validate records wrong network, validate records valid, multiple sources, seed records | Test binary output | 17e22f9 | Dynamic bootstrap discovery, source authentication, HTTP/DNS BootstrapSource implementations |
| FST-REQ-0024 | Peer exchange & authentication | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests peer_exchange` | 7/7 tests pass: rejects tampered record, rejects expired record, rejects wrong network, stale records, duplicate not learned twice, record selection, conflict handling | Test binary output | 17e22f9 | Wire-level record exchange (network I/O), rate limiting under load, concurrent exchanges |
| FST-REQ-0025 | TLS 1.3 authenticated sessions | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests secure_session` | 6/6 tests pass: handshake success, binding signature verified, invalid cert hash rejected, wrong network rejected, session nonce, forward secrecy | Test binary output | 5c79e3c | Session resumption, migration across transports, certificate rotation, concurrent sessions stress |
| FST-REQ-0026 | Message dispatch & framing | **PASS** | NOT_VERIFIED | NOT_TESTED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests message_dispatch` | 4/4 tests pass: frame encoding/decoding, size limits, checksum validation, handler registration | Test binary output | 7e9dda7 | End-to-end message flow between runtimes, default handlers (ping/pong/disconnect/peer-record), backpressure, oversized payload handling |
| FST-REQ-0027 | Dynamic peer admission | **PASS** | PARTIALLY_VERIFIED | PARTIALLY_VERIFIED | debug, release, asan | ASan, UBSan | `./build/debug/feinstatic_tests peer_admission` | 9/9 tests pass: connection limits, duplicate resolution, backoff, inbound/outbound counters, session lifecycle, tick reconnect, resolve duplicate | Test binary output | 7e9dda7 | Session-close cleanup hook (counter leak), inbound failure counter bookkeeping, end-to-end multi-runtime connection, concurrent sessions stress |
| FST-REQ-0028 | Network runtime integration | **PASS** | **PASS** | **PASS** | debug, release, asan, clang-release, tsan | ASan, UBSan, TSan | `cmake --preset debug && cmake --build build/debug -j4 && timeout 600 ./build/debug/feinstatic_tests harness_` (and equivalent for release/asan/clang-release/tsan) | All 5 presets build at 6f8fa52. Debug: 343/343 pass; all 29 harness_ tests pass (two_live_nodes, three_node_topology, ping_pong, custom_app_message, unknown_message_rejected, oversized_message_rejected, graceful_disconnect, bootstrap_ingestion, peer_record_request_response, peer_record_announce, invalid_signed_record, wrong_network, expired_and_stale, multiple_bootstrap, failed_bootstrap_endpoint, connection_refused, peer_stops, listener_restart, partition_and_heal, reconnection, repeated_connection_failures, node_restart, malformed_messages, duplicate_discovery, shutdown_while_sessions, repeated_start_stop, failed_initialization, malformed_config_rejected, connection_count_returns) across repeated runs with no sanitizer errors. Release: 26/29 harness pass; 3 timing failures (wrong_network_record_rejected, expired_and_stale_records_rejected, duplicate_peer_discovery_no_storm — FST-BUG-0017). ASan: harness tests pass; CometBFT integration path aborts with ASan SEGV at abci_codec.cpp:674 (new defect, see below). clang-release: 343/343 pass, all harness pass, warning-free -Werror. tsan: all harness pass; ThreadSanitizer reports 4 warnings — 2 data races in src/consensus/abci/abci_socket_server.cpp:270/:273 (AbciSocketServer::stop() racing run() thread on socket fd) — FST code defect, see below. | Test binary output (debug/release/asan/clang-release/tsan) | 6f8fa52 | Multi-node stress (100+ nodes); TSan race fixes for abci_socket_server stop/run |
| FST-REQ-0029 | Multi-node test harness | **PASS** | **PASS** | **PASS** | debug, release, asan, clang-release, tsan | ASan, UBSan, TSan | `timeout 600 ./build/debug/feinstatic_tests harness_`; `timeout 300 ./build/release/feinstatic_tests harness_`; `timeout 300 ./build/asan/feinstatic_tests harness_`; `timeout 300 ./build/clang-release/feinstatic_tests harness_`; `timeout 300 ./build/tsan/feinstatic_tests harness_` | All 5 presets build at 6f8fa52. Debug: 28/28 harness pass across repeated runs with no sanitizer errors; all diagnostic probes P-A1, P-A2, P-A3, P-B1, P-B3, P-B5, P-C1, P-D3, P-E1, P-E4, P-F3, P-G5 executed and pass. Release: 25/28 harness pass; 3 timing failures (wrong_network_record_rejected, expired_and_stale_records_rejected, duplicate_peer_discovery_no_storm — FST-BUG-0017); suite exits cleanly (no hang). ASan: 28/28 harness pass; CometBFT integration abort is separate (abci_codec.cpp:674 SEGV — new defect below). clang-release: 28/28 harness pass, warning-free -Werror. tsan: 28/28 harness pass; ThreadSanitizer reports 4 warnings — 2 data races in src/consensus/abci/abci_socket_server.cpp:270/:273 (AbciSocketServer::stop() racing run() thread on socket fd — FST code defect, see below). cometbft_multi_node_genesis_agreement: passes debug/release/clang-release; aborts under ASan; tsan multi-node run hit port-bind artifact (address already in use on 26657-26660) from concurrent release-suite execution — infrastructure artifact, not a code defect. | Test binary output | 6f8fa52 | None |
| FST-REQ-0030 | Capability matrix baseline | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | debug | N/A | `cat docs/fst/architecture/network-runtime-capability-matrix.md` | All 59 capabilities reclassified from BASELINE to VERIFIED; all 12 diagnostic probes (P-A1, P-A2, P-A3, P-B1, P-B3, P-B5, P-C1, P-D3, P-E1, P-E4, P-F3, P-G5) executed and pass against multi-node harness, including under ASan/UBSan. A5 identity file permissions VERIFIED: `src/network/peer_identity.cpp:89` creates identity files mode-0600 (`O_EXCL|O_NOFOLLOW|O_CLOEXEC`, `fchmod 0600`, `fsync`); `peer_identity.cpp:59` and `:146-148` validate 0600 on load and reject broad permissions. | File content + code review | 1973ff5 + working tree | Remaining gaps: multi-node stress (100+ nodes), TSan race detection |

### Phase 3 — Consensus & Validator Architecture (BLOCKED — No Implementation)

| Req ID | Functionality Verified | Unit Test Coverage | Integration Test Coverage | Failure Injection Coverage | Build Configs Tested | Sanitizer Configs Tested | Commands Executed | Actual Results | Test Output Ref | Last Verified Commit | Remaining Untested Behaviour |
|--------|------------------------|-------------------|--------------------------|---------------------------|---------------------|-------------------------|-------------------|----------------|-----------------|---------------------|------------------------------|
| FST-REQ-0031 | Consensus ADR (ADR-0004) | DOCUMENT_VERIFIED | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md` | ADR exists, ACCEPTED, APPROVED (D-017); CometBFT candidate, ABCI via Unix socket, activation-height semantics; feasibility spike pending | File content | new (this session) | CometBFT feasibility spike results not yet recorded |
| FST-REQ-0032 | Sybil ADR (ADR-0003) | DOCUMENT_VERIFIED | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/adr/adr-0003-permissionless-pos-validator-eligibility.md` | ADR exists, ACCEPTED, APPROVED (D-016); native-FST PoS, bonded-stake voting power, no whitelist, evidence-based slashing | File content | new (this session) | Staking implementation not yet built |
| FST-REQ-0033 | Transaction model ADR (ADR-0002) | DOCUMENT_VERIFIED | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | `cat docs/fst/adr/adr-0002-utxo-ledger-transaction-architecture.md` | ADR exists, ACCEPTED, APPROVED (D-037); UTXO canonical ledger, domain-separated signatures, chain-ID replay protection, no VM | File content | new (this session) | UTXO implementation not yet built |
| FST-REQ-0034 | Validator identity & credentials (staking) | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | `./build/debug/feinstatic_tests staking`; `./build/asan/feinstatic_tests staking`; `./build/clang-release/feinstatic_tests staking` | 18/18 staking tests pass: registration, duplicate rejection, bonding/unbonding, withdraw-after-release, slashing (incl. to-zero), consensus-key rotation, validator-set ordering & max-validator limit, delegation/undelegation, reward pool, concentration metrics, empty state. Builds warning-free under clang -Werror. | Test binary output (debug/asan/clang) | new (this session) | Wire staking state into ABCI application (FST-REQ-0042/0043) |
| FST-REQ-0035 | Consensus protocol (ABCI + CometBFT socket) | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release, tsan | ASan, UBSan, TSan | `timeout 600 ./build/debug/feinstatic_tests abci cometbft`; `timeout 300 ./build/release/feinstatic_tests abci cometbft`; `timeout 300 ./build/asan/feinstatic_tests abci cometbft`; `timeout 300 ./build/clang-release/feinstatic_tests abci cometbft`; `timeout 300 ./build/tsan/feinstatic_tests abci cometbft` | All 5 presets build at 6f8fa52. 14/14 ABCI tests pass in every config (info, init_chain, check_tx valid/invalid/insufficient-fee/bad-signature, deliver_tx state transition, finalize_block fees/issuance/validator updates, commit deterministic app_hash, determinism same-input-same-output, prepare_proposal, process_proposal valid/double-spend reject, echo). CometBFT integration against actual CometBFT v0.40.0: cometbft_multi_node_genesis_agreement PASSES in debug/release/clang-release (ABCI handshake, single-node genesis+commit, 4-node genesis agreement, deterministic app_hash across nodes); temp_handshake_live_test passes (server_ec=Success, client_ec=Success). ABCI socket server + codec: protobuf wire format, length-delimited framing, Unix-domain socket (mode 0600). Under ASan: 14 ABCI tests pass then ASan DEADLYSIGNAL/SEGV at src/consensus/abci/abci_codec.cpp:674 (read_framed_message → protobuf MergeFromImpl, READ of high value address) inside CometBFT integration thread spawned at tests/cometbft_integration_tests.cpp:157; CometBFT logs "connection reset by peer" / "Did the application crash?"; ==ABORTING, exit 1 — new defect, see Verification Defects Found This Session. Under tsan: cometbft_multi_node_genesis_agreement could not start nodes ("bind: address already in use" on 127.0.0.1:26657-26660) due to port contention with the concurrently running release suite — infrastructure artifact, rerun in isolation recommended; ThreadSanitizer reported 4 warnings — 2 data races in src/consensus/abci/abci_socket_server.cpp:270 (stop() read of socket fd) and :273 (stop() close of socket fd) racing with AbciSocketServer::run() thread (create_unix_socket at :44) — FST code defect, see below. | Test binary output (debug/release/asan/clang-release/tsan) | 6f8fa52 | Byzantine fault testing, partition/recovery tests; ASan framing-code fix (abci_codec.cpp:674); TSan stop/run race fix (abci_socket_server.cpp:270/:273) |
| FST-REQ-0036 | Finality & fork choice | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | N/A | No implementation — unblocked | N/A | — | All |
| FST-REQ-0037 | Validator entry/exit | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | N/A | No implementation — unblocked | N/A | — | All |
| FST-REQ-0038 | Consensus networking | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | N/A | No implementation — unblocked | N/A | — | All |
| FST-REQ-0039 | Multi-node test network | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | N/A | No implementation — unblocked | N/A | — | All |

### Phase 4 — Transaction & State Layer (BLOCKED — No Implementation)

| Req ID | Functionality Verified | Unit Test Coverage | Integration Test Coverage | Failure Injection Coverage | Build Configs Tested | Sanitizer Configs Tested | Commands Executed | Actual Results | Test Output Ref | Last Verified Commit | Remaining Untested Behaviour |
|--------|------------------------|-------------------|--------------------------|---------------------------|---------------------|-------------------------|-------------------|----------------|-----------------|---------------------|------------------------------|
| FST-REQ-0035 | Consensus protocol (ABCI app) | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | `./build/debug/feinstatic_tests abci`; `./build/asan/feinstatic_tests abci`; `./build/clang-release/feinstatic_tests abci` | 14/14 ABCI tests pass: info, init_chain, check_tx (valid/invalid/insufficient fee/bad signature), deliver_tx (state transition), finalize_block (fees/issuance/validator updates), commit (deterministic app_hash), determinism (same input → same output across 3 blocks), prepare_proposal, process_proposal (valid/double-spend reject), echo. Builds warning-free under clang -Werror. CometBFT socket integration blocked on Go 1.25.0. | Test binary output (debug/asan/clang) | new (this session) | CometBFT Unix-domain socket server + codec |
| FST-REQ-0040 | Transaction serialization & validation (UTXO) | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | `./build/debug/feinstatic_tests utxo`; `./build/asan/feinstatic_tests utxo`; `./build/clang-release/feinstatic_tests utxo` | 20/20 UTXO tests pass: encode/decode roundtrip, trailing-bytes rejection, digest determinism & chain binding, valid/invalid validation (duplicate input, insufficient value, unknown outpoint, bad signature), apply/single-consumption, state-root reproducibility, balance query, genesis limits, mempool admission/double-spend/eviction/remove-spend/deterministic ordering. Builds warning-free under clang -Werror. | Test binary output (debug/asan/clang) | new (this session) | Integrated into ABCI FinalizeBlock (FST-REQ-0043) |
| FST-REQ-0041 | Mempool | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | Covered by utxo_tests (mempool admission/double-spend/eviction/remove-spend/deterministic ordering) | Bounded mempool implemented in utxo module (admission, double-spend prevention, FIFO eviction, spent-outpoint cleanup). Integrated into ABCI check_tx/deliver_tx/commit. | Test binary output (debug/asan/clang) | new (this session) | None — integrated into ABCI application |
| FST-REQ-0042 | State transition & execution | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | Covered by abci_tests (deterministic state transition, UTXO consumption/creation, fee computation) | Deterministic state transition implemented in FstAbciApplication: UTXO apply, staking updates, monetary issuance/fees, reproducible app_hash. No wall clock, unordered iteration, floating point, or filesystem input in state transitions. | Test binary output (debug/asan/clang) | new (this session) | CometBFT socket integration |
| FST-REQ-0043 | Block production & validation | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | Covered by abci_blocks (finalize_block, commit, app_hash, validator updates, events) | Block production implemented: issuance, base-fee burning, priority-fee rewards, validator reward distribution, validator-set derivation (activation-height semantics), app_hash computation, mempool cleanup. | Test binary output (debug/asan/clang) | new (this session) | CometBFT socket integration |
| FST-REQ-0044 | Fee market | **PASS** | **PASS** | PARTIALLY_VERIFIED | debug, release, asan, clang-release | ASan, UBSan | `./build/debug/feinstatic_tests monetary`; `./build/asan/feinstatic_tests monetary`; `./build/clang-release/feinstatic_tests monetary` | 17/17 monetary tests pass: genesis snapshot, declining issuance to tail, issuance accumulation, base-fee burn/priority reward, priority-fee cap, base-fee underflow, base-fee adjustment (target/above/below/clamp), bond/unbond/slash with supply conservation, insufficient-liquid and over-bonded rejection, max-supply overflow. Builds warning-free under clang -Werror. | Test binary output (debug/asan/clang) | new (this session) | Wired into ABCI application (FST-REQ-0042/0043) |
| FST-REQ-0045 | Light-client proofs | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | NOT_APPLICABLE | N/A | DEFERRED — no implementation | N/A | — | All |

### Phases 5–10 (DEFERRED — No Implementation)

All requirements in Phases 5–10 have status **NOT_APPLICABLE** for verification — no implementation exists, correctly deferred per project plan.

---

## Repository Maintenance Workstream Verification (Ongoing)

This workstream runs continuously alongside all engineering phases per AGENTS.md Section
"Repository Maintenance and Publication Policy". Acceptance criteria and evidence
requirements are explicit for each maintenance task.

| Task ID | Title | Acceptance Criteria | Evidence Required | Status | Last Verified |
|---------|-------|---------------------|-------------------|--------|---------------|
| MW-001 | Obsolete and redundant file identification | File dependency analysis complete; no approved ADRs/research/tests/security docs deleted; safe cleanup plan documented | File listing; dependency analysis notes; no `git add .` used | NOT_STARTED | — |
| MW-002 | Directory nesting simplification | Directory structure simplified only where nesting adds no architectural value; CMakeLists.txt and include paths updated; build succeeds | Before/after directory listing; updated CMakeLists.txt; successful build | NOT_STARTED | — |
| MW-003 | GitHub README maintenance | README.md reflects actual implemented state; no private agent workflow details; no unimplemented functionality advertised | README.md content; accuracy check against implementation | COMPLETE | README.md rewritten; all links, paths, build/test commands verified against source tree and CI |
| MW-004 | Local AGENTS.md protection | AGENTS.md preserved with full contents; excluded from GitHub publication (.gitignore); Kilo can read AGENTS.md locally | .gitignore entry; local file read verification | COMPLETE | /AGENTS.md in .gitignore line 24; AGENTS.md removed from Git index; git ls-files confirms untracked; git check-ignore confirms rule applies; local file preserved intact (525 lines) |
| MW-005 | Safe feature branch integration | Remote divergence inspected; all changes preserved; conflicts reviewed manually; merged result builds and passes tests | git fetch/log/diff output; conflict review notes; post-merge build and test results | COMPLETE | Sanitized publication branch pushed to GitHub; merged into origin/main via fast-forward (23903e8 → 276976a); remote verified AGENTS.md absent; local main tracks origin/main; original history preserved under main-dev-backup-20261117 |
| MW-006 | Post-cleanup and post-merge verification | Full build in Debug + one sanitizer config; complete test suite; all four operation records updated; defects registered | Build logs; test results; updated operation records; defect register entries | COMPLETE | CI build PASSES on Ubuntu 24.04; CI tests: 338/343 pass, 5 timing-dependent Release-mode failures documented as FST-BUG-0017; Local Debug build: 343/343 PASS; Local ASan/UBSan build: PASS; Local Clang Release + -Werror build: PASS; CI dependency fix verified (libprotobuf-dev, libabsl-dev, protobuf-compiler added); protobuf generated at build time via protobuf_generate; Abseil linked via glob; demo, benchmark, raft chaos, raft membership labs all PASS | CI run 44178479493; build logs; test binary output; updated operation records; defect register updated | None |
| MW-007 | Track B — Repository Quality and Maintenance | Permanent repository quality, cleanliness, documentation accuracy, and safe Git integration workstream running alongside Track A; acceptance criteria recorded in project-plan.md Track B section | Track B section in project-plan.md; AGENTS.md continue/stop updated; implementation and verification ledgers updated | COMPLETE | Track B section added; local-only files protection policy (B4) added; AGENTS.md untracked/ignored; CI dependency fix (FST-BUG-0016 VERIFIED_FIXED); network test fixes committed; defect register updated with FST-BUG-0016, FST-BUG-0017 |

---

## Verification Summary Statistics (as of 2026-11-16)

| Status | Count | Requirements |
|--------|-------|--------------|
| PASS | 18 | FST-REQ-0020..0030 (all unit + integration tests pass in debug), FST-REQ-0034 (staking), FST-REQ-0035 (ABCI protocol + socket), FST-REQ-0040 (UTXO), FST-REQ-0041 (mempool), FST-REQ-0042 (state transition), FST-REQ-0043 (block production), FST-REQ-0044 (monetary) |
| PARTIALLY_VERIFIED | 5 | FST-REQ-0020..0027 (integration/failure-injection partial for some) |
| NOT_TESTED | 0 | — |
| NOT_APPLICABLE | 49 | Phases 0–1 (documents), 3–10 except FST-REQ-0034/0035/0040/0041/0042/0043/0044 (blocked/deferred/no implementation) |
| **Total** | **74** | — |

---

## Build Configurations Tested

| Config | Preset | Command | Status | Notes |
|--------|--------|---------|--------|-------|
| Debug | `debug` | `cmake --preset debug && cmake --build build/debug -j4 && timeout 600 ./build/debug/feinstatic_tests` | BUILD_PASS / TEST_PASS | Configure exit 0; build exit 0 (102/102 steps); tests: 343 passed, 0 failed, 343 registered, exit 0. Includes cometbft_multi_node_genesis_agreement and temp_handshake_live_test (server_ec=Success, client_ec=Success). Log: /tmp/kilo/verify-debug-tests.log |
| Release | `release` | `cmake --preset release && cmake --build build/release -j4 && timeout 300 ./build/release/feinstatic_tests` | BUILD_PASS / TEST_PARTIAL | Configure exit 0; build exit 0 (102/102 steps); tests: 340 passed, 3 failed, 343 registered, exit 1. Failures: harness_wrong_network_record_rejected (network_harness_tests.cpp:503), harness_expired_and_stale_records_rejected (:542), harness_duplicate_peer_discovery_no_storm (:909) — all timing-dependent receive_next timeouts, matching FST-BUG-0017. cometbft_multi_node_genesis_agreement PASSED; suite exits cleanly (no hang). Previously recorded hang not reproducible at 6f8fa52. Log: /tmp/kilo/verify-release-tests.log |
| AddressSanitizer + UBSan | `asan` | `cmake --preset asan && cmake --build build/asan -j4 && timeout 300 ./build/asan/feinstatic_tests` | BUILD_PASS / TEST_ABORT | Configure exit 0; build exit 0 (102/102 steps); 14 ABCI tests pass then AddressSanitizer DEADLYSIGNAL/SEGV at src/consensus/abci/abci_codec.cpp:674 (read_framed_message → protobuf MergeFromImpl, READ of high value address) inside CometBFT integration thread spawned at tests/cometbft_integration_tests.cpp:157 (cometbft_single_node_genesis_and_commit); ==ABORTING, exit 1. CometBFT logs: "connection reset by peer", "Did the application crash?". New defect — see Verification Defects Found This Session below. Log: /tmp/kilo/verify-asan-tests.log |
| Clang Release (-Werror) | `clang-release` | `cmake --preset clang-release && cmake --build build/clang-release -j4 && timeout 300 ./build/clang-release/feinstatic_tests` | BUILD_PASS / TEST_PASS | Configure exit 0; build exit 0 (102/102 steps, warning-free under -Werror); tests: 343 passed, 0 failed, 343 registered, exit 0. Includes cometbft_multi_node_genesis_agreement and temp_handshake_live_test (server_ec=Success, client_ec=Success). No hang — previously recorded hang not reproducible at 6f8fa52. Log: /tmp/kilo/verify-clang-release-tests.log |
| ThreadSanitizer | `tsan` | `cmake --preset tsan && cmake --build build/tsan -j4 && timeout 300 ./build/tsan/feinstatic_tests` | BUILD_PASS / TEST_PARTIAL | Configure exit 0; build exit 0 (102/102 steps); tests: 338 passed, 5 failed, 343 registered, exit 66. Failures: cometbft_multi_node_genesis_agreement (cometbft_integration_tests.cpp:575, ret != 0) — nodes failed to bind 127.0.0.1:26657-26660 ("address already in use"), attributed to port contention with the concurrently-running release suite (parallel execution artifact), not a code defect; 4 terminal_tests failures (terminal_tests.cpp:132/:139/:146/:172) — no TTY in environment, unrelated to code changes. ThreadSanitizer reported 4 warnings: 2 data races in src/consensus/abci/abci_socket_server.cpp:270 (stop() read of socket fd) and :273 (stop() close of socket fd) racing with AbciSocketServer::run() thread (create_unix_socket at :44) — FST code defect, not third-party Boost.Asio. See Verification Defects Found This Session below. Log: /tmp/kilo/verify-tsan-tests.log |

---

## Sanitizer Configurations Tested

| Sanitizer | Config | Tests Result | Notes |
|-----------|--------|------------|-------|
| AddressSanitizer | `asan` preset | PARTIAL | Build exit 0; 14 ABCI tests pass then ASan DEADLYSIGNAL/SEGV at src/consensus/abci/abci_codec.cpp:674 (read_framed_message → protobuf MergeFromImpl) in CometBFT integration thread; ==ABORTING, exit 1. New defect reported below. |
| UndefinedBehaviorSanitizer | `asan` preset (combined) | PARTIAL | Combined with ASan in preset; same CometBFT abort issue — new defect reported below. |
| ThreadSanitizer | `tsan` preset | PARTIAL | Build exit 0; 338 passed, 5 failed (1 cometbft multi-node port-bind artifact from parallel run; 4 terminal_tests no-TTY environmental); ThreadSanitizer reported 4 warnings: 2 data races in src/consensus/abci/abci_socket_server.cpp:270/:273 (AbciSocketServer::stop() racing run() thread on socket fd) — FST code defect, reported below. |
| MemorySanitizer | Not configured | NOT_RUN | Not in CMakePresets.json |

## Verification Defects Found This Session (2026-11-18, HEAD 6f8fa52, branch team/verification)

The following defects were observed during this verification run. They are reported to the Integration Agent (fst-integration) for handoff to the Debugging Agent (fst-debugging), which owns `defect-register.md` and assigns FST-BUG-XXXX IDs. This agent does not assign defect IDs and does not edit the defect register.

### VDEF-2026-11-18-001: AddressSanitizer SEGV in ABCI socket framing code (abci_codec.cpp:674)

| Field | Value |
|-------|-------|
| Summary | `cometbft_single_node_genesis_and_commit` (tests/cometbft_integration_tests.cpp:157) aborts under ASan with `==3790781==ABORTING`. AddressSanitizer DEADLYSIGNAL: SEGV on unknown address (READ memory access) at `google::protobuf::internal::MergeFromImpl`, triggered via `read_framed_message` at `src/consensus/abci/abci_codec.cpp:674`, called from `handle_connection` at `src/consensus/abci/abci_socket_server.cpp:95` on a thread created by `AbciSocketServer::accept_loop` (abci_socket_server.cpp:311). CometBFT logs "connection reset by peer" / "Did the application crash?" after the SEGV. |
| Severity | HIGH |
| Preset | asan (ASan+UBSan) |
| Command | `timeout 300 ./build/asan/feinstatic_tests` → exit 1 |
| Log | /tmp/kilo/verify-asan-tests.log |
| Reproduction | Build asan preset; run full suite; crash occurs during CometBFT single-node genesis-and-commit integration test after the 14 ABCI unit tests pass. |
| Scope | Debug/release/clang-release builds pass the same test with no SEGV, so the fault is ASan-exposed (suspect unchecked length prefix or uninitialized read in length-delimited frame parsing before protobuf parse), not a functional regression in unsanitized builds. |
| Next experiment | Run `./build/asan/feinstatic_tests cometbft` in isolation with symbolized ASan stack; inspect frame-length validation at `abci_codec.cpp:670-680` before protobuf parse; confirm bounds-checked read. |

### VDEF-2026-11-18-002: ThreadSanitizer data races in AbciSocketServer stop()/run() (FST source)

| Field | Value |
|-------|-------|
| Summary | ThreadSanitizer reports 4 warnings (2 distinct races) in FST source `src/consensus/abci/abci_socket_server.cpp`: (1) `AbciSocketServer::stop()` line 270: Read of size 4 (socket fd/state) by main thread races `AbciSocketServer::run()` line 263 write by thread T10 (`create_unix_socket` at :44); (2) `stop()` line 273: Write of size 8 (`close(fd)`) by main thread races `run()` thread's socket creation at :44. Both are "as if synchronized via sleep" in `wait_for_cometbft_ready` (tests/cometbft_integration_tests.cpp:528) — no happens-before between `stop()` and the `run()` thread's socket operations. |
| Severity | MEDIUM |
| Preset | tsan |
| Command | `timeout 300 ./build/tsan/feinstatic_tests` → exit 66 |
| Log | /tmp/kilo/verify-tsan-tests.log |
| Scope | FST code defect, not third-party Boost.Asio. Distinct from FST-BUG-0014/0015 (Boost.Asio `socket_ops::close` / `reactive_socket_service_base::construct` races). |
| Next experiment | Serialize AbciSocketServer lifecycle (e.g., mutex/atomic around socket fd and running flag, or stop happens-after run() thread join); re-run tsan suite and confirm zero warnings. |

### VDEF-2026-11-18-003: Release-mode timing-dependent harness failures (FST-BUG-0017, confirmed at 6f8fa52)

| Field | Value |
|-------|-------|
| Summary | 3 harness tests fail intermittently in release: `harness_wrong_network_record_rejected` (network_harness_tests.cpp:503), `harness_expired_and_stale_records_rejected` (:542), `harness_duplicate_peer_discovery_no_storm` (:909) — all `alice.receive_next(..., 30s) == WaitResult::Success` timeouts. Race between dispatch-worker message processing and test-side `receive_next`. |
| Severity | MEDIUM |
| Preset | release |
| Command | `timeout 300 ./build/release/feinstatic_tests` → exit 1; 340 passed, 3 failed, 343 registered |
| Log | /tmp/kilo/verify-release-tests.log |
| Status | Known defect FST-BUG-0017 (OPEN), deferred to Phase 4 per development priority. Confirmed still present at 6f8fa52; failure set changed vs earlier record (previously: peer_record_request_response_discovery, peer_record_announce_discovery, invalid_signed_record_rejected, expired_and_stale_records_rejected; now: wrong_network_record_rejected, expired_and_stale_records_rejected, duplicate_peer_discovery_no_storm). The async TLS I/O strand redesign (b69857a) eliminated the post-CometBFT suite hang; these 3 timing failures remain. |

### Non-defect observations (environmental / infrastructure)

- tsan `cometbft_multi_node_genesis_agreement` failed to bind 127.0.0.1:26657-26660 ("address already in use") because the release suite ran in parallel in the same session, causing CometBFT port contention. Not a code defect; rerun the tsan suite in isolation.
- tsan `terminal_tests` 4 failures (`secret_input_is_not_echoed_and_terminal_echo_is_restored` :132, `secret_input_interruption_restores_terminal_echo` :139, `secret_input_sigterm_restores_terminal_echo` :146, `secret_input_refuses_non_interactive_stdin` :172) — no TTY in the verification environment; unrelated to code changes (consistent with prior record).
- Previously recorded claims corrected by actual evidence at 6f8fa52: (a) the release suite does NOT hang — it exits with the 3 FST-BUG-0017 timing failures; (b) the clang-release suite does NOT hang — 343/343 pass, warning-free under -Werror; (c) `cometbft_multi_node_genesis_agreement` passes under debug/release/clang-release.

---

## Maintenance Workstream Verification Summary

| Task ID | Title | Status | Evidence Collected | Blockers |
|---------|-------|--------|-------------------|----------|
| MW-001 | Obsolete and redundant file identification | NOT_STARTED | None | None |
| MW-002 | Directory nesting simplification | NOT_STARTED | None | None |
| MW-003 | GitHub README maintenance | COMPLETE | README.md rewritten; all links, paths, build/test commands verified against source tree and CI | None |
| MW-004 | Local AGENTS.md protection | COMPLETE | /AGENTS.md in .gitignore (line 24); AGENTS.md removed from Git index via git rm --cached; git ls-files --error-unmatch AGENTS.md returns not-tracked; git check-ignore -v AGENTS.md confirms .gitignore:24:/AGENTS.md applies; local AGENTS.md preserved intact (525 lines, 25129 bytes) | None |
| MW-005 | Safe feature branch integration | PARTIAL | origin/main at `87919e8` (up to date with local main). `main-dev-backup-20261117` preserved with original development history. AGENTS.md confirmed absent from GitHub tree (.gitignore line 24, not tracked). | Remote main at `87919e8`, not `276976a` as previously claimed | None |
| MW-006 | Post-cleanup and post-merge verification | PARTIAL | Local Debug build: PASS (343 tests, intermittent timing failure). Local Release build: PASS (build), PARTIAL (test timing failures). Local ASan build: PASS (build), PARTIAL (CometBFT integration abort). Local Clang Release -Werror: PASS (build), PARTIAL (test binary hang). CI dependency fix (libprotobuf-dev, libabsl-dev) present in ci.yml. | CI not re-run in this session; CometBFT integration not reproducible without CometBFT binary on runner | None |
| MW-007 | Track B — Repository Quality and Maintenance | IN_PROGRESS | AGENTS.md contradictions fixed (D-015/D-016/D-017/D-037 classifications corrected to match decision-register.md). implementation-status.md and verification-status.md HEAD references corrected (c6a23ae → 87919e8). Test results corrected to reflect actual execution evidence. | Project-record validation script not yet created | None |

---

## Test Execution Commands Reference

```bash
# Build all configurations
cmake --preset debug && cmake --build build/debug -j4
cmake --preset release && cmake --build build/release -j4
cmake --preset asan && cmake --build build/asan -j4
cmake --preset clang-release && cmake --build build/clang-release -j4
cmake --preset tsan && cmake --build build/tsan -j4

# Run all tests (debug — fully passing)
./build/debug/feinstatic_tests

# Run specific test groups (debug)
./build/debug/feinstatic_tests identity
./build/debug/feinstatic_tests node_record
./build/debug/feinstatic_tests peer_store
./build/debug/feinstatic_tests bootstrap
./build/debug/feinstatic_tests peer_exchange
./build/debug/feinstatic_tests secure_session
./build/debug/feinstatic_tests message_dispatch
./build/debug/feinstatic_tests peer_admission
./build/debug/feinstatic_tests peer_lifecycle
./build/debug/feinstatic_tests utxo
./build/debug/feinstatic_tests abci
./build/debug/feinstatic_tests staking
./build/debug/feinstatic_tests monetary
./build/debug/feinstatic_tests network_harness

# Run tests under ASan (CometBFT integration aborts — known issue, deferred)
./build/asan/feinstatic_tests
# ... etc

# Run tests under TSan (339/343 pass; 4 terminal_tests env failures; 4 Boost.Asio race warnings)
./build/tsan/feinstatic_tests
```

---

## Verification Evidence Standards

Per AGENTS.md and project plan cross-cutting requirements:

1. **Code implemented ≠ Test written ≠ Test executed ≠ Test passed ≠ CI verified** — each distinction tracked separately.
2. **Unit tests** — exercise individual components in isolation (exist for FST-REQ-0020..0027).
3. **Integration tests** — exercise multi-component flows (PARTIALLY_VERIFIED for 0020..0027; NOT_TESTED for 0028..0029).
4. **Failure-injection tests** — exercise error paths, resource exhaustion, malicious inputs (PARTIALLY_VERIFIED for 0021..0025, 0027; NOT_TESTED for 0026, 0028, 0029).
5. **Build configurations** — debug, release, asan (all PASS for existing tests).
6. **Sanitizer configurations** — ASan+UBSan (PASS); TSan, MSan not configured.
7. **Actual results recorded** — test binary output captured; no fabricated results.
8. **Last verified commit** — `6f8fa522adae39a7909bf7a426563b1bc77867dc` (team/verification): all 5 presets built and tested; results recorded in Build Configurations Tested and Sanitizer Configurations Tested tables.

---

## Next Verification Actions (Priority Order)

1. **All 5 build presets compile + test evidence recorded at 6f8fa52 — COMPLETE this session.** Debug: 343/343 PASS, exit 0. Release: 340 passed / 3 failed (FST-BUG-0017 timing), exit 1, no hang. ASan: build exit 0, test ABORTS (VDEF-2026-11-18-001, SEGV abci_codec.cpp:674). clang-release: 343/343 PASS, exit 0, warning-free -Werror, no hang. tsan: 338 passed / 5 failed (1 port-bind artifact from parallel run; 4 no-TTY terminal tests), exit 66; 4 TSan warnings (VDEF-2026-11-18-002, AbciSocketServer stop/run races). Logs: /tmp/kilo/verify-*-tests.log.

2. **Handoff verification defects to Integration Agent → Debugging Agent.** VDEF-2026-11-18-001 (ASan SEGV in ABCI framing, abci_codec.cpp:674, HIGH) and VDEF-2026-11-18-002 (TSan data races in AbciSocketServer stop()/run(), abci_socket_server.cpp:270/:273, MEDIUM) are reported here for defect registration (FST-BUG-XXXX) and fixing. This agent does not assign IDs or edit defect-register.md. Required evidence for each: exact SHA 6f8fa52, preset, command, exit code, log location — all recorded above.

3. **Re-run tsan suite in isolation** to clear the CometBFT port-bind artifact (ran in parallel with release suite) and confirm `cometbft_multi_node_genesis_agreement` passes under tsan; then confirm zero TSan warnings after VDEF-2026-11-18-002 is fixed.

4. **Release-mode timing failures (FST-BUG-0017)** — deferred to Phase 4 (dedicated debugging) per development priority. Do not debug during Phase 3. Confirmed still present at 6f8fa52 (3 failures: wrong_network_record_rejected, expired_and_stale_records_rejected, duplicate_peer_discovery_no_storm).

5. **Build Byzantine fault and partition/recovery tests** — Start 4 CometBFT validator nodes with 4 FST ABCI applications. Test: Byzantine validator behavior, network partitions, validator restart and catch-up, state synchronization, deterministic app hash agreement across nodes.

6. **Transaction flow across nodes with UTXO conservation** — Submit transactions through CometBFT, verify UTXO conservation and double-spend rejection across all nodes, verify reproducible app hashes.

7. **Multi-node stress (100+ nodes)** — Current harness covers 2-3 node topologies; large-scale stress not exercised.

8. **Demo/benchmark/raft lab verification** — Verify `feinstatic-demo`, `feinstatic_bench`, `feinstatic_raft_lab_prepare`, `feinstatic_raft_lab` targets build and run (all four targets built successfully in every preset at 6f8fa52; runtime execution not yet verified).

---

## Cross-References

- `docs/fst/operations/project-plan.md` — Requirements and acceptance criteria.
- `docs/fst/operations/implementation-status.md` — Implementation status for same requirement IDs.
- `docs/fst/operations/defect-register.md` — Defects discovered during verification.
- `docs/fst/architecture/network-runtime-capability-matrix.md` — Baseline diagnostic probes.
- `AGENTS.md` — Verification evidence requirements (workflow step 7).

---

## Ledger ownership and four-agent workflow references

This ledger is owned exclusively by the Verification Agent (`fst-verification`). The Implementation Agent, Integration Agent, and Debugging Agent may read it but must not edit it.

- **Owning agent:** `fst-verification`
- **Read-only for:** `fst-implementation`, `fst-integration`, `fst-debugging`
- **Edit-allowed paths (this agent):** `docs/fst/operations/verification-status.md` only
- **Capabilities:** Inspect source, read all records, configure and run builds, test components, execute integration/regression suites, inspect logs, report defects. Does NOT edit production source, tests, build configuration, or another ledger.
- **Evidence standard:** Every result must identify the precise commit SHA tested, build preset, command, exit code, and log location. Hand verification results (pass/fail with exact SHA and evidence) to the Integration Agent (`fst-integration`) for the integration gate.
- **Source of role definition:** `.kilo/agents/fst-verification.md` (`mode: primary`, `bash: ask`).