# Asynchronous TLS I/O Design Plan

**Status:** PROPOSED (design only — not yet implemented)
**Applies to:** `src/network/secure_session.{hpp,cpp}`, `src/network/network_runtime.{hpp,cpp}`, `src/network/peer_session.{hpp,cpp}`, `src/network/message_dispatch.{hpp,cpp}`, `src/network/peer_admission.{hpp,cpp}`
**Related defects:** FST-BUG-0013, FST-BUG-0014, FST-BUG-0015, FST-BUG-0017 (SSL concurrency race + Release-mode harness hang/timeout)
**Research basis:** Boost.Asio strand serialization model; OpenSSL `SSL_stream` is not thread-safe for concurrent read/write; `boost::asio::io_context::strand` guarantees ordered, non-concurrent invocation of handlers bound to it.

---

## Problem

`SecureSession::send_message` (blocking `boost::asio::write`) and
`SecureSession::receive_message` (blocking `boost::asio::read`) operate on the
same `boost::asio::ssl::stream<tcp::socket>`. Currently:

- each session has a dedicated dispatch worker thread that blocks on
  `receive_message` (read);
- `NodeFixture::send` / `NetworkRuntime` callers invoke `send_message`
  (blocking write) from arbitrary threads (test caller thread, admission
  connector thread).

Concurrent blocking read + write on one OpenSSL stream from two threads is the
unresolved SSL concurrency issue. It causes Release-mode `receive_next` timeouts
(FST-BUG-0017) and full-suite hangs (worker/caller block indefinitely on the SSL
stream), and underlies the TSan `socket_ops::close` / `reactive_socket_service_base`
cross-thread races (FST-BUG-0014/0015).

---

## Design goals

1. **No concurrent access to any `ssl_stream_`**: every read, write, shutdown,
   and close on a session's SSL stream is serialized on a single `io_context`
   strand bound to that session.
2. **Non-blocking send**: `send_message` enqueues and returns an error_code
   immediately; it never blocks the caller.
3. **Ordered, bounded write queue**: outgoing frames per session are drained
   FIFO; a full queue applies backpressure (error_code) rather than unbounded
   memory growth.
4. **Safe lifetime**: async operations hold a `shared_ptr` to the session
   (`enable_shared_from_this`) so a session cannot be destroyed while a read or
   write is pending.
5. **Graceful shutdown**: acceptor stops, pending async ops are cancelled, SSL
   streams are shut down on their strands, `io_context` threads are joined, no
   use-after-free or detached threads.
6. **No dedicated thread per session**: reads are async reads driven by the
   `io_context` thread pool; the per-session `std::thread` dispatch worker is
   removed.

---

## Ownership model

| Entity | Owner | Lifetime rule |
|--------|-------|---------------|
| `boost::asio::io_context` | `NetworkRuntime` | Lives for the runtime's lifetime; run on 1..N threads in `NetworkRuntime::run()`. |
| per-session `io_context::strand` | `SecureSession` | One strand per session, created at construction; all session SSL ops bound to it. |
| `SecureSession` | `AdmissionController` (`sessions_` map, `std::shared_ptr<PeerSession>`) | `PeerSession` holds `std::shared_ptr<SecureSession>`; removed from map on close → last `shared_ptr` may be released only after the strand has drained pending ops. |
| async read/write handlers | session itself | Capture `shared_from_this()` (session derives `enable_shared_from_this<SecureSession>`); the strand keeps the handler invocation serialized. |
| write queue entries | session | Owned by the session while queued; moved into the async_write op when drained. |

`SecureSession` derives from `std::enable_shared_from_this<SecureSession>`. It is
always constructed via `std::make_shared<SecureSession>(...)`. The constructor
takes `boost::asio::io_context&`, `SecureSessionContext&`,
`std::shared_ptr<const KeyProvider>`.

`PeerSession` (thin wrapper: session + handshake result + metadata) continues to
be owned by `AdmissionController::sessions_` (`std::map<NodeId,
std::shared_ptr<PeerSession>>`). `AdmissionController::session_closed(node_id)`
erases the entry (decrementing inbound/outbound counters), after which
`PeerSession::~PeerSession` closes the wrapped `SecureSession`.

---

## Strand boundaries

Each `SecureSession` constructs a `boost::asio::io_context::strand`
`strand_{io_.get_executor()}` bound to the io_context executor.

Every operation that touches `ssl_stream_` is dispatched through `strand_`:

- `async_connect` / `async_accept`: resolve, connect, TLS handshake, then
  application handshake — all chained via `boost::asio::post(strand_, ...)` or
  by binding completion handlers to `strand_`.
- `receive_message` → replaced by an **async read loop**: `async_read` header
  (kFrameHeaderBytes), then `async_read` payload (length bytes), validate
  checksum/size, invoke the dispatcher handler + observers, then chain the next
  `async_read` header. All read handlers bind to `strand_`. A read error or
  close ends the loop and posts `session_closed`.
- `send_message` → enqueues to the per-session write queue **on the strand**;
  if the queue was empty, initiates `async_write` of the head bound to `strand_`;
  on write completion, pops the head and writes the next queued item (drain).
- `close(ec)`: posted to the strand; cancels pending async ops
  (`ssl_stream_.next_layer().cancel()`), performs `ssl_stream_.shutdown(ec)`,
  closes the underlying socket, then notifies `AdmissionController::session_closed`.

Callers on any thread (test thread, admission connector, dispatcher handler)
invoke `send_message`, which posts the enqueue/drain to the strand and returns
immediately. No caller ever touches `ssl_stream_` directly. The public
`ssl_stream()`/`stream()` accessors and the blocking `send_message`/`receive_message`
are removed or deprecated; callers use the async/queue API.

Because all of a session's stream operations run on its strand, there is never
concurrent read/write on the same `ssl_stream_` — the SSL concurrency issue is
eliminated by construction. Cross-thread Boost.Asio races (FST-BUG-0014/0015) are
likewise removed: socket construction/close happen on the owning strand.

---

## Write-queue behavior

Per-session write queue (mutated only on the session's strand):

```cpp
struct WriteItem {
    std::uint16_t type;
    Bytes payload;
    boost::asio::steady_timer deadline; // optional per-write timeout
};
std::deque<WriteItem> write_queue_;   // strand-owned
bool write_draining_ = false;         // strand-owned
std::size_t write_queue_max_ = 256;   // bounded
```

`send_message(type, payload, ec)` semantics:

1. If `!session->is_authenticated()` → `ec = not_a_stream`, return 0.
2. Post to `strand_`:
   - if `write_queue_.size() >= write_queue_max_` → `ec = no_buffer_space`, return 0 (backpressure).
   - `write_queue_.push_back({type, payload})`.
   - if `!write_draining_` → `write_draining_ = true`; initiate
     `async_write(ssl_stream_, frame(buffer), strand_-bound completion handler)`.
3. Return 0 with `ec = success` (enqueued; delivery is asynchronous).

Write completion handler (on strand):

- if `ec` → close session (post `session_closed`), stop draining.
- else → `write_queue_.pop_front()`; if `!write_queue_.empty()` → initiate
  `async_write` of new head; else → `write_draining_ = false`.

Ordering: frames for a session are written strictly FIFO, so a `kPong` always
precedes a later `kPeerRecordResponse` on the same session. Backpressure: a
slow reader bounds memory at `write_queue_max_` frames; senders observe
`no_buffer_space` instead of blocking indefinitely.

Frame format is unchanged (4-byte length + 2-byte type + 32-byte checksum +
payload), so the wire protocol is compatible with existing peers.

---

## Graceful shutdown

`NetworkRuntime::stop()`:

1. Set `running_ = false`, `stop_requested_ = true`.
2. `admission_->stop()`: stop the acceptor (no new connections), then for each
   session post `close()` to that session's strand (cancels pending async reads
   and writes, shuts down the SSL stream, closes the socket). This removes each
   session from `sessions_` via `session_closed` (decrementing counters).
3. `io_context.stop()` (or let it drain), then join all `io_context` worker
   threads.
4. Clear `dispatch_workers_` (no longer used — the per-session worker thread is
   removed); `finished_workers_` no longer needed.

Session teardown ordering (use-after-free safety):

- A `SecureSession` is destroyed only after its strand has no pending handlers
  holding `shared_from_this()`. `close()` cancels pending ops first; completion
  handlers that run after cancellation see `ec == operation_aborted` and do not
  re-arm reads/writes or touch the session. The `shared_ptr` held by
  `AdmissionController::sessions_` is released in `session_closed`; any in-flight
  handler's captured `shared_from_this()` keeps the object alive until that
  handler returns.
- `NetworkRuntime::~NetworkRuntime` calls `stop()` before members are destroyed,
  so the io_context and strands are quiesced before `tls_context_`,
  `store_`, `admission_`, etc. are reset.

---

## Removed / changed APIs

- `SecureSession::send_message(...)` blocking → replaced by async enqueue
  (returns `ec` immediately; no blocking write).
- `SecureSession::receive_message(...)` blocking → removed; replaced by internal
  async read loop that delivers frames to the dispatcher handler.
- Per-session `std::thread` dispatch worker (`start_dispatch_worker`,
  `run_dispatch_worker`, `reap_finished_workers`, `dispatch_workers_`,
  `finished_workers_`) → removed; reads driven by `io_context` thread pool via
  strand-bound async reads.
- `MessageDispatcher::dispatch(session, ec)` blocking read → callers use
  `session->async_receive(handler)` or the dispatcher initiates the read loop
  once per session after authentication; the dispatcher no longer blocks.

---

## Verification plan

- Build Debug, Release, ASan/UBSan, Clang Release -Werror: must compile
  warning-free.
- `./build/debug/feinstatic_tests harness_` — 29/29 pass reliably ×5.
- `./build/release/feinstatic_tests harness_` — 29/29 pass reliably ×5 (no
  intermittent timeout, no full-suite hang) — closes FST-BUG-0017 Release-mode
  failures/hang.
- `./build/tsan/feinstatic_tests` — zero `socket_ops::close` and
  `reactive_socket_service_base::construct` race reports — closes FST-BUG-0014/
  FST-BUG-0015.
- No deadlock under repeated `start()`/`stop()` cycles and
  `harness_shutdown_while_sessions_active`.
- Write-queue backpressure: saturate a slow peer, confirm senders observe
  `no_buffer_space` rather than blocking.

---

## Implementation order

1. Rework `SecureSession` to `enable_shared_from_this` + strand; replace
   blocking `send_message`/`receive_message` with async read loop + write queue.
2. Remove per-session dispatch worker thread from `NetworkRuntime`; drive reads
   via `io_context` + strands; `run()` runs the io_context thread pool.
3. Rework `MessageDispatcher` to consume frames delivered by the async read loop
   (callback-based), and to enqueue sends via the session's strand-bound write
   queue.
4. Update `AdmissionController::session_closed` and `PeerSession` destructor for
   strand-safe close and removal.
5. Update `NetworkRuntime::initialize/run/stop` for the new lifecycle
   (io_context run/stop, no worker threads).
6. Build + run the verification plan above.

This plan is a design artifact only. No source changes are made in this session.
