# FST Network Runtime Capability Matrix

**Status:** VERIFIED — post-implementation classification updated from baseline diagnostic.
**Repository:** FeinStatic (FST), commit `1973ff5` (main) including verified working tree.
**Scope:** Candidate-neutral P2P networking foundation only
(`src/network/`, `tests/`). No consensus, transactions, blocks, or monetary
policy.

## Classification legend

| Classification | Meaning |
|---|---|
| IMPLEMENTED AND VERIFIED | Code exists and existing tests exercise it successfully. |
| IMPLEMENTED BUT NOT INTEGRATED | Code exists but is not wired into the runtime / not exercised end-to-end. |
| PARTIALLY IMPLEMENTED | Code exists but has gaps, defects, or is not operational end-to-end. |
| NOT IMPLEMENTED | No code exists for the capability. |
| NOT VERIFIED | Code exists but no test exercises it; result unknown. |

## Capability matrix — verified (post-change)

| # | Capability | Classification | Implementation location | Public API | Existing tests | Diagnostic probe | Observed result | Remaining gap |
|---|---|---|---|---|---|---|---|---|
| A1 | Load independent identity per node | IMPLEMENTED AND VERIFIED | `src/network/identity.cpp` (FileKeyProvider::load), `src/network/network_runtime.cpp` | `FileKeyProvider::load(path)`, `NetworkRuntime::initialize` | `tests/identity_tests.cpp`, `tests/network_harness_tests.cpp` | Instantiate two NetworkRuntime with different identity files | Identity loading works; missing/malformed identity returns failure error_code (not thrown) | None |
| A2 | Two nodes different identity files + independent peer DBs | IMPLEMENTED AND VERIFIED | `src/network/peer_store.cpp` (independent SQLite path), `network_runtime.cpp` | `PeerStore::open(path)` | `tests/peer_store_tests.cpp`, `tests/network_harness_tests.cpp` (two_live_nodes, three_node_topology) | Two runtimes with distinct database_path | End-to-end exercised: two runtimes connect with independent identities and peer DBs | None |
| A3 | Init fails cleanly for missing/malformed/invalid identity | IMPLEMENTED AND VERIFIED | `src/network/network_runtime.cpp` | `NetworkRuntime::initialize` | `tests/network_harness_tests.cpp` (harness_failed_initialization_cleanup) | Missing identity path → initialize must return failure error_code | initialize returns failure error_code; no exception propagates; runtime stops cleanly | None |
| A4 | Config rejects invalid network identifiers / unusable settings | IMPLEMENTED AND VERIFIED | `src/network/network_runtime.cpp` | `NetworkRuntimeConfig` | `tests/network_harness_tests.cpp` | initialize with non-32-byte network_identifier | Config validation rejects invalid network_identifier upfront | None |
| A5 | Temp identity files protected with appropriate permissions | NOT VERIFIED | `src/network/node.cpp` (save_endpoint_registry_atomic uses 0600), `identity.cpp` (NodeIdentity save) | `FileKeyProvider::save_new` | None | Check file permissions on saved identity | NodeIdentity persistence uses mode-0600 pattern; network runtime file permissions not confirmed | Verify and document permissions for identity/peer-store files |
| B1 | Bind a loopback listener | IMPLEMENTED AND VERIFIED | `src/network/peer_admission.cpp` | `AdmissionController::start_listening` | `tests/peer_admission_tests.cpp`, `tests/network_harness_tests.cpp` | start_listening("127.0.0.1", port) | Works end-to-end through NetworkRuntime; listener binds and accepts | None |
| B2 | listen_port=0 produces dynamic port | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (`listen_port_ = acceptor_->local_endpoint().port()`) | `AdmissionController::listen_port()` / `NetworkRuntime::listen_port()` | `tests/peer_admission_tests.cpp`, `tests/network_harness_tests.cpp` | start_listening(port=0), read listen_port() | Dynamic port allocated; read back after bind | None |
| B3 | Two NetworkRuntime instances connect | IMPLEMENTED AND VERIFIED | `src/network/peer_admission.cpp` (connect_to_peer), `secure_session.cpp` | `AdmissionController::connect_to_peer` | `tests/network_harness_tests.cpp` (harness_two_live_nodes, harness_three_node_topology) | Two runtimes, A connects to B's listen_port | End-to-end TLS+handshake between two live runtimes; sessions registered both sides | None |
| B4 | Inbound side registers authenticated peer | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (on_connect) | `AdmissionController::session(node_id)` | `tests/network_harness_tests.cpp` | Accept connection, query session() | Session registered under peer_node_id on inbound accept | Session removal on remote close handled via session_closed hook |
| B5 | Outbound/inbound connection counters consistent | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` | `connection_count()`, `inbound_count()`, `outbound_count()` | `tests/network_harness_tests.cpp` (harness_connection_count_returns_after_disconnect) | Connect, disconnect, check counts return to expected | Counters increment on connect; decrement on session close via session_closed callback | None |
| B6 | Duplicate connections behave correctly | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (resolve_duplicate) | `connect_to_peer` with expected NodeId | `tests/network_harness_tests.cpp` (harness_duplicate_peer_discovery_no_storm) | Second connection from same peer | resolve_duplicate closes new session, keeps existing; no peer discovery storm | None |
| C1 | Two live nodes establish actual TLS 1.3 sessions | IMPLEMENTED AND VERIFIED | `src/network/secure_session.cpp` (TLS 1.3 tlsv13), `peer_admission.cpp` | `SecureSession::async_connect`/`async_accept` | `tests/network_harness_tests.cpp` | Two runtimes handshake over loopback; assert is_authenticated() | TLS 1.3 + app handshake works end-to-end between runtimes | None |
| C2 | App handshake binds NodeId to TLS cert | IMPLEMENTED AND VERIFIED | `secure_session.cpp` (do_verify_handshake, do_send_handshake) | `SecureHandshakeResult.tls_cert_hash`, binding signature | `tests/secure_session_tests.cpp`, `tests/network_harness_tests.cpp` | Handshake, compare live cert hash vs presented | Binding signature over cert hash verified; live_tls_cert_hash compared | None |
| C3 | Invalid signatures / wrong-network / NodeId-pubkey mismatch rejected | IMPLEMENTED AND VERIFIED | `secure_session.cpp`, `node_record.cpp` | `validate_record`, `do_verify_handshake` | `tests/network_harness_tests.cpp` (harness_invalid_signed_record_rejected, harness_wrong_network_record_rejected) | Handshake with mismatched cert hash; wrong-network record | Rejected at do_verify_handshake; record validation rejects wrong network at wire level | None |
| C4 | Failed auth cleans up resources and counters | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (on_connect handshake error → close) | `AdmissionController` | `tests/network_harness_tests.cpp` (harness_connection_refused_does_not_crash, harness_repeated_connection_failures_no_storm) | Failed handshake, check counters | Failed auth cleans up resources; counters consistent; no counter leak on repeated failures | None |
| D1 | MessageDispatcher invoked by NetworkRuntime | IMPLEMENTED AND VERIFIED | `message_dispatch.cpp`, `network_runtime.cpp` | `NetworkRuntime::dispatcher()` | `tests/network_harness_tests.cpp` | Call dispatcher after session established | Dispatcher connected to sessions via per-session receive/dispatch worker | None |
| D2 | Handlers registered | IMPLEMENTED AND VERIFIED | `message_dispatch.cpp`, `network_runtime.cpp` | `MessageDispatcher::register_handler` | `tests/message_dispatch_tests.cpp`, `tests/network_harness_tests.cpp` | register_handler(kPing, fn) invoked at runtime | Control-plane handlers registered and invoked end-to-end | None |
| D3 | Send app message, peer processes it | IMPLEMENTED AND VERIFIED | `message_dispatch.cpp`, `network_runtime.cpp` | `MessageDispatcher::send`/`dispatch` | `tests/network_harness_tests.cpp` (harness_custom_application_message_round_trip) | A sends message, B receives | Live message flow between runtimes; app message round trip | None |
| D4 | Ping/pong and disconnect end-to-end | IMPLEMENTED AND VERIFIED | `message_dispatch.cpp`, `network_runtime.cpp` | `dispatcher.ping`/`disconnect`, kPing/kPong/kDisconnect | `tests/network_harness_tests.cpp` (harness_ping_pong_round_trip, harness_graceful_disconnect) | Ping round trip; disconnect closes session | Default handlers: ping→pong, disconnect→cleanup session | None |
| D5 | Connection closes during blocking receive | IMPLEMENTED AND VERIFIED | `secure_session.cpp` (receive_message), `network_runtime.cpp` | `receive_message` | `tests/network_harness_tests.cpp` (harness_shutdown_while_sessions_active, harness_peer_stops_during_active_session) | Close peer while worker blocked | Socket closed on shutdown unblocks receive; worker exits cleanly | None |
| D6 | Multiple peers concurrent, no races / shutdown hangs | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (sessions_mutex_), `secure_session.cpp`, `network_runtime.cpp` | `AdmissionController::sessions()` | `tests/network_harness_tests.cpp` (three_node_topology, harness_repeated_start_stop_cycles) | N concurrent sessions, stop runtime | Mutex-protected session map; per-session SSL streams; worker threads joined on shutdown; no hang | None |
| E1 | Ingest configured bootstrap seed records | IMPLEMENTED AND VERIFIED | `network_runtime.cpp`, `bootstrap.cpp` | `NetworkRuntime::load_bootstrap_records(now)` | `tests/network_harness_tests.cpp` (harness_bootstrap_ingestion_and_auto_connect) | Configure bootstrap seed record; start A; assert ingestion | Bootstrap records ingested, validated, persisted; auto-connect triggered | None |
| E2 | Automatically attempt connections to discovered endpoints | IMPLEMENTED AND VERIFIED | `network_runtime.cpp` | — | `tests/network_harness_tests.cpp` (harness_bootstrap_ingestion_and_auto_connect, harness_multiple_bootstrap_records_connect_all) | Node with bootstrap record connects on start | Automatic outbound connection to valid bootstrap endpoints; invalid endpoints skipped | None |
| E3 | Peer exchange transmits signed records over network | IMPLEMENTED AND VERIFIED | `peer_exchange.cpp`, `network_runtime.cpp` (wire handlers) | `PeerExchange::exchange` | `tests/network_harness_tests.cpp` (harness_peer_record_request_response_discovery, harness_peer_record_announce_discovery) | exchange() over live session | Wire-level kPeerRecordRequest/Response/Announce transmit signed records | None |
| E4 | Discover second peer through authenticated peer | IMPLEMENTED AND VERIFIED | `network_runtime.cpp` (peer-record handlers + peer store) | — | `tests/network_harness_tests.cpp` (harness_peer_record_announce_discovery, harness_duplicate_peer_discovery_no_storm) | A learns C's record from B over live session | A persists C's record from B and connects to C | None |
| E5 | Records validated before persistence/connection | IMPLEMENTED AND VERIFIED | `node_record.cpp` (validate_record), `bootstrap.cpp`, `network_runtime.cpp` | `validate_record` | `tests/network_harness_tests.cpp` (harness_invalid_signed_record_rejected, harness_wrong_network_record_rejected, harness_expired_and_stale_records_rejected) | Invalid record rejected before persistence | Validation at all ingestion paths (network ID, sig, expiry, sequence, freshness); malformed records do not crash peers | None |
| F1 | Stop and recreate a connection | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (stop, reconnect_peer), `network_runtime.cpp` | `AdmissionController::stop`/`connect_to_peer` | `tests/network_harness_tests.cpp` (harness_listener_restart_and_reaccept, harness_reconnection_after_endpoint_recovery) | Close session, reconnect | stop() closes sessions; reconnect_peer uses stored endpoints + backoff | None |
| F2 | Failed connections retried with bounded backoff | IMPLEMENTED AND VERIFIED | `peer_lifecycle.cpp` (capped exp backoff 250ms–30s), `peer_admission.cpp` | `tick(now)`, `PeerLifecycle` | `tests/network_harness_tests.cpp` (harness_repeated_connection_failures_no_storm) | Failure → Offline → HalfOpen → Healthy | Bounded exponential backoff enforced; no reconnection storm | None |
| F3 | Closed sessions removed from admission bookkeeping | IMPLEMENTED AND VERIFIED | `network_runtime.cpp` (session_closed callback), `peer_admission.cpp` | `session_closed_locked` | `tests/network_harness_tests.cpp` (harness_peer_stops_during_active_session, harness_partition_and_heal) | Remote disconnect, check session() returns null | Session close callback removes from sessions_ and decrements counters | None |
| F4 | Peer store retains valid records across restart | IMPLEMENTED AND VERIFIED | `peer_store.cpp` (SQLite WAL, save/load_all) | `PeerStore::open`/`save`/`reload` | `tests/network_harness_tests.cpp` (harness_node_restart_persistent_peer_store) | Restart store, verify records | Persisted across runtime restart via SQLite WAL | None |
| F5 | Recover after temporary disconnection | IMPLEMENTED AND VERIFIED | `peer_lifecycle.cpp`, `peer_admission.cpp` | `tick`/`reconnect_peer` | `tests/network_harness_tests.cpp` (harness_partition_and_heal) | Stop peer, restart, reconnect | Partition/heal tested end-to-end; reconnection after endpoint recovery | None |
| G1 | Stopping closes listeners and all sessions | IMPLEMENTED AND VERIFIED | `peer_admission.cpp` (stop), `network_runtime.cpp` | `AdmissionController::stop`, `NetworkRuntime::stop` | `tests/network_harness_tests.cpp` (harness_shutdown_while_sessions_active) | stop(), check listener closed | Stop closes acceptor, all sessions, stops io_context | None |
| G2 | Dispatch workers joined | IMPLEMENTED AND VERIFIED | `network_runtime.cpp` | — | `tests/network_harness_tests.cpp` (harness_repeated_start_stop_cycles) | stop() returns, no worker still running | Dispatch worker threads joined on shutdown; no leak/hang | None |
| G3 | SQLite handles and temp files cleaned up | IMPLEMENTED AND VERIFIED | `peer_store.cpp` (destructor closes db), `tests/network_harness_tests.cpp` | `PeerStore::~PeerStore` | `tests/network_harness_tests.cpp` | Remove store files after close | SQLite handle closed on destruction; temp files removed by harness | None |
| G4 | Connection counts and worker counts bounded | IMPLEMENTED AND VERIFIED | `peer_admission.hpp` (max 64/32/32/2), `network_runtime.cpp` | `can_accept_inbound`/`can_initiate_outbound` | `tests/network_harness_tests.cpp` | Exceed limits, check rejection | Connection limits enforced at admission; one worker per session; sessions bounded | None |
| G5 | Repeated init/start/stop without hang or leak | IMPLEMENTED AND VERIFIED | `network_runtime.cpp` (initialized_ flag, repeatable-safe stop+recreate), `peer_admission.cpp` | `initialize`/`run`/`stop` | `tests/network_harness_tests.cpp` (harness_repeated_start_stop_cycles, harness_failed_initialization_cleanup) | Multiple initialize→run→stop→initialize cycles | Repeatable-safe initialization; no leaks or hangs across cycles | None |

## Key verified findings

1. **`NetworkRuntime::initialize` returns failure error_code on all fallible operations.** Identity load failures, TLS credential generation failures, store open failures, and invalid config are translated to error codes — no exceptions escape. Verified by `harness_failed_initialization_cleanup`.

2. **Config validation.** `network_identifier` (32 bytes per D-036) and listen address validated upfront before resource acquisition.

3. **MessageDispatcher wired to sessions.** Per-session receive/dispatch workers started on session established; control-plane handlers (ping/pong/disconnect/peer-record) registered and functional.

4. **Bootstrap-driven outbound connection.** `load_bootstrap_records` called from initialize; valid records persisted and auto-connect initiated.

5. **Session-close cleanup hook.** `run_dispatch_worker` calls `admission_->session_closed(node_id)` on worker exit; counters decremented and session removed.

6. **Peer-control message handlers.** Default handlers for kPing/kPong/kDisconnect/kPeerRecordRequest/kPeerRecordResponse/kPeerRecordAnnounce implemented and verified end-to-end.

7. **Wire encoding for peer-record messages.** `encode_peer_record_request/response/announce` and `decode_peer_record_request/response/announce` with bounds checking (count ≤ 32, per-record bounds, trailing garbage detection).

8. **Repeatable-safe initialization.** `initialized_` flag; re-initialization calls stop() and resets all resources before re-creation.

## Diagnostic experiments executed (all pass)

| Probe | Description | Result |
|---|---|---|
| P-A1 | Load identity from file for node A and node B; assert `node_id()` differs | PASS |
| P-A2 | Missing identity path → `initialize` returns failure error_code, not throw | PASS |
| P-A3 | Non-32-byte `network_identifier` → `initialize` rejects | PASS |
| P-B1 | `listen_port=0` → `listen_port()` non-zero; two nodes bind distinct ports | PASS |
| P-B3 | A connects to B's endpoint → authenticated session both sides | PASS |
| P-B5 | Connect then close; `connection_count()` returns to pre-connect value | PASS |
| P-C1 | Complete TLS 1.3 + app handshake between A and B; `is_authenticated()` | PASS |
| P-D3 | Register ping handler on B; A sends ping; B's handler invoked | PASS |
| P-E1 | Bootstrap seed record for B on A; start A; assert connection attempt | PASS |
| P-E4 | A connected to B; B shares C's record; A persists C and connects | PASS |
| P-F3 | Remote closes session; session removed from admission | PASS |
| P-G5 | initialize → run → stop → initialize again on same runtime | PASS |

## Remaining gaps

1. **A5 — Identity file permissions not confirmed.** NodeIdentity persistence uses mode-0600 pattern; network runtime file permissions not explicitly verified.

2. **Multi-node stress (100+ nodes).** Current harness tests cover 2-3 node topologies; large-scale stress not exercised.

3. **ASan/UBSan verification for harness tests.** Harness tests verified in debug config only; not yet run under AddressSanitizer/UBSan.

4. **TSan race detection.** ThreadSanitizer not configured in CMakePresets.json; concurrent access races not detected.
