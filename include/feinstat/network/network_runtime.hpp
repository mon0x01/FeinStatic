#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/peer_admission.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_store.hpp"
#include "feinstat/network/secure_session.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace feinstat::network {

// Runtime configuration for the FST networking subsystem.
struct NetworkRuntimeConfig {
    std::string identity_path;
    std::string identity_passphrase;
    std::string database_path;
    Bytes network_identifier;
    std::string listen_address{"127.0.0.1"};
    std::uint16_t listen_port{0};
    std::vector<BootstrapEntry> bootstrap_entries;
    AdmissionController::Config admission{};
    std::uint16_t protocol_version{8};
    std::vector<std::string> capabilities{"peer"};
    std::vector<NodeEndpoint> endpoints;
    std::uint64_t record_expiry_unix_ms{0};
    std::chrono::milliseconds tick_interval{std::chrono::seconds(1)};
};

// The FST network runtime. Integrates identity, TLS, discovery, admission,
// message dispatch, and lifecycle into a single operational subsystem.
class NetworkRuntime {
public:
    NetworkRuntime();
    ~NetworkRuntime();

    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;
    NetworkRuntime(NetworkRuntime&&) = delete;
    NetworkRuntime& operator=(NetworkRuntime&&) = delete;

    // Initialize from configuration. Loads identity, generates TLS credentials,
    // opens the peer store, and starts listening.
    std::error_code initialize(const NetworkRuntimeConfig& config);

    // Start the network runtime (blocking). Runs until stop() is called or a
    // signal is received.
    void run();

    // Request a graceful shutdown.
    void stop() noexcept;

    [[nodiscard]] PeerStore& store() noexcept { return *store_; }
    [[nodiscard]] BootstrapDiscovery& bootstrap() noexcept { return *bootstrap_; }
    [[nodiscard]] PeerExchange& exchange() noexcept { return *exchange_; }
    [[nodiscard]] AdmissionController& admission() noexcept { return *admission_; }
    [[nodiscard]] MessageDispatcher& dispatcher() noexcept { return *dispatcher_; }
    [[nodiscard]] SecureSessionContext& tls_context() noexcept { return *tls_context_; }
    [[nodiscard]] const KeyProvider& identity() const noexcept { return *identity_; }
    [[nodiscard]] NodeId node_id() const noexcept { return identity_->node_id(); }

    [[nodiscard]] bool is_running() const noexcept { return running_; }
    [[nodiscard]] std::uint16_t listen_port() const noexcept { return listen_port_; }

private:
    void do_tick();
    void load_bootstrap_records(std::uint64_t now_unix_ms);

    NetworkRuntimeConfig config_;
    std::unique_ptr<SecureSessionContext> tls_context_;
    std::shared_ptr<const KeyProvider> identity_;
    std::unique_ptr<PeerStore> store_;
    std::unique_ptr<BootstrapDiscovery> bootstrap_;
    std::unique_ptr<StorePeerAuthenticator> authenticator_;
    std::unique_ptr<PeerExchange> exchange_;
    std::unique_ptr<AdmissionController> admission_;
    std::unique_ptr<MessageDispatcher> dispatcher_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread tick_thread_;
    std::uint16_t listen_port_{0};
};

} // namespace feinstat::network
