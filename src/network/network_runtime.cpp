#include "feinstat/network/network_runtime.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace feinstat::network {

NetworkRuntime::NetworkRuntime() = default;

NetworkRuntime::~NetworkRuntime() {
    stop();
}

std::error_code NetworkRuntime::initialize(const NetworkRuntimeConfig& config) {
    config_ = config;

    // Load node identity from file.
    if (!config.identity_passphrase.empty()) {
        identity_ = std::make_shared<const FileKeyProvider>(
            FileKeyProvider::load_vault(config.identity_path, config.identity_passphrase));
    } else {
        identity_ = std::make_shared<const FileKeyProvider>(
            FileKeyProvider::load(config.identity_path));
    }

    // Create TLS context and generate self-signed credentials.
    tls_context_ = std::make_unique<SecureSessionContext>(
        config.network_identifier,
        config.protocol_version,
        config.capabilities,
        config.endpoints,
        config.record_expiry_unix_ms);
    tls_context_->generate_tls_credentials();

    // Open persistent peer store.
    store_ = std::make_unique<PeerStore>(PeerStore::Config{});
    std::string store_path = config.database_path.empty()
        ? (config.identity_path + ".peers.db")
        : config.database_path;
    store_->open(store_path);

    // Create store-backed authenticator.
    authenticator_ = std::make_unique<StorePeerAuthenticator>(*store_, config.network_identifier);

    // Create bootstrap discovery from configured entries.
    bootstrap_ = std::make_unique<BootstrapDiscovery>(config.network_identifier);
    for (const auto& entry : config.bootstrap_entries) {
        bootstrap_->add_entry(entry);
    }

    // Create peer exchange.
    exchange_ = std::make_unique<PeerExchange>(*store_, *bootstrap_, *authenticator_,
                                                PeerExchange::Config{});

    // Create admission controller.
    admission_ = std::make_unique<AdmissionController>(*store_, *bootstrap_, *authenticator_,
                                                        *exchange_, *tls_context_,
                                                        identity_, config.admission);

    // Create message dispatcher.
    dispatcher_ = std::make_unique<MessageDispatcher>(*tls_context_, *store_, *exchange_,
                                                       MessageDispatcher::Config{config.protocol_version,
                                                                                  config.capabilities});

    // Start listening for inbound peer connections.
    std::error_code ec;
    admission_->start_listening(config.listen_address, config.listen_port);
    listen_port_ = admission_->listen_port();

    return ec;
}

void NetworkRuntime::run() {
    running_ = true;
    stop_requested_ = false;

    while (running_ && !stop_requested_) {
        do_tick();
        std::this_thread::sleep_for(config_.tick_interval);
    }

    stop();
}

void NetworkRuntime::stop() noexcept {
    if (!running_) return;
    running_ = false;
    stop_requested_ = true;
    admission_->stop();
}

void NetworkRuntime::do_tick() {
    auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    admission_->tick(now);
}

void NetworkRuntime::load_bootstrap_records(std::uint64_t now_unix_ms) {
    auto records = bootstrap_->seed_records();
    auto batch = bootstrap_->validate_records(records, now_unix_ms);
    for (const auto& record : batch.valid) {
        store_->upsert(record, DiscoverySource::Bootstrap, now_unix_ms);
    }
}

} // namespace feinstat::network
