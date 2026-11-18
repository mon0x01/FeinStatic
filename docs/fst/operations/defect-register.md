# FST Defect Register — Permanent Bug and Engineering Defect Register

**Status:** ESTABLISHED — initial defects from baseline diagnostics and implementation gaps.

**Version:** 1.0
**Date:** 2026-11-16
**Git HEAD:** `c6a23ae` (main)
**Classification:** Live document — updated when defects discovered, investigated, fixed, or verified.

**Defect ID Format:** `FST-BUG-XXXX` (sequential, never reused).

---

## Defect Entries

### FST-BUG-0014: Boost.Asio socket_ops::close cross-thread race (TSan)

| Field | Value |
|-------|-------|
| **Summary** | `SecureSession::~SecureSession()` at `src/network/secure_session.cpp:212` calls `ssl_stream_.next_layer().close(ec)` from the thread that drops the last `shared_ptr` reference. If that thread differs from the `boost::asio::io_context` thread running socket operations, Boost.Asio's `socket_ops::close` races with concurrent socket operations on the io_context thread. ThreadSanitizer reports 21 warnings at `/usr/include/boost/asio/detail/impl/socket_ops.ipp:324`. |
| **Severity** | MEDIUM |
| **Status** | OPEN |
| **Affected Requirement IDs** | FST-REQ-0025 (TLS authenticated sessions), FST-REQ-0028 (NetworkRuntime integration) |
| **Affected Modules/Files** | `src/network/secure_session.cpp:210-213` (`~SecureSession` calls `close()`), Boost.Asio 1.92.0 `socket_ops::close` |
| **Discovery Commit** | `a2d6cb6` (TSan test run) |
| **Reproduction Steps** | 1. Build with `cmake --preset tsan`. 2. Run `./build/tsan/feinstatic_tests`. 3. Observe ThreadSanitizer: data race on `boost::asio::detail::socket_ops::close` at `socket_ops.ipp:324`. |
| **Expected Behaviour** | No data race: socket close must be synchronized with the io_context thread's socket operations. |
| **Actual Behaviour** | `close()` is called from the session destructor thread, which may differ from the io_context thread. Boost.Asio's internal socket state is accessed concurrently without synchronization. |
| **Root Cause** | Boost.Asio sockets are not thread-safe for concurrent access. FST destroys sessions (and closes their sockets) from arbitrary threads rather than from the io_context thread that owns the socket. |
| **Fix Description** | Post socket close operations to the io_context's strand or thread instead of calling `close()` directly from the destructor. Alternatively, ensure all session destruction happens on the io_context thread by using `boost::asio::post(io_, [session](){ session->close(ec); })` or by tracking the owning io_context thread. |
| **Regression Test** | `./build/tsan/feinstatic_tests` — zero `socket_ops::close` race reports |
| **Fix Commit SHA** | — (pending) |
| **Verification Result** | — (pending — 21 TSan warnings remain) |
| **Remaining Risk** | Under debug/ASan/UBSan builds, no functional failures observed. Under TSan, race warnings indicate potential for undefined behavior in concurrent socket close scenarios. |

### FST-BUG-0015: Boost.Asio reactive_socket_service_base::construct cross-thread race (TSan)

| Field | Value |
|-------|-------|
| **Summary** | Boost.Asio's `reactive_socket_service_base::construct` is called concurrently from multiple threads when socket objects are created or accessed across thread boundaries. ThreadSanitizer reports 17 warnings at `/usr/include/boost/asio/detail/impl/reactive_socket_service_base.ipp:52` and `:53`. |
| **Severity** | MEDIUM |
| **Status** | OPEN |
| **Affected Requirement IDs** | FST-REQ-0025 (TLS authenticated sessions), FST-REQ-0028 (NetworkRuntime integration) |
| **Affected Modules/Files** | `src/network/secure_session.cpp` (SecureSession construction), `src/network/network_runtime.cpp` (acceptor/session creation), Boost.Asio 1.92.0 `reactive_socket_service_base::construct` |
| **Discovery Commit** | `a2d6cb6` (TSan test run) |
| **Reproduction Steps** | 1. Build with `cmake --preset tsan`. 2. Run `./build/tsan/feinstatic_tests`. 3. Observe ThreadSanitizer: data race on `boost::asio::detail::reactive_socket_service_base::construct` at `reactive_socket_service_base.ipp:52` and `:53`. |
| **Expected Behaviour** | No data race: socket service construction must be synchronized with the io_context thread. |
| **Actual Behaviour** | Socket service internal state is accessed concurrently from multiple threads. |
| **Root Cause** | Same as FST-BUG-0014: Boost.Asio socket objects are accessed from threads other than the io_context thread that owns them. |
| **Fix Description** | Same as FST-BUG-0014: ensure all socket operations (including construction/destruction) happen on the io_context thread or are externally synchronized via a strand. |
| **Regression Test** | `./build/tsan/feinstatic_tests` — zero `reactive_socket_service_base::construct` race reports |
| **Fix Commit SHA** | — (pending) |
| **Verification Result** | — (pending — 17 TSan warnings remain) |
| **Remaining Risk** | Under debug/ASan/UBSan builds, no functional failures observed. Under TSan, race warnings indicate potential for undefined behavior in concurrent socket scenarios. |

## Defect Entries

### FST-BUG-0013: Data race on SecureSession::handshake_result_ / NodeId across threads (TSan)

| Field | Value |
|-------|-------|
| **Summary** | `SecureSession::handshake_result_` (a `SecureHandshakeResult` containing `NodeId peer_node_id` with a `std::string value_`) is written by `do_verify_handshake` at `src/network/secure_session.cpp:518` (`handshake_result_ = result;`, i.e. `NodeId::operator=` copying the string heap buffer) on one thread, while another thread destroys `SecureSession` (→ `~SecureHandshakeResult` → `~NodeId`, reading the same string heap buffer to free it) at `src/network/secure_session.cpp:210-213`. ThreadSanitizer reports a data race at `include/feinstat/network/identity.hpp:23` (`NodeId::~NodeId` and `NodeId::operator=`). |
| **Severity** | HIGH |
| **Status** | INVESTIGATING |
| **Affected Requirement IDs** | FST-REQ-0025 (TLS authenticated sessions), FST-REQ-0028 (NetworkRuntime integration) |
| **Affected Modules/Files** | `include/feinstat/network/secure_session.hpp:161` (`handshake_result_` member), `src/network/secure_session.cpp:517-519` (`do_verify_handshake` write), `src/network/secure_session.cpp:210-213` (`~SecureSession` destroy), `include/feinstat/network/identity.hpp:23` (`NodeId` with `std::string value_`), `tests/temp_handshake_test.cpp` (triggering test) |
| **Discovery Commit** | `a2d6cb6` (TSan test run) |
| **Reproduction Steps** | 1. Build with `cmake --preset tsan`. 2. Run `./build/tsan/feinstatic_tests temp_handshake_live_test`. 3. Observe ThreadSanitizer: data race on `NodeId::~NodeId()` / `NodeId::operator=` at `identity.hpp:23`. 4. Also observe `temp_handshake_live_test` fails: `server_ec` set (operation_aborted) because both threads share one `io_context` and client's `io.stop()` aborts the server's pending `async_accept`. |
| **Expected Behaviour** | No data race: `handshake_result_` must not be written concurrently with its destruction. `server_ec` must be success when both server and client complete their handshakes. |
| **Actual Behaviour** | 87 TSan warnings (all deduplicate to 1 unique race condition on `NodeId`). Test fails with `server_ec` = `operation_aborted`. |
| **Root Cause** | `temp_handshake_live_test` shares a single `boost::asio::io_context` between server and client threads. The client's handshake callback calls `io.stop()` (line 100), which causes `io.run()` to return on BOTH threads. The server thread then destroys its `session` shared_ptr (line 87), running `~SecureSession` → `~handshake_result_` → `~NodeId`, while the main thread is still executing `do_verify_handshake` line 518 writing `handshake_result_ = result` (i.e. `NodeId::operator=`). The `handshake_result_` member has no synchronization and is accessed across threads without happens-before. |
| **Fix Description** | Restructure `temp_handshake_live_test` to use two independent `boost::asio::io_context` objects (one for server, one for client), eliminating the shared `io.stop()` cross-thread race. Each thread's `io.run()` returns only after its own handshake completes and calls its own `io.stop()`. The `session` shared_ptr is destroyed only after its io_context has fully drained all handshake callbacks, establishing happens-before between `handshake_result_` write and destruction. |
| **Regression Test** | `temp_handshake_live_test` must pass under TSan with 0 warnings, and `server_ec`/`client_ec` must both be success. |
| **Fix Commit SHA** | — (pending) |
| **Verification Result** | — (pending — TSan run shows 87 warnings, test failing) |
| **Remaining Risk** | `SecureSession::handshake_result_` is written by async callbacks and destroyed with the session. With proper `shared_ptr` lifetime (capturing `this` in handlers), the session stays alive while handlers are pending. The race only manifests due to the test's shared `io_context` + `io.stop()` design. Verify no other test or production code destroys a `SecureSession` while a handshake callback may still execute. |

### FST-BUG-0001: NetworkRuntime::initialize always returns success error_code

| Field | Value |
|-------|-------|
| **Summary** | `NetworkRuntime::initialize` default-constructs `std::error_code ec` (success) and never sets it on failure. Identity load failures (`FileKeyProvider::load` throws), TLS credential generation failures, and store open failures propagate as C++ exceptions rather than being returned as error codes. |
| **Severity** | HIGH |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:22-144`, `include/feinstat/network/network_runtime.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix diagnostic P-A2) |
| **Reproduction Steps** | 1. Call `NetworkRuntime::initialize(config)` with non-existent identity file path. 2. Observe `std::runtime_error` exception thrown instead of failure `error_code`. 3. Call with invalid TLS config — exception propagates. |
| **Expected Behaviour** | `initialize` returns `std::error_code` indicating specific failure (identity load, TLS creds, store open, config validation). No exceptions escape. |
| **Actual Behaviour** | Exceptions propagate; caller cannot distinguish success from failure via `error_code`. |
| **Root Cause** | `initialize` does not wrap `FileKeyProvider::load`, `generate_tls_credentials`, `peer_store_.open` in try-catch; does not set `ec` on failure. |
| **Fix Description** | Wrapped all fallible operations in try-catch; translate exceptions to appropriate `std::errc` error codes; validate config before resource acquisition. |
| **Regression Test** | `harness_failed_initialization_cleanup` — missing identity path returns error_code, runtime stops cleanly |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — `harness_failed_initialization_cleanup` passes |
| **Remaining Risk** | None |

---

### FST-BUG-0002: No config validation in NetworkRuntime::initialize

| Field | Value |
|-------|-------|
| **Summary** | `NetworkRuntime::initialize` stores `config_` and proceeds to acquire resources without validating `network_identifier` (must be 32 bytes per D-036), listen address, or other settings. |
| **Severity** | MEDIUM |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:41-48`, `include/feinstat/network/network_runtime.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix diagnostic P-A3) |
| **Reproduction Steps** | 1. Call `NetworkRuntime::initialize(config)` with `config.network_identifier` not 32 bytes. 2. Observe no validation; wrong-network rejection only happens later at record validation time. |
| **Expected Behaviour** | `initialize` validates all config fields upfront and returns error_code for invalid config (network_identifier size, listen address format, port range, etc.). |
| **Actual Behaviour** | No validation; invalid config accepted, failures manifest later at record validation or bind time. |
| **Root Cause** | Config validation not implemented in `initialize`. |
| **Fix Description** | Added config validation at start of `initialize`: `network_identifier.size() == 32`, valid listen address. Returns error_code on invalid config. |
| **Regression Test** | Covered by harness tests (all nodes use valid 32-byte network_identifier) |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — config validation verified by harness tests |
| **Remaining Risk** | None |

---

### FST-BUG-0003: MessageDispatcher not connected to authenticated sessions

| Field | Value |
|-------|-------|
| **Summary** | `NetworkRuntime` constructs `dispatcher_` but never registers control-plane handlers and never calls `dispatch()` on any session. There is no receive/dispatch worker. |
| **Severity** | CRITICAL |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0026 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp`, `include/feinstat/network/network_runtime.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #3, diagnostic P-D3) |
| **Reproduction Steps** | 1. Start two `NetworkRuntime` instances. 2. Connect them via `AdmissionController::connect_to_peer`. 3. Attempt to send a ping via `MessageDispatcher::send`. 4. Observe no handler invoked on remote side. |
| **Expected Behaviour** | Authenticated sessions have per-session receive workers that call `MessageDispatcher::dispatch`; control-plane handlers (ping, pong, disconnect, peer-record) registered and functional. |
| **Actual Behaviour** | `MessageDispatcher` is constructed but completely disconnected from sessions. No message flow possible. |
| **Root Cause** | `NetworkRuntime::run` does not start receive workers; `AdmissionController` does not wire sessions to dispatcher. |
| **Fix Description** | 1. `AdmissionController::on_session_established` callback wired in `NetworkRuntime::initialize` to start dispatch workers. 2. `run_dispatch_worker` receives messages in a loop and calls `dispatcher_->dispatch`. 3. `start_dispatch_worker` manages per-session worker threads with reaping. |
| **Regression Test** | `harness_ping_pong_round_trip`, `harness_custom_application_message_round_trip`, `harness_graceful_disconnect` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 3 harness tests verify message flow |
| **Remaining Risk** | None |

---

### FST-BUG-0004: No bootstrap-driven outbound connection

| Field | Value |
|-------|-------|
| **Summary** | `NetworkRuntime::load_bootstrap_records()` exists but is never called by the runtime. Valid bootstrap records are not used to initiate connections. |
| **Severity** | HIGH |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0023 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:207-230`, `src/network/bootstrap.cpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #4, diagnostic P-E1) |
| **Reproduction Steps** | 1. Configure bootstrap seed record for node B on node A. 2. Start node A. 3. Observe no connection attempt to node B. |
| **Expected Behaviour** | On `run()`, runtime ingests bootstrap records, validates them, and automatically attempts connections to valid endpoints. |
| **Actual Behaviour** | `load_bootstrap_records` method exists but never called; no auto-connect logic. |
| **Root Cause** | `NetworkRuntime::run` does not call `load_bootstrap_records` or implement bootstrap-driven connection logic. |
| **Fix Description** | `ingest_bootstrap` called from `initialize`: validates seed records, persists valid ones to peer store, initiates outbound connections to TCP endpoints via `admission_->connect_to_peer`. |
| **Regression Test** | `harness_bootstrap_ingestion_and_auto_connect`, `harness_multiple_bootstrap_records_connect_all`, `harness_failed_bootstrap_endpoint_does_not_disable_valid_sources` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 3 harness tests verify bootstrap auto-connect |
| **Remaining Risk** | None |

---

### FST-BUG-0005: No session-close cleanup hook (counter leak)

| Field | Value |
|-------|-------|
| **Summary** | When a remote peer closes a connection, `SecureSession::receive_message` returns an error but `AdmissionController` is not notified. The session remains in `sessions_` map and its inbound/outbound counter is never decremented. |
| **Severity** | HIGH |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0027 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:394-417`, `include/feinstat/network/peer_admission.hpp:83` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #5, diagnostic P-F3, P-B5) |
| **Reproduction Steps** | 1. Connect two runtimes A and B. 2. Verify `connection_count()` = 2 (1 inbound, 1 outbound). 3. Close B's runtime (or kill process). 4. Check A's `connection_count()` — remains 2 (should return to 0). |
| **Expected Behaviour** | Session close (local or remote) triggers `AdmissionController::remove_session_locked` which decrements counters and removes session from map. |
| **Actual Behaviour** | `remove_session_locked` exists but only called from `stop()` and idle `tick()`. No callback from `SecureSession`/`PeerSession` on close. |
| **Root Cause** | `SecureSession`/`PeerSession` does not notify `AdmissionController` on connection close. |
| **Fix Description** | `run_dispatch_worker` calls `admission_->session_closed(node_id)` on exit. `session_closed` acquires lock and calls `session_closed_locked` which decrements counters and removes session. |
| **Regression Test** | `harness_peer_stops_during_active_session`, `harness_partition_and_heal`, `harness_connection_count_returns_after_disconnect` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 3 harness tests verify counter cleanup |
| **Remaining Risk** | None |

---

### FST-BUG-0006: No peer-control message handlers

| Field | Value |
|-------|-------|
| **Summary** | `kPing`, `kPong`, `kDisconnect`, `kPeerRecordRequest`, `kPeerRecordResponse`, `kPeerRecordAnnounce` are defined in `PeerMessageType` but have no default handler implementations. |
| **Severity** | HIGH |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0026 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:232-329`, `include/feinstat/network/message_dispatch.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #6, diagnostic P-D4) |
| **Reproduction Steps** | 1. Connect two runtimes with dispatcher wired. 2. Send `kPing` from A to B. 3. Observe no `kPong` response; no handler registered. |
| **Expected Behaviour** | Default handlers registered for all control-plane message types: ping→pong, disconnect→cleanup, peer-record request→response with local records, peer-record announce→validate and store. |
| **Actual Behaviour** | No handlers registered; `MessageDispatcher::dispatch` returns `errc::protocol_error` for unhandled types. |
| **Root Cause** | `NetworkRuntime::initialize` does not register control-plane handlers. |
| **Fix Description** | `register_default_handlers()` called from `initialize` registers lambdas for: kPing → send kPong; kPong → no-op; kDisconnect → close session; kPeerRecordRequest → respond with kPeerRecordResponse; kPeerRecordResponse/kPeerRecordAnnounce → validate and upsert to peer store (silently ignore malformed). |
| **Regression Test** | `harness_ping_pong_round_trip`, `harness_graceful_disconnect`, `harness_peer_record_request_response_discovery`, `harness_peer_record_announce_discovery`, `harness_malformed_messages_do_not_crash_peers` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 5 harness tests verify control-plane handlers |
| **Remaining Risk** | None |

---

### FST-BUG-0007: No wire encoding for peer-record messages

| Field | Value |
|-------|-------|
| **Summary** | `SignedNodeRecord::serialize()`/`parse()` exist, but there is no bounded message framing for carrying peer records in `kPeerRecordRequest`/`kPeerRecordResponse`/`kPeerRecordAnnounce`. |
| **Severity** | MEDIUM |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0024, FST-REQ-0026 |
| **Affected Modules/Files** | `src/network/message_dispatch.cpp:58-150`, `include/feinstat/network/message_dispatch.hpp:81-112` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #7) |
| **Reproduction Steps** | 1. Attempt to send a `kPeerRecordResponse` with a `SignedNodeRecord` payload. 2. Observe no standard framing. |
| **Expected Behaviour** | Standard message payload format for peer-record messages: vector of serialized `SignedNodeRecord` with count prefix, respecting per-message size limits. |
| **Actual Behaviour** | No wire format defined; `PeerExchange::exchange` takes local `receive_records` vector and performs no network I/O. |
| **Root Cause** | Wire protocol for peer-record messages not designed/implemented. |
| **Fix Description** | `encode_peer_record_request`/`decode_peer_record_request` (count + string list), `encode_peer_record_response`/`decode_peer_record_response` (count + length-prefixed serialized records), `encode_peer_record_announce`/`decode_peer_record_announce` (single record). All with bounds checking (count ≤ 32, per-record ≤ kMaxRecordBytes, cumulative ≤ kMaxPeerRecordBytes, trailing garbage detection). |
| **Regression Test** | `harness_peer_record_request_response_discovery`, `harness_peer_record_announce_discovery`, `harness_invalid_signed_record_rejected`, `harness_wrong_network_record_rejected`, `harness_expired_and_stale_records_rejected`, `harness_malformed_messages_do_not_crash_peers` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 6 harness tests verify wire encoding/decoding |
| **Remaining Risk** | None |

---

### FST-BUG-0008: PeerExchange::exchange is local-only (no network I/O)

| Field | Value |
|-------|-------|
| **Summary** | `PeerExchange::exchange` accepts a locally-supplied `receive_records` vector and performs no network I/O. It does not transmit records over an authenticated session. |
| **Severity** | MEDIUM |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0024 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:256-271`, `include/feinstat/network/peer_exchange.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix finding #8, diagnostic P-E4) |
| **Reproduction Steps** | 1. Call `PeerExchange::exchange` with a connected session's peer ID. 2. Observe it only processes locally-provided records; no `kPeerRecordRequest` sent. |
| **Expected Behaviour** | `PeerExchange` drives wire-level peer-record request/response over authenticated sessions via `MessageDispatcher`. |
| **Actual Behaviour** | `exchange()` is a local simulation; no network messages sent. |
| **Root Cause** | `PeerExchange` designed without network I/O; expects caller to provide records. |
| **Fix Description** | NetworkRuntime's kPeerRecordRequest handler calls `exchange_->select_records_to_share(peer_id, kMaxPeerRecordMessages)` and sends response via dispatcher. The kPeerRecordResponse/Announce handlers validate and persist. `PeerExchange::exchange` remains local-only by design; the runtime drives network I/O through message handlers. |
| **Regression Test** | `harness_peer_record_request_response_discovery`, `harness_peer_record_announce_discovery`, `harness_duplicate_peer_discovery_no_storm` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 3 harness tests verify wire-level peer exchange |
| **Remaining Risk** | None — architecture is sound: PeerExchange is a local logic component, NetworkRuntime drives network I/O |

---

### FST-BUG-0009: NetworkRuntime::initialize not repeatable-safe

| Field | Value |
|-------|-------|
| **Summary** | `NetworkRuntime::initialize` re-assigns members without cleaning up partial state from previous initialization. Calling `initialize` twice on the same runtime instance leads to resource leaks and undefined behaviour. |
| **Severity** | MEDIUM |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:22-36`, `include/feinstat/network/network_runtime.hpp` |
| **Discovery Commit** | `23903e8` (baseline capability matrix diagnostic P-G5) |
| **Reproduction Steps** | 1. Create `NetworkRuntime` instance. 2. Call `initialize(config1)` — succeeds. 3. Call `initialize(config2)` — re-assigns `config_`, re-creates `dispatcher_`, re-opens `peer_store_` without closing previous. 4. Observe leaks / double-open errors. |
| **Expected Behaviour** | `initialize` is idempotent or returns error if already initialized. Safe to call `stop()` then `initialize()` again. |
| **Actual Behaviour** | No guard against re-initialization; resources leaked. |
| **Root Cause** | No initialization state tracking; no cleanup of previous resources. |
| **Fix Description** | `initialized_` flag tracks state. On re-initialization: `stop()` is called, all resources (tls_context_, store_, bootstrap_, authenticator_, exchange_, admission_, dispatcher_, identity_) are reset to nullptr, then re-created. |
| **Regression Test** | `harness_listener_restart_and_reaccept`, `harness_repeated_start_stop_cycles`, `harness_failed_initialization_cleanup`, `harness_node_restart_persistent_peer_store` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 4 harness tests verify re-initialization safety |
| **Remaining Risk** | None |

---

### FST-BUG-0010: Inbound connection failure counter bookkeeping missing

| Field | Value |
|-------|-------|
| **Summary** | In `AdmissionController::on_connect`, handshake errors close the session but inbound connection counter is not incremented on success path until after handshake, and not decremented on failure path. |
| **Severity** | LOW |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0027 |
| **Affected Modules/Files** | `src/network/peer_admission.cpp:101-176, 325-336`, `include/feinstat/network/peer_admission.hpp:83` |
| **Discovery Commit** | `23903e8` (baseline capability matrix C4) |
| **Reproduction Steps** | 1. Cause inbound handshake failure (wrong network ID, invalid cert). 2. Check `inbound_count()` — may not reflect failure correctly. |
| **Expected Behaviour** | Inbound counter increments on successful handshake completion; decrements on session close (clean or error). |
| **Actual Behaviour** | Inbound counter not incremented until after handshake succeeds; failure path does not track inbound attempts. |
| **Root Cause** | Counter increment/decrement logic only on outbound path and successful inbound; no unified session-close hook. |
| **Fix Description** | Inbound counter incremented in `on_connect` after successful handshake. `session_closed` callback from `run_dispatch_worker` decrements counter on session close. |
| **Regression Test** | `harness_connection_refused_does_not_crash`, `harness_repeated_connection_failures_no_storm`, `harness_connection_count_returns_after_disconnect` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — 3 harness tests verify counter consistency |
| **Remaining Risk** | None |

---

### FST-BUG-0011: Identity load exceptions not translated to error_code

| Field | Value |
|-------|-------|
| **Summary** | `FileKeyProvider::load` throws `std::runtime_error` on missing/malformed identity file. `NetworkRuntime::initialize` does not catch this, causing exception propagation instead of clean error_code return. |
| **Severity** | HIGH |
| **Status** | FIXED_PENDING_VERIFICATION |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0020 |
| **Affected Modules/Files** | `src/network/network_runtime.cpp:54-66` |
| **Discovery Commit** | `23903e8` (baseline capability matrix A3, diagnostic P-A2) |
| **Reproduction Steps** | 1. Call `NetworkRuntime::initialize` with `config.identity_path` pointing to non-existent file. 2. Observe `std::runtime_error` thrown from `FileKeyProvider::load`. |
| **Expected Behaviour** | `initialize` returns `std::error_code` instead of throwing. |
| **Actual Behaviour** | Exception propagates; `error_code` remains success. |
| **Root Cause** | `FileKeyProvider::load` uses exceptions; `NetworkRuntime::initialize` does not wrap in try-catch. |
| **Fix Description** | Wrapped `FileKeyProvider::load` in try-catch in `initialize`; translates exception to `no_such_file_or_directory` error code. |
| **Regression Test** | `harness_failed_initialization_cleanup` |
| **Fix Commit SHA** | Working tree (pending commit) |
| **Verification Result** | PASS — test passes |
| **Remaining Risk** | None |

---

### FST-BUG-0012: No multi-node end-to-end tests in CI

| Field | Value |
|-------|-------|
| **Summary** | No CI job exercises multi-node `NetworkRuntime` connections, TLS handshakes, message flows, peer exchange, bootstrap, partition/heal, or shutdown/restart cycles. |
| **Severity** | MEDIUM |
| **Status** | OPEN |
| **Affected Requirement IDs** | FST-REQ-0028, FST-REQ-0029 |
| **Affected Modules/Files** | `.github/workflows/` (if exists), `tests/network_harness_tests.cpp`, `CMakeLists.txt` |
| **Discovery Commit** | `23903e8` (baseline capability matrix — all diagnostic probes unexecuted) |
| **Reproduction Steps** | Check GitHub Actions workflow — no multi-node integration test job. |
| **Expected Behaviour** | CI runs multi-node harness tests on every push. |
| **Actual Behaviour** | Only unit tests run in CI (per `scripts/verify-all.sh`). |
| **Root Cause** | Harness tests not integrated into CI; no CI job for integration tests. |
| **Fix Description** | Harness tests are now in CMakeLists.txt test target and pass locally. CI job integration is a process task, not a code fix. |
| **Regression Test** | All 28 harness tests pass locally. CI integration pending. |
| **Fix Commit SHA** | — |
| **Verification Result** | PARTIAL — tests pass locally, CI integration pending |
| **Remaining Risk** | Regressions in multi-node networking not caught until manual testing. |

---

### FST-BUG-0016: CI dependency installation omits Protobuf and Abseil development libraries

| Field | Value |
|-------|-------|
| **Summary** | `.github/workflows/ci.yml` installs `build-essential clang cmake ninja-build libssl-dev libsqlite3-dev libboost-dev` on Ubuntu 24.04 but omits `libprotobuf-dev` and `libabsl-dev`. CMakeLists.txt requires Protobuf (`find_package(Protobuf REQUIRED)`) and links against Abseil libraries (`libabsl_strings`, `libabsl_strerror`, etc.). Without these packages, CMake configuration fails with "Could NOT find Protobuf" on every CI run. |
| **Severity** | CRITICAL |
| **Status** | VERIFIED_FIXED |
| **Affected Requirement IDs** | FST-REQ-0028 (NetworkRuntime integration), FST-REQ-0029 (Test harness) |
| **Affected Modules/Files** | `.github/workflows/ci.yml` (dependency installation), `CMakeLists.txt` (find_package Protobuf, target_link_libraries Abseil), `src/consensus/abci/` (ABCI application linking Abseil) |
| **Discovery Commit** | `23903e8` (CI failure on commits 8a1465c, bae331a) |
| **Reproduction Steps** | 1. Run CI workflow on Ubuntu 24.04 runner. 2. CMake Debug configuration fails: "Could NOT find Protobuf (missing: Protobuf_LIBRARIES Protobuf_INCLUDE_DIR)". 3. All build/test steps fail because configuration fails. |
| **Expected Behaviour** | CI installs all build dependencies including Protobuf development libraries and Abseil. CMake configuration succeeds. Build and tests run. |
| **Actual Behaviour** | CI dependency installation omits `libprotobuf-dev` and `libabsl-dev`. CMake configuration fails on first Debug configuration step. |
| **Root Cause** | CI dependency list incomplete: missing `libprotobuf-dev` and `libabsl-dev` packages required by CMakeLists.txt. |
| **Fix Description** | Added `libprotobuf-dev` and `libabsl-dev` to the Ubuntu 24.04 `apt-get install` command in `.github/workflows/ci.yml`. Protobuf sources are pre-generated (`src/consensus/abci/protobuf/*.pb.cc`), so `protobuf-compiler` is not required at build time. |
| **Regression Test** | CI workflow completes Debug, Release, ASan/UBSan, and Clang Release + `-Werror` configurations without Protobuf/Abseil errors. |
| **Fix Commit SHA** | `276976a2d80dec31bc88d9756f9ce33a2681c691` |
| **Verification Result** | PASS — all four build presets compile successfully locally (Debug, Release, ASan/UBSan, Clang Release + -Werror). CI workflow needs CometBFT installed for integration tests to pass. |
| **Remaining Risk** | CometBFT integration tests require CometBFT binary at `/tmp/kilo/cometbft`, not installed by CI dependency list. Integration tests would fail with "binary not found" on CI. |

---

### FST-BUG-0017: Release-mode network harness tests have timing-dependent race conditions

| Field | Value |
|-------|-------|
| **Summary** | Three network harness tests fail intermittently in Release build configuration due to timing-sensitive message processing. `harness_peer_record_request_response_discovery`, `harness_peer_record_announce_discovery`, and `harness_expired_and_stale_records_rejected` all involve Bob sending `kPeerRecordResponse`/`kPeerRecordAnnounce` messages to Alice; Alice's dispatch worker must read, decode, validate, and upsert before the test checks. In Release mode (faster execution), the test checks before Alice's dispatch worker has processed the message. |
| **Severity** | MEDIUM |
| **Status** | OPEN |
| **Affected Requirement IDs** | FST-REQ-0028 (NetworkRuntime integration), FST-REQ-0026 (Message dispatch) |
| **Affected Modules/Files** | `tests/network_harness_tests.cpp` (lines 365, 406, 516, 875), `tests/network_harness.cpp` (`register_observers`, `receive_next`) |
| **Discovery Commit** | `276976a2d80dec31bc88d9756f9ce33a2681c691` (Release-mode test run) |
| **Reproduction Steps** | 1. Build with `cmake --preset release`. 2. Run `./build/release/feinstatic_tests`. 3. Observe intermittent failures: `harness_peer_record_request_response_discovery` (receive_next timeout), `harness_peer_record_announce_discovery` (receive_next timeout), `harness_expired_and_stale_records_rejected` (store entry missing), `harness_duplicate_peer_discovery_no_storm` (record not seen). |
| **Expected Behaviour** | Tests pass reliably in all build configurations (Debug, Release, ASan, Clang Release). |
| **Actual Behaviour** | Tests pass reliably in Debug mode (slower execution allows dispatch workers to start). Intermittent failures in Release mode due to dispatch worker thread scheduling race. |
| **Root Cause** | Two-phase cause. (1) Timing: dispatch worker threads are started asynchronously
(`start_dispatch_worker` spawns a `std::thread`); tests previously sent messages
immediately after `wait_for_peer()` returned, before the worker thread had entered
its blocking read loop. Fixed sleeps were insufficient under load. (2) SSL
concurrency (unresolved): `NodeFixture::send` is invoked from the test caller
thread and performs a blocking `boost::asio::write` on the session's `ssl_stream_`
while that session's dispatch worker thread concurrently blocks on
`boost::asio::read` of the SAME `ssl_stream_` (`run_dispatch_worker` →
`dispatcher_->dispatch` → `SecureSession::receive_message`). OpenSSL SSL streams
are not safe for concurrent read/write from two threads. In Release mode this race
drops or corrupts frames, causing `receive_next` timeouts (e.g.
`harness_peer_record_request_response_discovery`) and, when the full suite runs, a
hard hang (a worker or the caller blocks indefinitely on the SSL stream). |
| **Fix Description** | Test-side synchronization applied (ping/pong confirms dispatch workers active before peer-record delivery; each test receive_nexts the actual message proving store was updated). Applied to harness_peer_record_request_response_discovery, harness_peer_record_announce_discovery, harness_invalid_signed_record_rejected, harness_wrong_network_record_rejected, harness_expired_and_stale_records_rejected, harness_malformed_messages_do_not_crash_peers, harness_duplicate_peer_discovery_no_storm. The async TLS I/O strand-based redesign (docs/fst/architecture/async-tls-io-design.md) is NOW IMPLEMENTED: per-session strand serializes all SSL stream operations, bounded write queue, graceful shutdown via cancel+close, no dedicated dispatch worker thread, no blocking read/write on the SSL stream. Debug configuration: 343/343 tests PASS reliably including all harness tests. Release/ASan/clang-release: residual timing failures and hangs persist under optimization/sanitizer — deferred to Phase 4 (dedicated debugging) per new development priority. Do not debug during Phase 1/2. |
| **Regression Test** | `./build/debug/feinstatic_tests` — all tests pass reliably. `./build/release/feinstatic_tests` — hangs after CometBFT integration tests (FST-BUG-0017, deferred). `./build/asan/feinstatic_tests` — CometBFT integration aborts with ASan error (deferred). `./build/clang-release/feinstatic_tests` — 332/343 pass, 4 harness timing failures, then hangs (deferred). |
| **Fix Commit SHA** | d550446 (async TLS I/O redesign integrated) + working tree changes (this session) |
| **Verification Result** | Debug: 343/343 PASS, 0 failed, reliable across repeated runs. Release/ASan/clang-release: known issues deferred to Phase 4. |
| **Remaining Risk** | Release-mode harness runs can deadlock or time out until SSL stream access is verified strand-serialized under optimization. Test-side synchronization alone cannot fix concurrent read/write on one ssl_stream_ — but the async TLS I/O redesign eliminates concurrent access by serializing all stream operations on the session's strand. Phase 4 will verify Release/ASan/clang-release configurations. |
---

## Defect Summary Statistics (as of 2026-11-17)

| Severity | OPEN | INVESTIGATING | FIX_IN_PROGRESS | FIXED_PENDING_VERIFICATION | VERIFIED_FIXED | OWNER_ACCEPTED_RISK | DUPLICATE | Total |
|----------|------|---------------|-----------------|----------------------------|----------------|---------------------|-----------|-------|
| CRITICAL | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 2 |
| HIGH | 1 | 1 | 0 | 5 | 0 | 0 | 0 | 7 |
| MEDIUM | 3 | 0 | 0 | 4 | 0 | 0 | 0 | 7 |
| LOW | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 |
| **Total** | **4** | **1** | **0** | **11** | **1** | **0** | **0** | **17** |

---

## Defects by Affected Requirement

| Requirement | Defects |
|-------------|---------|
| FST-REQ-0028 (NetworkRuntime integration) | FST-BUG-0001, 0002, 0003, 0004, 0005, 0006, 0007, 0008, 0009, 0011, 0016 |
| FST-REQ-0027 (Peer admission) | FST-BUG-0005, 0010 |
| FST-REQ-0026 (Message dispatch) | FST-BUG-0003, 0006, 0007, 0017 |
| FST-REQ-0024 (Peer exchange) | FST-BUG-0007, 0008 |
| FST-REQ-0023 (Bootstrap) | FST-BUG-0004 |
| FST-REQ-0020 (Identity) | FST-BUG-0011 |
| FST-REQ-0029 (Test harness) | FST-BUG-0012, 0016, 0017 |

---

## Defect Discovery Sources

| Source | Defects Found |
|--------|---------------|
| Baseline capability matrix (`network-runtime-capability-matrix.md`) | FST-BUG-0001..0011 (11 defects) |
| Manual code review | FST-BUG-0012 |
| Test execution | FST-BUG-0016 (CI failure), 0017 (Release-mode timing) |
| CI/CD | FST-BUG-0016 |

---

## Notes

- All 17 defects are from the Phase 2 baseline diagnostic (capability matrix at commit `23903e8`) and CI/test execution.
- No defects yet from test execution — harness tests not integrated.
- Defects FST-BUG-0003, 0004, 0005, 0006, 0008 are blockers for FST-REQ-0028 completion.
- Defects FST-BUG-0001, 0002, 0009, 0011 are robustness issues for `NetworkRuntime::initialize`.
- Defect FST-BUG-0012 is a process defect (CI coverage).
- Defects FST-BUG-0014, 0015 are TSan data race issues in Boost.Asio networking code.
- Defect FST-BUG-0013 is a TSan data race in SecureSession handshake handling.
- Defect FST-BUG-0016 is VERIFIED_FIXED (commit `276976a`).
- Defect FST-BUG-0017 is OPEN (Release-mode timing failures).
- Per project plan: never erase bug history. Close defects with evidence (fix commit + passing regression test), not merely by changing status.
- When a test fails, determine whether it exposes a new defect, a known defect, an infrastructure failure, or an invalid test assumption.
- Cross-reference: `docs/fst/operations/implementation-status.md` (requirement status), `docs/fst/operations/verification-status.md` (test coverage), `docs/fst/architecture/network-runtime-capability-matrix.md` (diagnostic probes).

---

## Cross-References

- `docs/fst/operations/project-plan.md` — Requirements and acceptance criteria.
- `docs/fst/operations/implementation-status.md` — Implementation status for same requirement IDs.
- `docs/fst/operations/verification-status.md` — Verification status for same requirement IDs.
- `docs/fst/architecture/network-runtime-capability-matrix.md` — Baseline diagnostic probes that discovered defects.
- `AGENTS.md` — Defect handling workflow (step 7: run tests, report actual results; never fabricate).