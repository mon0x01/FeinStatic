#include "feinstat/network/peer_admission.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/node_record.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace feinstat::network {

AdmissionController::AdmissionController(
    PeerStore& store,
    BootstrapDiscovery& bootstrap,
    PeerAuthenticator& authenticator,
    PeerExchange& exchange,
    SecureSessionContext& tls_context,
    std::shared_ptr<const KeyProvider> identity,
    Config config)
    : store_(store), bootstrap_(bootstrap), authenticator_(authenticator),
      exchange_(exchange), tls_context_(tls_context),
      identity_(std::move(identity)), config_(std::move(config)),
      work_guard_(boost::asio::make_work_guard(io_)) {
    // bootstrap_ and exchange_ are stored for future use;
    // they are intentionally not referenced in the current implementation.
    (void)bootstrap_;
    (void)exchange_;
}

AdmissionController::~AdmissionController() {
    stop();
}

void AdmissionController::start_listening(const std::string& address, std::uint16_t port) {
    if (running_) return;
    running_ = true;
    acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(io_);
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::make_address(address), port);
    acceptor_->open(ep.protocol());
    acceptor_->set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_->bind(ep);
    acceptor_->listen();
    listen_port_ = acceptor_->local_endpoint().port();
    do_accept();
    io_thread_ = std::thread([this] { io_.run(); });
}

void AdmissionController::stop() noexcept {
    if (!running_) return;
    running_ = false;

    // Stop the io_context and join its thread before touching any socket.
    // Once the io thread has exited there are no concurrent socket operations,
    // so closing sockets from this thread is race-free (FST-BUG-0014 /
    // FST-BUG-0015: socket_ops::close / reactive_socket_service_base accessed
    // from the caller thread while the io thread is still using the socket).
    // The sockets are closed explicitly so peers observe EOF and remove the
    // session from their admission tables -- shutdown does not hang.
    io_.stop();
    if (io_thread_.joinable()) io_thread_.join();

    boost::system::error_code ec;
    if (acceptor_) acceptor_->close(ec);

    // Snapshot the sessions under the lock, then close each transport socket
    // now that the io thread is stopped (no concurrent socket ops remain).
    std::vector<std::shared_ptr<PeerSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions.reserve(sessions_.size());
        for (auto& [id, entry] : sessions_) {
            sessions.push_back(entry.session);
        }
        sessions_.clear();
        inbound_count_.store(0, std::memory_order_release);
        outbound_count_.store(0, std::memory_order_release);
    }
    for (auto& s : sessions) {
        boost::system::error_code close_ec;
        s->secure_session().socket().close(close_ec);
    }
}

void AdmissionController::do_accept() {
    if (!running_) return;
    auto session = std::make_shared<SecureSession>(io_, tls_context_, identity_);
    acceptor_->async_accept(session->socket(),
        [this, session](const boost::system::error_code& ec) {
            if (!running_) return;
            do_accept();
            if (ec) return;
            on_accept(session, ec);
        });
}

void AdmissionController::on_accept(std::shared_ptr<SecureSession> session,
                                     const boost::system::error_code& ec) {
    if (ec) return;

    if (!can_accept_inbound()) {
        boost::system::error_code close_ec;
        session->close(close_ec);
        return;
    }

    session->async_accept(
        [this, session](const boost::system::error_code& handshake_ec,
                         SecureHandshakeResult result) {
            if (handshake_ec) {
                boost::system::error_code close_ec;
                session->close(close_ec);
                return;
            }
            on_connect(session, result.peer_node_id, "",
                         [](const boost::system::error_code&, std::shared_ptr<PeerSession>) {},
                         handshake_ec, result);
        },
        std::nullopt);
}

void AdmissionController::on_connect(std::shared_ptr<SecureSession> session,
                                      const NodeId& node_id,
                                      const std::string& host,
                                      std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback,
                                      const boost::system::error_code& ec,
                                      SecureHandshakeResult result) {
    if (ec) {
        callback(ec, nullptr);
        return;
    }

    // Use the verified peer NodeId from the handshake result for all
    // post-handshake operations. The pre-handshake node_id is only used for
    // admission-control bookkeeping before the handshake.
    // Copy (not reference) the verified peer NodeId. `result` is later moved
    // into the PeerSession below, which would leave a reference alias empty.
    const NodeId peer_id = result.peer_node_id;
    const std::uint64_t now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Release the pre-handshake tracking entry if it was a placeholder.
    if (node_id != peer_id) {
        sessions_.erase(node_id);
    }

    auto existing = sessions_.find(peer_id);
    if (existing != sessions_.end()) {
        // An outbound attempt that resolves to an existing session never
        // becomes an established connection, so release the slot reserved in
        // connect_to_peer. Inbound attempts hold no reservation.
        if (!host.empty()) --outbound_count_;
        resolve_duplicate(peer_id, std::make_shared<PeerSession>(
            std::move(session), std::move(result), now));
        callback(boost::system::error_code{}, existing->second.session);
        return;
    }

    if (sessions_.size() >= config_.max_connections) {
        boost::system::error_code close_ec;
        session->close(close_ec);
        callback(boost::system::errc::make_error_code(boost::system::errc::not_a_stream), nullptr);
        return;
    }

    auto entry = store_.get(peer_id);
    if (!entry) {
        store_.upsert(result.peer_record, DiscoverySource::PeerExchange, now);
    }

    authenticator_.register_authenticated_peer(peer_id, result.peer_public_key, now);
    store_.mark_verified(peer_id, now);
    store_.touch(peer_id, now);

    auto peer_session = std::make_shared<PeerSession>(std::move(session),
                                                       std::move(result), now);
    bool outbound = !host.empty();
    sessions_.emplace(peer_id, ConnectionEntry{peer_session, outbound, now});
    // Outbound slots are reserved (counted) in connect_to_peer before the
    // connection attempt is issued; only inbound connections increment their
    // counter here. Incrementing outbound_count_ again would double-count.
    if (!outbound) ++inbound_count_;

    lifecycles_.emplace(peer_id, PeerLifecycle(peer_id.value(), config_.lifecycle));

    // Notify the runtime that a new authenticated session is admitted so a
    // dispatch worker can be started for it. Invoked while holding
    // sessions_mutex_, matching the documented contract.
    if (on_session_established) {
        on_session_established(peer_session);
    }

    callback(boost::system::error_code{}, peer_session);
}

void AdmissionController::connect_to_peer(
    std::optional<NodeId> expected_peer_node_id,
    const std::string& host,
    std::uint16_t port,
    std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback) {
    NodeId tracking_id = expected_peer_node_id.value_or(NodeId{});

    // Check admission limits under the lock, then release before async I/O.
    // Backoff/rate-limiting is an automatic-reconnect policy enforced by
    // reconnect_peer(); explicit connect_to_peer() calls always attempt.
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (!can_initiate_outbound(tracking_id)) {
            callback(boost::system::errc::make_error_code(boost::system::errc::not_a_stream), nullptr);
            return;
        }

        // Reserve outbound capacity.
        ++outbound_count_;
    }

    auto session = std::make_shared<SecureSession>(io_, tls_context_, identity_);

    session->async_connect(
        host, port,
        [this, session, tracking_id, host, port, callback = std::move(callback),
         expected_peer_node_id](const boost::system::error_code& ec, SecureHandshakeResult result) mutable {
            (void)port;  // port is passed to async_connect above
            if (ec) {
                // Decrement outbound count on failure since the connection
                // never succeeded.
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                --outbound_count_;
            }
            on_connect(session, tracking_id, host, std::move(callback), ec, result);
        },
        expected_peer_node_id);
}

void AdmissionController::reconnect_peer(
    const NodeId& node_id,
    std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback) {
    // Automatic reconnects are rate-limited by the peer lifecycle backoff.
    // Explicit connect_to_peer() calls are not gated here.
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto lifecycle_it = lifecycles_.find(node_id);
        if (lifecycle_it != lifecycles_.end()) {
            const auto now = std::chrono::steady_clock::now();
            if (!lifecycle_it->second.can_attempt(now)) {
                callback(boost::system::errc::make_error_code(boost::system::errc::timed_out),
                         nullptr);
                return;
            }
        }
    }

    auto entry = store_.get(node_id);
    if (!entry) {
        callback(boost::system::errc::make_error_code(boost::system::errc::no_such_process), nullptr);
        return;
    }

    for (const auto& ep : entry->record.endpoints) {
        if (ep.transport != TransportType::Tcp) continue;
        connect_to_peer(std::optional<NodeId>(node_id), ep.host, ep.port,
            [this, callback = std::move(callback), node_id](const boost::system::error_code& ec,
                                                             std::shared_ptr<PeerSession> sess) {
                if (ec) {
                    std::unique_lock<std::mutex> lock(sessions_mutex_);
                    auto lifecycle_it = lifecycles_.find(node_id);
                    if (lifecycle_it != lifecycles_.end()) {
                        auto now = std::chrono::steady_clock::now();
                        (void)lifecycle_it->second.record_failure(now, 0);
                    }
                }
                callback(ec, sess);
            });
        return;
    }

    callback(boost::system::errc::make_error_code(boost::system::errc::no_such_process), nullptr);
}

std::shared_ptr<PeerSession> AdmissionController::session(const NodeId& node_id) const noexcept {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(node_id);
    return it != sessions_.end() ? it->second.session : nullptr;
}

std::vector<std::shared_ptr<PeerSession>> AdmissionController::sessions() const noexcept {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::shared_ptr<PeerSession>> result;
    result.reserve(sessions_.size());
    for (const auto& [id, entry] : sessions_) {
        (void)id;
        result.push_back(entry.session);
    }
    return result;
}

std::size_t AdmissionController::connection_count() const noexcept {
    return inbound_count_ + outbound_count_;
}

std::size_t AdmissionController::inbound_count() const noexcept { return inbound_count_; }

std::size_t AdmissionController::outbound_count() const noexcept { return outbound_count_; }

bool AdmissionController::can_accept_inbound() const noexcept {
    return (inbound_count_ + outbound_count_) < config_.max_connections &&
           inbound_count_ < config_.max_inbound_connections;
}

bool AdmissionController::can_initiate_outbound(const NodeId& node_id) const noexcept {
    if ((inbound_count_ + outbound_count_) >= config_.max_connections) return false;
    if (outbound_count_ >= config_.max_outbound_connections) return false;

    std::size_t count = 0;
    for (const auto& [id, entry] : sessions_) {
        (void)id;
        if (entry.session->peer_node_id() == node_id) ++count;
    }
    return count < config_.max_connections_per_peer;
}

void AdmissionController::resolve_duplicate(const NodeId& node_id,
                                             std::shared_ptr<PeerSession> new_session) {
    auto it = sessions_.find(node_id);
    if (it == sessions_.end()) return;
    boost::system::error_code ec;
    new_session->close(ec);
    store_.record_connection_attempt(node_id, true,
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
}

void AdmissionController::remove_session_locked(const NodeId& node_id) noexcept {
    auto it = sessions_.find(node_id);
    if (it == sessions_.end()) return;

    if (it->second.outbound) --outbound_count_; else --inbound_count_;
    store_.mark_connected(node_id, false);
    sessions_.erase(it);
}

void AdmissionController::session_closed(const NodeId& node_id) noexcept {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    session_closed_locked(node_id);
}

void AdmissionController::session_closed_locked(const NodeId& node_id) noexcept {
    auto it = sessions_.find(node_id);
    if (it == sessions_.end()) return;
    if (it->second.outbound) --outbound_count_; else --inbound_count_;
    store_.mark_connected(node_id, false);
    sessions_.erase(it);
}

void AdmissionController::tick(std::uint64_t now_unix_ms) {
    std::unique_lock<std::mutex> lock(sessions_mutex_);

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        std::uint64_t idle = now_unix_ms - it->second.session->last_seen_unix_ms();
        if (idle > static_cast<std::uint64_t>(config_.idle_timeout.count())) {
            boost::system::error_code ec;
            it->second.session->close(ec);
            if (it->second.outbound) --outbound_count_; else --inbound_count_;
            store_.mark_connected(it->first, false);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    auto now = std::chrono::steady_clock::now();
    for (auto& [node_id, lifecycle] : lifecycles_) {
        // Only automatic reconnects are rate-limited; reconnect_peer() enforces
        // the lifecycle backoff, so skip peers that cannot attempt right now.
        if (!lifecycle.can_attempt(now)) continue;
        auto session_it = sessions_.find(node_id);
        if (session_it == sessions_.end()) {
            // Release lock before doing async I/O to avoid deadlock.
            lock.unlock();
            reconnect_peer(node_id, [](const boost::system::error_code&, std::shared_ptr<PeerSession>) {});
            lock.lock();
        }
    }

    last_cleanup_unix_ms_ = now_unix_ms;
}

} // namespace feinstat::network
