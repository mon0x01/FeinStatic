# ADR-0001: Candidate-Neutral Multi-Node Network Test Harness

## Metadata

| Field | Value |
|-------|-------|
| ID | `ADR-0001` |
| Title | Candidate-neutral multi-node network integration and fault-injection test harness |
| Date | 2026-11-15 |
| Status | PROPOSED |
| Classification | OPEN RESEARCH pending owner review |
| Author | FST engineering (Kilo) |
| Reviewed by | — (owner review required) |
| Supersedes | — |
| Superseded by | — |

## Problem statement

FST's networking subsystem (introduced in Milestones 2C–2D, `src/network/`) is not yet exercised as a running multi-node system. Existing tests cover units (peer store, bootstrap discovery, peer exchange, secure session handshake, message dispatch, peer lifecycle) and legacy authenticated-TCP scenarios coupled to the Ledger and Raft (`tests/network_tests.cpp`, `tests/network_integration_tests.cpp`), but there is no harness that instantiates multiple independent `NetworkRuntime` nodes, establishes real authenticated peer sessions between them, drives bootstrap and peer discovery, and injects network faults deterministically.

Without such a harness it is not possible to verify:
- multiple independent node identities and authenticated sessions,
- bootstrap and peer discovery between live nodes,
- peer admission, connection lifecycle, and reconnection with backoff,
- wrong-network rejection and isolation between live peers,
- node restart and reconnection,
- multiple independent bootstrap sources with no privileged source,
- network partitions and recovery,
- connection failures and resource limits,
- application-message-level fault injection (malformed/oversized/duplicate messages),
- adversarial peer behaviour against existing network boundaries (connection limits, payload limits, invalid signatures, wrong-network identities).

This ADR proposes a reusable, candidate-neutral harness architecture. It does **not** implement consensus, validators, transactions, blocks, monetary policy, or any Sybil-resistance mechanism. It is an engineering-enablement deliverable: once accepted, a separate implementation milestone can build stages 1–4 without repeating this research.

## Existing research and references

- FeinStatic Technical Baseline, `docs/fst/architecture/feinstat-baseline.md` — networking (protocol v8), authenticated TCP peer transport, Ed25519 mutual challenge-response, peer lifecycle circuit breaker, endpoint registry, Raft consensus preserved as reference.
- FST Engineering Principles, `docs/fst/principles/engineering-principles.md` (P1–P14) — no unilateral control, no permanent privileged authority, fork safety, founder independence, research-first (P12).
- FST Decision Register, `docs/fst/decisions/decision-register.md` — D-015–D-037 classifications preserved; D-036 (fork protection requirements) APPROVED; D-016 PROVISIONAL; D-017 OPEN RESEARCH.
- FST Adversary Model v0.1, `docs/fst/security/adversary-model-v0.1.md` — threat classes T-01..T-013 (malicious node operators, Sybil operators, validator coalitions, network-level attackers, malicious bootstrap providers, compromised implementations), parameterized thresholds, no universal honest-majority constant.
- FST Permissionless Test-Network Evaluation, `docs/fst/architecture/permissionless-test-network-evaluation.md` — candidate compositions C1/C3/C5, test-network requirements TN-1..TN-12, failure-injection scenarios.
- FST Permissionless Test-Network Decision Brief, `docs/fst/architecture/permissionless-test-network-decision-brief.md` — security boundaries, no consensus/Sybil/transaction/finality selection approved.
- AGENTS.md preservation rules — documentation-only change; no C++/CMake/test/CI modification; no destructive Git operations.

Exact source locations inspected (all under `src/network/` and `tests/`):

- `src/network/network_runtime.cpp:16-109` — `NetworkRuntime::initialize` loads identity, generates TLS credentials, opens peer store, builds bootstrap/admission/exchange/dispatcher, starts listening; `run()` is a blocking tick loop; `stop()` graceful shutdown.
- `src/network/network_runtime.hpp:28-42` — `NetworkRuntimeConfig`: `identity_path`, `listen_address`/`listen_port` (0 = dynamic), `bootstrap_entries`, `admission`, `protocol_version`, `capabilities`, `endpoints`, `tick_interval`, `database_path`.
- `src/network/peer_admission.cpp:33-331` — `AdmissionController`: `start_listening`, `connect_to_peer` (async, callback-driven, expected NodeId optional), `reconnect_peer` (store-backed endpoints, backoff), `tick` (idle cleanup + reconnection), connection limits (max 64/32/32/2), duplicate resolution, per-peer `PeerLifecycle`.
- `src/network/secure_session.cpp:210-314` — `SecureSession::async_connect`/`async_accept`: Boost.Asio TCP + TLS 1.3, application handshake with signed `SignedNodeRecord`, TLS certificate hash binding, session nonce binding.
- `src/network/secure_session.cpp:93-164` — `SecureSessionContext::generate_tls_credentials`: self-signed X.509 (Ed25519 key), CN = hex(public key), no CA, cert hash exposed via `tls_cert_hash()`.
- `src/network/peer_store.cpp:135-460` — `PeerStore`: SQLite (WAL, synchronous=FULL), bounded `max_peers` (4096), upsert with monotonic sequence, conflict/stale/full rejection, `mark_connected`/`mark_verified`/`touch`, `cleanup_expired`, `save`/`reload`.
- `src/network/bootstrap.cpp:11-48` — `BootstrapDiscovery`: validates records from multiple independent entries/sources, no network I/O itself, `BootstrapSource` interface for network fetching.
- `src/network/peer_exchange.cpp:63-169` — `PeerExchange::exchange`: rate-limited record exchange, validation, freshness, max records per exchange; `StorePeerAuthenticator` trusts verified peers only.
- `src/network/node_record.cpp:138-194` — `validate_record`: format version, **wrong-network rejection** (line 155, D-036), node_id matches public key, expiry, stale sequence, Ed25519 signature over canonical body.
- `src/network/identity.hpp:60-103` — `KeyProvider` interface; `Ed25519KeyProvider` (in-memory); `NodeId::from_public_key` (fn1 + sha256(pubkey)).
- `src/network/message_dispatch.cpp:44-103` — `MessageDispatcher`: payload size limits (`kMaxPingPayloadBytes`=64, `kMaxPeerRecordBytes`=64KiB, `kMaxCapabilityAdvertiseBytes`=256, `kMaxErrorBytes`=1024), `send`/`dispatch`/`ping`/`disconnect`, `register_handler`.
- `src/network/peer_lifecycle.cpp:32-135` — `PeerLifecycle`: 6 states (Unknown, Healthy, Degraded, Offline, HalfOpen, Disabled), capped exponential backoff 250ms–30s with FNV-1a peer-specific jitter.
- `tests/network_integration_tests.cpp` — three-node discovery chain, store restart/restoration, multiple bootstrap sources, duplicate identity conflict, wrong-network/tampered/expired record rejection.
- `tests/network_tests.cpp` — legacy `NodeServer`/`PeerClient` authenticated TCP coupled to `Ledger`; 3-node Raft cluster with `RunningRaftCluster`, handshake, transaction/block relay, sync, wrong-genesis rejection, inbound worker bounds.
- `tests/test_main.cpp` / `tests/test.hpp` — `FS_TEST`/`FS_REQUIRE` registry, CLI filter via argv[1] or `FEINSTATIC_TEST_FILTER`, ctest timeout 120s (`CMakeLists.txt:138`).
- `scripts/verify-all.sh` — debug/release/asan/clang-release presets, ctest, raft process chaos and membership labs.

## Alternatives evaluated

| Alternative | Advantages | Disadvantages | Classification |
|-------------|------------|---------------|----------------|
| **A. Real local multi-node integration harness** (in-process `NetworkRuntime` instances, loopback, dynamic ports) | Exercises actual TLS 1.3 + application authentication, real `PeerStore`/SQLite, real `AdmissionController` connection limits and lifecycle backoff, real bootstrap/peer exchange; zero production-code changes; each node independent identity/storage/config; fastest to implement and debug; deterministic ports via `listen_port=0`. | Same-process fault isolation weaker than multi-process (a node crash can affect the harness process); message-level fault injection (delay/loss/reorder) not natively supported by Boost.Asio TCP — only disconnect/refuse/terminate and application-level injection; TCP byte ordering preserved so arbitrary byte reordering is not representable (not a real fault). | PROVISIONAL (recommended foundation) |
| **B. In-process deterministic transport simulation** (injectable transport seam, simulated delivery, deterministic delays/partitions/message injection) | Full determinism, cheap partition/latency/fault injection, replayable adversarial scenarios, ideal for CI. | Existing `NetworkRuntime`/`AdmissionController`/`SecureSession` bind directly to Boost.Asio TCP + OpenSSL TLS with no transport abstraction; supporting simulation requires significant production-code changes (transport interface, socket injection) or a test-only fork, violating the "no production changes" constraint and candidate-neutrality; does not exercise real TLS/authentication paths. | OPEN RESEARCH (future optional layer, not foundation) |
| **C. Multi-process integration harness** (independent node processes, real loopback networking, external orchestrator) | Strongest process/storage isolation, exercises process lifecycle and real networking, closest to deployment, a node crash cannot corrupt sibling nodes. | Highest execution cost (process spawn, separate stores/ports), hardest to debug, requires an orchestrator/driver and port coordination, slower test iteration; existing raft chaos labs (`scripts/raft-process-chaos.py`) show the pattern but are coupled to Raft/Ledger. | PROVISIONAL (optional stage 4, after in-process foundation) |

**Recommended:** a layered approach — **Alternative A as the mandatory foundation** (stages 1–2), with **Alternative C as an optional process-level stage** (stage 4) for process-isolation and crash scenarios, and **Alternative B deferred** as OPEN RESEARCH until a transport abstraction is separately approved. This balances fidelity to production code, implementation cost, and fault coverage without inventing functionality or modifying production networking code.

## Reasoning

Alternative A is selected because it requires no changes to `src/network/` and exercises the real authenticated session, admission, discovery, and lifecycle code paths that FST will ship. Every `NetworkRuntime` instance is created with an independent `Ed25519KeyProvider` (identity), an independent `PeerStore` SQLite file (storage), an independent `NetworkRuntimeConfig` (bootstrap entries, listen address/port with `listen_port=0` for dynamic allocation), and an independent `network_identifier` (wrong-network rejection boundary). Nodes connect loopback-to-loopback using endpoints learned through bootstrap or peer exchange, driven through the public `AdmissionController::connect_to_peer` / `reconnect_peer` API and `MessageDispatcher::register_handler`/`send`/`dispatch`.

Alternative B is rejected as the foundation because `SecureSession`, `AdmissionController`, and `NetworkRuntime` are not transport-abstracted (Boost.Asio `tcp::socket`/`ssl::stream` are constructed directly, `secure_session.cpp:265-314`, `peer_admission.cpp:65-72`, `peer_admission.cpp:189`). Injecting simulated delay/loss would require wrapping the transport behind an interface and threading it through `SecureSession` and `AdmissionController` — production changes outside this milestone's documentation-only scope. Simulation is a legitimate future layer once a transport abstraction is approved as a separate ADR.

Alternative C is valuable for process-crash and storage-isolation scenarios but is expensive and complex; it is retained as an optional stage built on the same fixtures as stage 1, reusing the port-reservation pattern already used in `tests/network_tests.cpp:41-46` (`reserve_loopback_port`) and the multi-process pattern in `scripts/raft-process-chaos.py`.

The harness is candidate-neutral: it starts and stops at the network layer (`NetworkRuntime`, `PeerStore`, `BootstrapDiscovery`, `PeerExchange`, `AdmissionController`, `MessageDispatcher`, `SecureSession`). It does not touch `src/consensus/`, `src/ledger/`, transaction formats, block formats, validator selection, Sybil resistance, issuance, rewards, or monetary policy. Legacy Raft tests in `tests/network_tests.cpp` (`RunningRaftCluster`) remain historical/reference and are clearly separated from FST networking tests (D-024 PROVISIONAL, Raft preserved as reference only).

## Selected design

### Architecture overview (layered, candidate-neutral)

```
[Test orchestrator / fixture]
        |
        v
+---------------------+
| NodeFixture (per node) |
|  - Ed25519KeyProvider   (independent identity) |
|  - PeerStore SQLite DB  (independent storage) |
|  - NetworkRuntime       (TLS, discovery, admission, dispatch, lifecycle) |
|  - dynamic listen port  (listen_port=0, loopback) |
|  - network_identifier   (fork/wrong-network boundary) |
+---------------------+
        |  real TCP/TLS + app auth (loopback, dynamic ports)
        v
[NodeFixture A] <==> [NodeFixture B] <==> [NodeFixture C]

[FaultInjector] --controls--> [NodeFixture] (stop/start, close sessions,
                              refuse ports, drop bootstrap, inject malformed
                              / oversized / duplicate app messages)

[Observability] <-- [NodeFixture] (session count, connection state,
                                 lifecycle snapshot, store contents,
                                 dispatch handler invocations)
```

### Component boundaries

| Component | Responsibility | Test-harness use | Existing source |
|-----------|---------------|------------------|-----------------|
| `NodeFixture` (harness, new) | Owns one `NetworkRuntime` + identity + store + config; provides connect/disconnect/restart/observe API. | Instantiates N independent nodes. | New harness code (stage 1) |
| `NetworkRuntime` | Integrates identity, TLS, discovery, admission, dispatch, lifecycle; starts listener; tick loop. | Driven via `initialize`/`run`/`stop`/accessors; each node separate instance. | `src/network/network_runtime.cpp:16` |
| `Ed25519KeyProvider` / `KeyProvider` | Node identity; independent per node; `node_id() = fn1 || hex(sha256(pubkey))`. | One provider per node; guarantees distinct identities. | `src/network/identity.cpp:67-98`, `identity.hpp:82-103` |
| `PeerStore` | SQLite-backed validated peer records; bounded `max_peers`; monotonic sequence; conflict/stale/full rejection. | Independent DB file per node; assert learned records, sources, verification state. | `src/network/peer_store.cpp:135` |
| `BootstrapDiscovery` | Validates records from multiple independent entries/sources; no network I/O; not a trust root. | Per-node bootstrap entries; multiple sources; wrong-network/tampered/expired rejection. | `src/network/bootstrap.cpp:11`, `bootstrap.hpp:29` |
| `PeerExchange` | Rate-limited signed record exchange over authenticated connections; freshness; max records/exchange. | Drive discovery chain (A learns B via bootstrap, B shares C via exchange). | `src/network/peer_exchange.cpp:63` |
| `AdmissionController` | Inbound/outbound admission, connection limits (64/32/32/2), duplicate resolution, reconnection with bounded backoff, `PeerLifecycle` 6-state breaker. | Connect nodes, close sessions, force reconnection, assert counts/limits/backoff. | `src/network/peer_admission.cpp:33` |
| `SecureSession` / `SecureSessionContext` | TLS 1.3 + self-signed cert bound to NodeId via Ed25519 signature over cert hash; app handshake; session nonce. | Real authenticated sessions; cert-hash binding; wrong-key/invalid-signature rejection. | `src/network/secure_session.cpp:210` |
| `MessageDispatcher` | Versioned bounded message dispatch; payload size limits; `register_handler`/`send`/`dispatch`/`ping`/`disconnect`. | Register test handlers; send ping/error; inject oversized/malformed/duplicate app messages. | `src/network/message_dispatch.cpp:44` |
| `PeerLifecycle` | 6-state circuit breaker; capped exponential backoff 250ms–30s with peer-specific jitter. | Assert Degraded/Offline/HalfOpen transitions after failures; recovery after heal. | `src/network/peer_lifecycle.cpp:32` |
| `FaultInjector` (harness, new) | Controls node stop/start, session close, listener stop (refusal), bootstrap removal, app-message injection; deterministic sequencing, no randomness in assertions. | Reproduce partition/heal, refuse, terminate, delay-at-app-layer, duplicate, malformed, oversized. | New harness code (stage 3) |
| `Topology` / `ScenarioRunner` (harness, new) | Declares node set, bootstrap sources, links (connected/partitioned), fault schedule; waits on conditions with timeouts and bounded retries (no flaky sleep loops). | Declarative test scenarios; deterministic reproducibility. | New harness code (stage 2) |
| `HarnessAssertions` / observability (harness, new) | Poll node state (sessions, lifecycle snapshot, store entries, dispatch invocations) until condition or deadline; report failures with node context. | Deterministic assertions; diagnostics on failure. | New harness code (stage 2) |

### Test topology and node lifecycle

**Node lifecycle (per `NodeFixture`):**
1. Construct independent `Ed25519KeyProvider::generate()`, independent `PeerStore` at a unique temp path, independent `NetworkRuntimeConfig` with `listen_address="127.0.0.1"`, `listen_port=0` (dynamic), unique `network_identifier`, configured `bootstrap_entries`.
2. `NetworkRuntime::initialize(config)` — loads identity, generates TLS credentials, opens store, builds components, starts listener; capture `listen_port()`.
3. `NetworkRuntime::run()` on a dedicated thread (or driven by `do_tick` in the test thread for determinism).
4. Bootstrap: `load_bootstrap_records(now)` then `AdmissionController::connect_to_peer` to discovered endpoints (expected NodeId from record).
5. Peer exchange: register a `MessageDispatcher` handler for `kPeerRecordAnnounce`/`kPeerRecordResponse`, call `PeerExchange::exchange` over authenticated sessions to learn further peers.
6. Operate: exchange ping/application messages, observe session count and lifecycle states.
7. Teardown: `NetworkRuntime::stop()`, join thread, close/remove temp DB files, release ports.

**Topology primitives:**
- Independent nodes N≥2, each with distinct identity and store.
- Links: connected, partitioned (no route between groups), one-way (A can reach B, B cannot reach A).
- Bootstrap sources: multiple per node, independent, none privileged; source failure = remove entries/endpoints.
- Wrong-network: separate `network_identifier` for a disjoint group; cross-group records rejected at validation (`node_record.cpp:155`).

### Fault-injection design

Faults are injected through existing public interfaces and process control; no TCP byte reordering is claimed (TCP preserves byte ordering; `node.cpp` timed operations use ordered reads/writes). TCP-level disruption is distinguished from application-message-level injection.

| Fault | Mechanism | Deterministic? | Existing support |
|-------|-----------|----------------|------------------|
| Complete disconnection / partition | Close `AdmissionController` sessions (`PeerSession::close`), stop listener (`AdmissionController::stop`), drop bootstrap entries, remove endpoints. | Yes | `peer_admission.cpp:47`, `secure_session.cpp:316`, `network_runtime.cpp:87` |
| Partition healing | Restart listener, re-connect via `connect_to_peer`/`reconnect_peer`, re-run `load_bootstrap_records`. | Yes | `peer_admission.cpp:161`, `peer_admission.cpp:207`, `network_runtime.cpp:101` |
| Connection refusal | Stop listener / never start listener; connect to a port with no listener. | Yes | `peer_admission.cpp:33`, Boost.Asio connect error |
| Abrupt termination | `NetworkRuntime::stop()` mid-session; close socket; kill process (stage C). | Yes | `network_runtime.cpp:87` |
| Delayed delivery | Application-layer: delay dispatch/handler invocation; do not delay TCP bytes (no reordering). TCP timeouts (`connection_timeout`=10s) bound stalls. | Yes at app layer; TCP delay not injectable without transport seam | `message_dispatch.cpp:59` (handler is sync; test controls call timing) |
| Connection timeouts | `AdmissionController::Config.connection_timeout` (10s), `idle_timeout` (5min), `PeerLifecycle` backoff (250ms–30s). | Yes via configured timeouts + steady clock | `peer_admission.hpp:40-41`, `peer_lifecycle.cpp:56-84` |
| Repeated connection failures / reconnection | `reconnect_peer` + `PeerLifecycle.record_failure` → capped exponential backoff; assert Offline→HalfOpen→Healthy transitions. | Yes | `peer_admission.cpp:207`, `peer_lifecycle.cpp:86-95` |
| Malformed application messages | `MessageDispatcher::send` with invalid payload/type; `receive_message` checksum/type validation; oversized → `message_size` error. | Yes | `message_dispatch.cpp:27-86`, `secure_session.cpp:325-383` |
| Oversized application messages | Payloads exceeding `kMaxPingPayloadBytes`/`kMaxPeerRecordBytes`/etc. → `validate_message` returns size error before send. | Yes | `message_dispatch.cpp:27-42`, `secure_session.hpp:176` |
| Duplicate application messages | Re-send identical payload/type on the same session; observe idempotent handling (no message dedup exists — application owns semantics). | Yes | `message_dispatch.cpp:44-57` (no dedup; harness asserts behaviour) |
| Invalid signatures / wrong-network identities | `SignedNodeRecord` signature verification (`node_record.cpp:187`), network-identifier mismatch (`node_record.cpp:155`), node_id/pubkey mismatch (`node_record.cpp:161`), TLS cert-hash binding (`secure_session.cpp:497-504`). | Yes | `node_record.cpp:138-194`, `secure_session.cpp:486-509` |
| Peer unavailability / bootstrap-source failure | Remove bootstrap entries, drop endpoints, stop the bootstrap peer; assert discovery degrades gracefully, no central authority assumed. | Yes | `bootstrap.cpp:14-28`, `peer_admission.cpp:207` |

**TCP-level vs application-level:** The harness will not describe arbitrary TCP byte reordering, duplication, or corruption as a realistic fault (TCP guarantees byte order; OpenSSL/TLS provides record-layer integrity). Application-message-level injection (malformed, oversized, duplicate, invalid signature) is performed through `MessageDispatcher`/`SecureSession` frame boundaries, which is meaningful and supported by existing validation (`message_dispatch.cpp:27-42`).

**Reproducibility, cleanup, isolation:**
- Deterministic scheduling: faults applied at defined sequence numbers/tick counts; assertions use bounded deadlines with timeout, never unbounded sleeps.
- Each test uses unique temp store paths, unique dynamic ports (`listen_port=0`), unique `network_identifier`.
- Cleanup: `NetworkRuntime::stop()`, join threads, remove temp DB/-wal/-shm files; ports released by process exit or listener close.
- No global test-only administrator or trust authority: each node's identity and store are independent; bootstrap sources are validated by signature and are not trust roots (`bootstrap.hpp:26-28`, adversary model T-07).
- No secrets committed: identities are ephemeral `Ed25519KeyProvider::generate()` in memory; no passphrases or keys in test fixtures.

### Security assumptions

**Properties the harness can verify (network layer only):**
- Independent node identities are distinct and stable (`NodeId` from `Ed25519KeyProvider`, `identity.cpp:92-94`).
- Real authenticated TLS 1.3 sessions with NodeId/cert-hash binding are established between live peers (`secure_session.cpp:210-314`, `secure_session.cpp:486-509`).
- Wrong-network identities and records are rejected (`node_record.cpp:155`, D-036 fork protection).
- Tampered/expired/stale/conflicting node records are rejected (`node_record.cpp:138-194`).
- Connection limits (64 total / 32 inbound / 32 outbound / 2 per peer) are enforced (`peer_admission.hpp:36-43`, `peer_admission.cpp:262-277`).
- Peer lifecycle transitions Degraded→Offline→HalfOpen→Healthy under failures/recovery (`peer_lifecycle.cpp:32-135`).
- Reconnection with bounded backoff after failure (`peer_admission.cpp:207-235`, `peer_lifecycle.cpp:86-95`).
- Peer discovery via bootstrap and peer exchange with multiple independent sources, no privileged source (`bootstrap.cpp`, `peer_exchange.cpp`, adversary model T-07).
- Node restart and store-based reconnection (`peer_store.cpp:100-152`, `network_integration_tests.cpp:123-161`).
- Application-message payload limits and malformed-message rejection (`message_dispatch.cpp:27-42`).

**Properties the harness cannot establish:**
- Byzantine consensus safety or liveness (no consensus implemented; D-017 OPEN RESEARCH).
- Sybil resistance (no validator set, no costly resource; Douceur2002, adversary model T-02).
- Finality, ordering, transaction validity, or monetary policy.
- Network-level DoS resistance beyond the existing bounded admission (connection limits, payload limits, lifecycle breaker).
- Anything requiring a transport simulation seam not present in production code (Alternative B).

**Fault models and limitations:**
- Network control short of total partition; eventual reconnection assumed for recovery (adversary model §4).
- Rational, bounded adversary; not omnipotent; cryptographic primitives assumed unforgeable (D-023 PROVISIONAL review required before primitive reuse).
- TCP preserves byte ordering; no arbitrary byte reordering is modeled.
- Message-level delay/loss injection is application-layer only unless a transport abstraction is added (Alternative B, OPEN RESEARCH).

**Identity and network isolation:**
- Each node has an independent Ed25519 identity (`fn1 || hex(sha256(pubkey))`), independent store, independent config.
- `network_identifier` (32 bytes) is the wrong-network/fork boundary (D-036 APPROVED); records from a different identifier are rejected.
- No cross-node shared state; no central bootstrap trust root.

**Failures and cleanup:**
- Node crashes are simulated by `stop()` + process exit; sibling nodes unaffected in-process (stage A) or by construction (stage C).
- Temp files and ports cleaned up per test; fixtures are RAII.
- Tests time out on bounded deadlines (ctest 120s per `CMakeLists.txt:138`); no hanging on blocked I/O (connection_timeout 10s).

## Test determinism and reproducibility

- No randomness in assertions: identities are random but assertions only require distinctness (`alice.node_id() != bob.node_id()`), never specific values.
- Ports are dynamic (`listen_port=0`) and read back after `initialize`; no hard-coded ports (avoids collisions; existing tests use `reserve_loopback_port`, `network_tests.cpp:41-46`).
- Time is injected via explicit `now_unix_ms` parameters where the API supports it (`PeerStore::upsert(record, source, now)`, `PeerExchange::exchange(..., now)`, `AdmissionController::tick(now)`, `PeerLifecycle` uses a steady `Clock`).
- Fault schedules are declared data structures (sequence/tick → fault), not ad hoc `sleep` calls.
- Assertions poll with a deadline and bounded retry interval; on failure, dump node context (session count, lifecycle snapshot, store entries) before failing.
- Each test is isolated: unique store path, unique network identifier, fresh `NetworkRuntime`, full teardown.
- Re-running a test with the same inputs produces the same pass/fail outcome (modulo real OS scheduling, bounded by timeouts).

## Performance and maintenance trade-offs

| Dimension | Assessment |
|-----------|------------|
| Execution cost | In-process loopback tests are cheap; ctest 120s budget per `CMakeLists.txt:138`. Multi-process stage C is costlier; limit node count and duration. |
| Debuggability | In-process (stage A) allows single-process debugging and stack traces across nodes; multi-process (stage C) requires log correlation. |
| Maintenance | No production-code changes → harness breaks only if public network APIs change; harness tracks `src/network/` headers. |
| CI integration | Add test files under `tests/` and register in `CMakeLists.txt` (separate implementation milestone); run under debug/release/asan presets (`verify-all.sh`). |
| Flakiness risk | Bounded timeouts, dynamic ports, deterministic schedules; avoid `sleep`-based assertions. |

## Implementation locations and incremental delivery plan

**Documentation only in this milestone.** No implementation code is written or modified here.

Locations (new harness code, future milestone):
- `tests/network_harness.hpp` / `tests/network_harness.cpp` — `NodeFixture`, `Topology`, `ScenarioRunner`, `FaultInjector`, observability/assertions.
- `tests/network_harness_tests.cpp` — stage 1–4 tests (see below).
- CMakeLists.txt — add harness test files to `feinstatic_tests` (separate milestone; no CMake change now).
- `docs/fst/` — this ADR and register update.

**Incremental plan (after owner acceptance only):**
1. **Minimal harness foundation and independent node fixtures.** `NodeFixture` with independent identity/store/config/dynamic port; `NetworkRuntime` start/stop; assert listen port acquired; two nodes connect loopback and complete authenticated handshake; assert distinct identities and session count.
2. **Actual multi-node authenticated networking tests.** Bootstrap discovery chain (A→B via bootstrap, B→C via peer exchange); multiple independent bootstrap sources; wrong-network group rejection; restart and store-based reconnection; peer admission limits; lifecycle Degraded/Offline/Healthy transitions.
3. **Deterministic fault injection and recovery tests.** Partition and healing; connection refusal; abrupt termination and reconnection with backoff; repeated connection failures; idle-timeout cleanup; malformed/oversized/duplicate application messages; invalid signatures and wrong-network identities; bootstrap-source failure.
4. **Reproducible adversarial scenarios and CI integration.** Adversarial peer behaviour against existing network boundaries (connection-limit exhaustion, payload-limit enforcement, invalid handshake, cert-hash mismatch); optional multi-process stage C for process-crash/storage isolation; CI registration under `verify-all.sh` presets; determinism validation (repeated runs).

**Mandatory foundation:** stages 1–2 (in-process, real authenticated networking). **Optional advanced simulation:** stage C multi-process; Alternative B transport simulation deferred (OPEN RESEARCH).

## Verification strategy

- Build and run under the existing presets: debug, release, asan (ASan/UBSan), clang-release (-Werror) via `scripts/verify-all.sh` and `ctest --timeout 120` (`CMakeLists.txt:138`).
- Run each harness test repeatedly (e.g., 20–50 iterations) to shake out flakiness; assert deterministic pass/fail.
- AddressSanitizer/ThreadSanitizer builds to catch memory and concurrency issues in the harness's use of `NetworkRuntime` (multi-threaded tick + io thread).
- No fabricated results: report actual pass/fail counts; never claim security properties the harness cannot establish (Section: Security assumptions).
- Cross-reference adversary model threat classes T-01 (malicious node operators), T-06 (network-level attackers), T-07 (malicious bootstrap providers), T-13 (compromised implementations) as the test-planning reference; map each test to the threat class it exercises.

## Explicit non-goals

- Implement or select C1, C3, C5, or any consensus algorithm (D-017 OPEN RESEARCH).
- Implement validator selection, Sybil resistance, or any costly-resource admission (D-016 PROVISIONAL, mechanism unresolved).
- Implement transaction formats, transaction processing, block formats, chain state, or finality (D-037 OPEN RESEARCH).
- Implement issuance, validator rewards, or monetary policy (D-015 OPEN RESEARCH).
- Implement offline financial operation (D-018 PROVISIONAL).
- Turn Raft into FST consensus; legacy Raft tests remain historical/reference only (D-024 PROVISIONAL).
- Introduce a global test-only administrator or trust authority into the protocol.
- Modify `src/network/` or any production C++ source/header/CMake in this milestone.
- Add a new decision to the decision register or change D-015 through D-037.

## Cross-references

- [`decision-register.md`](../decisions/decision-register.md) — Parent decision register; D-015–D-037 classifications preserved.
- [`research-register.md`](../../research/research-register.md) — RS-001 (consensus), RS-002 (Sybil), RS-005 (P2P discovery), RS-013 (consensus–validator), RS-014 (identity/peer discovery implementation).
- [`../security/adversary-model-v0.1.md`](../../security/adversary-model-v0.1.md) — Threat classes T-01..T-013; test-planning reference.
- [`../architecture/permissionless-test-network-evaluation.md`](../../architecture/permissionless-test-network-evaluation.md) — Test-network requirements TN-1..TN-12; failure-injection scenarios.
- [`../architecture/permissionless-test-network-decision-brief.md`](../../architecture/permissionless-test-network-decision-brief.md) — Security boundaries; nothing approved here.
- [`../scope/scope-v0.1.md`](../../scope/scope-v0.1.md) — FST scope; out-of-scope items.
- [`../principles/engineering-principles.md`](../../principles/engineering-principles.md) — P1–P14.

## Unresolved questions / exact owner approval required

1. **Owner approval of ADR-0001 as PROPOSED / OPEN RESEARCH** is required before any implementation milestone begins. No implementation is authorized by this document.
2. Whether stage C (multi-process) is included in the implementation milestone, or deferred, is an owner decision (cost vs. process-isolation value).
3. Whether and when a transport abstraction (Alternative B) is added for deterministic message-level delay/loss injection is OPEN RESEARCH and requires a separate ADR; it is explicitly not part of this milestone's foundation.
4. Confirmation that the harness test files (`tests/network_harness.*`) may be added to `CMakeLists.txt` and `verify-all.sh` in a later milestone (no CMake/CI change here).
5. Naming and location conventions for harness fixtures under `tests/` (proposed: `tests/network_harness.hpp/.cpp`).

## Verification evidence

None yet. This ADR is a proposal; no implementation or test results exist. Verification occurs in the implementation milestone (stages 1–4).
