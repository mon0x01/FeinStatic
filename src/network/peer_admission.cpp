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
      identity_(std::move(identity)), config_(std::move(config)) {}

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
    boost::system::error_code ec;
    if (acceptor_) acceptor_->close(ec);
    {
    std::unique_lock<std::mutex> lock(sessions_mutex_);
        for (auto& [id, entry] : sessions_) {
            entry.session->close(ec);
        }
        sessions_.clear();
    }
    io_.stop();
    if (io_thread_.joinable()) io_thread_.join();
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
            std::uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            on_connect(session, result.peer_node_id, "", 0,
                         [](const boost::system::error_code&, std::shared_ptr<PeerSession>) {},
                         handshake_ec, result);
        },
        std::nullopt);
}

void AdmissionController::on_connect(std::shared_ptr<SecureSession> session,
                                      const NodeId& node_id,
                                      const std::string& host,
                                      std::uint16_t port,
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
    const NodeId& peer_id = result.peer_node_id;
    std::uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Release the pre-handshake tracking entry if it was a placeholder.
    if (node_id != peer_id) {
        sessions_.erase(node_id);
    }

    auto existing = sessions_.find(peer_id);
    if (existing != sessions_.end()) {
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
    if (outbound) ++outbound_count_; else ++inbound_count_;

    lifecycles_.emplace(peer_id, PeerLifecycle(peer_id.value(), config_.lifecycle));

    callback(boost::system::error_code{}, peer_session);
}

void AdmissionController::connect_to_peer(
    std::optional<NodeId> expected_peer_node_id,
    const std::string& host,
    std::uint16_t port,
    std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback) {
    NodeId tracking_id = expected_peer_node_id.value_or(NodeId{});

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    if (!can_initiate_outbound(tracking_id)) {
        callback(boost::system::errc::make_error_code(boost::system::errc::not_a_stream), nullptr);
        return;
    }

    auto lifecycle_it = lifecycles_.find(tracking_id);
    if (lifecycle_it != lifecycles_.end()) {
        auto now = std::chrono::steady_clock::now();
        if (!lifecycle_it->second.can_attempt(now)) {
            callback(boost::system::errc::make_error_code(boost::system::errc::timed_out), nullptr);
            return;
        }
    }

    auto session = std::make_shared<SecureSession>(io_, tls_context_, identity_);

    session->async_connect(
        host, port,
        [this, session, tracking_id, host, port, callback = std::move(callback),
         expected_peer_node_id](const boost::system::error_code& ec, SecureHandshakeResult result) mutable {
            if (ec) {
                // Decrement outbound count on failure since the connection
                // never succeeded.
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                --outbound_count_;
            }
            on_connect(session, tracking_id, host, port, std::move(callback), ec, result);
        },
        expected_peer_node_id);
}

void AdmissionController::reconnect_peer(
    const NodeId& node_id,
    std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback) {
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
                        lifecycle_it->second.record_failure(now, 0);
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
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void AdmissionController::remove_session_locked(const NodeId& node_id) noexcept {
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
        if (lifecycle.can_attempt(now) && !lifecycle.enabled()) continue;
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
