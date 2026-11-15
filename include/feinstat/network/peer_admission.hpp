#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_lifecycle.hpp"
#include "feinstat/network/peer_session.hpp"
#include "feinstat/network/peer_store.hpp"
#include "feinstat/network/secure_session.hpp"

#include <boost/system/error_code.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace feinstat::network {

// AdmissionController manages dynamic peer admission: inbound and outbound
// connections, connection limits, duplicate resolution, reconnection with
// bounded backoff, and peer lifecycle tracking.
class AdmissionController {
public:
    struct Config {
        std::size_t max_connections{64};
        std::size_t max_inbound_connections{32};
        std::size_t max_outbound_connections{32};
        std::size_t max_connections_per_peer{2};
        std::chrono::milliseconds connection_timeout{std::chrono::seconds(10)};
        std::chrono::milliseconds idle_timeout{std::chrono::minutes(5)};
        PeerLifecycleConfig lifecycle{};
    };

    AdmissionController(PeerStore& store,
                        BootstrapDiscovery& bootstrap,
                        PeerAuthenticator& authenticator,
                        PeerExchange& exchange,
                        SecureSessionContext& tls_context,
                        std::shared_ptr<const KeyProvider> identity,
                        Config config);

    ~AdmissionController();

    AdmissionController(const AdmissionController&) = delete;
    AdmissionController& operator=(const AdmissionController&) = delete;
    AdmissionController(AdmissionController&&) = delete;
    AdmissionController& operator=(AdmissionController&&) = delete;

    // Start listening on the given address/port for inbound peer connections.
    void start_listening(const std::string& address, std::uint16_t port);

    // Stop listening and close all sessions.
    void stop() noexcept;

    // Initiate an outbound connection to a discovered peer endpoint.
    // expected_peer_node_id, when set, requires the peer to authenticate as this
    // NodeId during the handshake. When nullopt, the peer's identity is still
    // verified through TLS + signed record, but the specific NodeId is not
    // pre-checked (useful for bootstrap-discovered peers whose ID is unknown).
    void connect_to_peer(std::optional<NodeId> expected_peer_node_id,
                         const std::string& host,
                         std::uint16_t port,
                         std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback);

    // Reconnect to a peer using its stored endpoints (with backoff via PeerLifecycle).
    void reconnect_peer(const NodeId& node_id,
                        std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback);

    // Look up an existing session by node ID.
    [[nodiscard]] std::shared_ptr<PeerSession> session(const NodeId& node_id) const noexcept;

    [[nodiscard]] std::vector<std::shared_ptr<PeerSession>> sessions() const noexcept;

    [[nodiscard]] std::size_t connection_count() const noexcept;
    [[nodiscard]] std::size_t inbound_count() const noexcept;
    [[nodiscard]] std::size_t outbound_count() const noexcept;
    [[nodiscard]] std::uint16_t listen_port() const noexcept { return listen_port_; }

    // Run the periodic maintenance tick (cleanup, reconnection, health checks).
    void tick(std::uint64_t now_unix_ms);

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    struct ConnectionEntry {
        std::shared_ptr<PeerSession> session;
        bool outbound{false};
        std::uint64_t connected_at_unix_ms{0};
    };

    void do_accept();
    void on_accept(std::shared_ptr<SecureSession> session, const boost::system::error_code& ec);
    void on_connect(std::shared_ptr<SecureSession> session,
                    const NodeId& node_id,
                    const std::string& host,
                    std::function<void(boost::system::error_code, std::shared_ptr<PeerSession>)> callback,
                    const boost::system::error_code& ec,
                    SecureHandshakeResult result);

    bool can_accept_inbound() const noexcept;
    bool can_initiate_outbound(const NodeId& node_id) const noexcept;
    void resolve_duplicate(const NodeId& node_id, std::shared_ptr<PeerSession> new_session);
    void remove_session_locked(const NodeId& node_id) noexcept;

    PeerStore& store_;
    BootstrapDiscovery& bootstrap_;
    PeerAuthenticator& authenticator_;
    PeerExchange& exchange_;
    SecureSessionContext& tls_context_;
    std::shared_ptr<const KeyProvider> identity_;
    Config config_;

    boost::asio::io_context io_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::thread io_thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex sessions_mutex_;
    std::map<NodeId, ConnectionEntry> sessions_;
    std::map<NodeId, PeerLifecycle> lifecycles_;
    std::size_t inbound_count_{0};
    std::size_t outbound_count_{0};
    std::uint64_t last_cleanup_unix_ms_{0};
    std::uint16_t listen_port_{0};
};

} // namespace feinstat::network
