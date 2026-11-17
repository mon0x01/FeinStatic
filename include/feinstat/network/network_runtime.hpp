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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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
    std::vector<SignedNodeRecord> bootstrap_seed_records;
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

    // Initialize from configuration. Configuration is validated before any
    // resource is acquired. Identity load, TLS credential generation, and store
    // failures are returned as a non-success error_code (never thrown).
    // Re-initialization is supported: a previous runtime is stopped and its
    // resources released first.
    std::error_code initialize(const NetworkRuntimeConfig& config);

    // Start the network runtime (blocking). Runs the admission tick loop until
    // stop() is called. Intended to be run on a dedicated thread.
    void run();

    // Request a graceful shutdown. Closes the listener and all sessions, and
    // joins all dispatch workers. Safe to call repeatedly.
    void stop() noexcept;

    // Run a single admission tick (idle cleanup + reconnection) at the given
    // wall-clock time. Useful for deterministic test driving.
    void tick(std::uint64_t now_unix_ms);

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

    // Access the runtime configuration (needed for re-initialization/restart
    // scenarios and tests).
    [[nodiscard]] NetworkRuntimeConfig config() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_;
    }

    // Start a dispatch worker for an authenticated session. Called by the
    // admission controller when a new session is admitted.
    void start_dispatch_worker(std::shared_ptr<PeerSession> session);

private:
    void do_tick();
    void load_bootstrap_records(std::uint64_t now_unix_ms);
    void ingest_bootstrap(std::uint64_t now_unix_ms);
    void register_default_handlers();
    void run_dispatch_worker(std::shared_ptr<PeerSession> session);
    void reap_finished_workers();
    std::uint64_t now_ms() noexcept;

    NetworkRuntimeConfig config_;
    mutable std::mutex config_mutex_;
    mutable std::mutex lifecycle_mutex_;
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
    bool initialized_{false};
    std::thread tick_thread_;
    std::uint16_t listen_port_{0};

    // One dispatch worker per authenticated session. A worker must NOT erase
    // its own map entry: doing so would destroy its own joinable std::thread
    // object and call std::terminate. Instead a worker records its NodeId in
    // finished_workers_ on exit; the run loop (do_tick) reaps finished workers
    // by joining and erasing their thread objects. stop() joins all remaining.
    std::mutex dispatch_workers_mutex_;
    std::map<NodeId, std::thread> dispatch_workers_;
    std::set<NodeId> finished_workers_;
};

} // namespace feinstat::network
