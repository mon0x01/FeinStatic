#include "feinstat/network/network_runtime.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/node_record.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace feinstat::network {

NetworkRuntime::NetworkRuntime() = default;

NetworkRuntime::~NetworkRuntime() {
    stop();
}

std::error_code NetworkRuntime::initialize(const NetworkRuntimeConfig& config) {
    // Re-initialization: stop and release any previous resources first so the
    // runtime can be started again without leaking.
    if (initialized_) {
        stop();  // locks lifecycle_mutex_ internally
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        tls_context_.reset();
        store_.reset();
        bootstrap_.reset();
        authenticator_.reset();
        exchange_.reset();
        admission_.reset();
        dispatcher_.reset();
        identity_.reset();
        initialized_ = false;
    }

    std::error_code ec;

    // Validate configuration before acquiring any resource.
    if (config.network_identifier.size() != 32) {
        ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
        return ec;
    }
    if (config.listen_address.empty()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
        return ec;
    }

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
    }

    // Create all runtime resources under the lifecycle mutex so that concurrent
    // stop() calls cannot observe partially constructed state.
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);

    // Load node identity from file (the production path). Failures are reported
    // clearly as an error_code instead of propagating exceptions.
    try {
        if (!config.identity_passphrase.empty()) {
            identity_ = std::make_shared<const FileKeyProvider>(
                FileKeyProvider::load_vault(config.identity_path, config.identity_passphrase));
        } else {
            identity_ = std::make_shared<const FileKeyProvider>(
                FileKeyProvider::load(config.identity_path));
        }
    } catch (const std::exception&) {
        ec = boost::system::errc::make_error_code(boost::system::errc::no_such_file_or_directory);
        identity_.reset();
        return ec;
    }

    // Create TLS context and generate self-signed credentials.
    try {
        tls_context_ = std::make_unique<SecureSessionContext>(
            config.network_identifier,
            config.protocol_version,
            config.capabilities,
            config.endpoints,
            config.record_expiry_unix_ms);
        tls_context_->generate_tls_credentials();
    } catch (const std::exception&) {
        ec = boost::system::errc::make_error_code(boost::system::errc::io_error);
        identity_.reset();
        tls_context_.reset();
        return ec;
    }

    // Open persistent peer store.
    try {
        store_ = std::make_unique<PeerStore>(PeerStore::Config{});
        std::string store_path = config.database_path.empty()
            ? (config.identity_path + ".peers.db")
            : config.database_path;
        store_->open(store_path);
    } catch (const std::exception&) {
        ec = boost::system::errc::make_error_code(boost::system::errc::io_error);
        identity_.reset();
        tls_context_.reset();
        store_.reset();
        return ec;
    }

    // Create store-backed authenticator.
    authenticator_ = std::make_unique<StorePeerAuthenticator>(*store_, config.network_identifier);

    // Create bootstrap discovery from configured entries and signed seed records.
    bootstrap_ = std::make_unique<BootstrapDiscovery>(config.network_identifier);
    for (const auto& entry : config.bootstrap_entries) {
        bootstrap_->add_entry(entry);
    }
    for (const auto& record : config.bootstrap_seed_records) {
        bootstrap_->add_seed_record(record);
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

    // Wire default control-plane handlers into the dispatcher.
    register_default_handlers();

    // Admission notifies the runtime when a session is admitted so a dispatch
    // worker can be started for it.
    admission_->on_session_established = [this](std::shared_ptr<PeerSession> session) {
        start_dispatch_worker(std::move(session));
    };

    // Start listening for inbound peer connections.
    admission_->start_listening(config.listen_address, config.listen_port);
    listen_port_ = admission_->listen_port();

    // Ingest configured signed bootstrap seed records and connect to their
    // valid TCP endpoints.
    ingest_bootstrap(now_ms());

    initialized_ = true;
    return ec;
}

void NetworkRuntime::run() {
    running_ = true;
    stop_requested_ = false;

    while (running_.load(std::memory_order_acquire) &&
           !stop_requested_.load(std::memory_order_acquire)) {
        do_tick();
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            std::this_thread::sleep_for(config_.tick_interval);
        }
    }

    stop();
}

void NetworkRuntime::stop() noexcept {
    running_ = false;
    stop_requested_ = true;

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (admission_) {
            admission_->stop();
        }
    }

    // Join any dispatch workers that have not yet self-removed. Sessions have
    // been closed by admission_->stop(), which unblocks blocked receives.
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(dispatch_workers_mutex_);
        workers.reserve(dispatch_workers_.size());
        for (auto& [node_id, worker] : dispatch_workers_) {
            (void)node_id;
            workers.push_back(std::move(worker));
        }
        dispatch_workers_.clear();
        finished_workers_.clear();
    }
    for (auto& worker : workers) {
        try {
            if (worker.joinable()) worker.join();
        } catch (...) {
            // Joining a dispatch worker must not throw during shutdown.
        }
    }
}

void NetworkRuntime::tick(std::uint64_t now_unix_ms) {
    admission_->tick(now_unix_ms);
}

void NetworkRuntime::do_tick() {
    auto now = now_ms();
    admission_->tick(now);
    reap_finished_workers();
}

void NetworkRuntime::load_bootstrap_records(std::uint64_t now_unix_ms) {
    auto records = bootstrap_->seed_records();
    auto batch = bootstrap_->validate_records(records, now_unix_ms);
    for (const auto& record : batch.valid) {
        store_->upsert(record, DiscoverySource::Bootstrap, now_unix_ms);
    }
}

void NetworkRuntime::ingest_bootstrap(std::uint64_t now_unix_ms) {
    auto records = bootstrap_->seed_records();
    auto batch = bootstrap_->validate_records(records, now_unix_ms);

    // Persist only valid records.
    for (const auto& record : batch.valid) {
        store_->upsert(record, DiscoverySource::Bootstrap, now_unix_ms);
    }

    // Use valid TCP endpoints to initiate outbound peer connections. The
    // expected NodeId is required when connecting to a known signed record.
    // Connection failures do not prevent the node from running.
    for (const auto& record : batch.valid) {
        for (const auto& ep : record.endpoints) {
            if (ep.transport != TransportType::Tcp) continue;
            admission_->connect_to_peer(
                NodeId(record.node_id), ep.host, ep.port,
                [](const boost::system::error_code&, std::shared_ptr<PeerSession>) {
                    // Bootstrap connection failures are non-fatal; the node
                    // keeps running and peer lifecycle bounds any retry.
                });
        }
    }
}

void NetworkRuntime::register_default_handlers() {
    // kPing: respond with a kPong echoing the ping payload.
    dispatcher_->register_handler(PeerMessageType::kPing,
        [this](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes& payload,
               boost::system::error_code& ec) {
            Bytes pong = payload;
            (void)dispatcher_->send(session, PeerMessageType::kPong, pong, ec);
        });

    // kPong: acknowledgement of a ping. No response required.
    dispatcher_->register_handler(PeerMessageType::kPong,
        [](std::shared_ptr<PeerSession>, PeerMessageType, const Bytes&,
           boost::system::error_code& ec) {
            ec = boost::system::error_code{};
        });

    // kDisconnect: close the session gracefully.
    dispatcher_->register_handler(PeerMessageType::kDisconnect,
        [](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes&,
           boost::system::error_code& ec) {
            session->close(ec);
        });

    // kPeerRecordRequest: share a bounded set of records we know about.
    dispatcher_->register_handler(PeerMessageType::kPeerRecordRequest,
        [this](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes& payload,
               boost::system::error_code&) {
            std::vector<NodeId> requested;
            if (decode_peer_record_request(payload, requested)) {
                return; // malformed request; silently ignore
            }
            const NodeId peer_id = session->peer_node_id();
            auto records =
                exchange_->select_records_to_share(peer_id, kMaxPeerRecordMessages);
            Bytes response = encode_peer_record_response(records);
            boost::system::error_code ec;
            (void)dispatcher_->send(session, PeerMessageType::kPeerRecordResponse,
                                    response, ec);
        });

    // kPeerRecordResponse: validate and persist records announced by a peer.
    dispatcher_->register_handler(PeerMessageType::kPeerRecordResponse,
        [this](std::shared_ptr<PeerSession>, PeerMessageType, const Bytes& payload,
               boost::system::error_code&) {
            std::vector<SignedNodeRecord> records;
            if (decode_peer_record_response(payload, records)) {
                return; // malformed response; silently ignore
            }
            const std::uint64_t now = now_ms();
            RecordValidationContext ctx{};
            Bytes net_id;
            {
                std::lock_guard<std::mutex> lock(config_mutex_);
                net_id = config_.network_identifier;
            }
            ctx.network_identifier = std::move(net_id);
            ctx.now_unix_ms = now;
            std::unordered_map<NodeId, std::uint64_t> last_sequence;
            for (const auto& entry : store_->list(true)) {
                last_sequence[entry.node_id] = entry.record.sequence;
            }
            ctx.last_sequence = &last_sequence;

            for (const auto& record : records) {
                RecordValidationResult vr = validate_record(record, ctx);
                if (!vr.valid) {
                    continue;
                }
                store_->upsert(record, DiscoverySource::PeerExchange, now);
            }
        });

    // kPeerRecordAnnounce: validate and persist a single announced record.
    dispatcher_->register_handler(PeerMessageType::kPeerRecordAnnounce,
        [this](std::shared_ptr<PeerSession>, PeerMessageType, const Bytes& payload,
               boost::system::error_code&) {
            SignedNodeRecord record;
            if (decode_peer_record_announce(payload, record)) {
                return; // malformed announce; silently ignore
            }
            const std::uint64_t now = now_ms();
            RecordValidationContext ctx{};
            Bytes net_id;
            {
                std::lock_guard<std::mutex> lock(config_mutex_);
                net_id = config_.network_identifier;
            }
            ctx.network_identifier = std::move(net_id);
            ctx.now_unix_ms = now;
            std::unordered_map<NodeId, std::uint64_t> last_sequence;
            for (const auto& entry : store_->list(true)) {
                last_sequence[entry.node_id] = entry.record.sequence;
            }
            ctx.last_sequence = &last_sequence;

            RecordValidationResult vr = validate_record(record, ctx);
            if (!vr.valid) {
                return; // invalid record; silently ignore
            }
            store_->upsert(record, DiscoverySource::PeerExchange, now);
        });
}

void NetworkRuntime::start_dispatch_worker(std::shared_ptr<PeerSession> session) {
    // If shutdown has begun, do not start a new worker: a thread added after
    // stop() drains dispatch_workers_ would be destroyed joinable. Close the
    // session instead.
    if (!running_.load(std::memory_order_acquire)) {
        boost::system::error_code ec;
        session->close(ec);
        return;
    }

    // Capture the key before moving `session` into the worker lambda; the
    // init-capture below leaves `session` empty.
    const NodeId node_id = session->peer_node_id();

    // If a previous worker for this node finished but was not yet reaped, reap
    // it now so the emplace below does not collide and leave the new session
    // without a dispatch worker. A finished worker has returned from
    // run_dispatch_worker and no longer touches the registry, so joining it is
    // safe; the join happens outside the lock.
    std::thread stale;
    {
        std::lock_guard<std::mutex> lock(dispatch_workers_mutex_);
        auto it = dispatch_workers_.find(node_id);
        if (it != dispatch_workers_.end() && finished_workers_.erase(node_id) > 0) {
            stale = std::move(it->second);
            dispatch_workers_.erase(it);
        }
    }
    if (stale.joinable()) stale.join();

    std::thread worker([this, session = std::move(session)]() mutable {
        run_dispatch_worker(std::move(session));
    });
    std::lock_guard<std::mutex> lock(dispatch_workers_mutex_);
    dispatch_workers_.emplace(node_id, std::move(worker));
}

void NetworkRuntime::reap_finished_workers() {
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> lock(dispatch_workers_mutex_);
        if (finished_workers_.empty()) return;
        done.reserve(finished_workers_.size());
        for (const auto& node_id : finished_workers_) {
            auto it = dispatch_workers_.find(node_id);
            if (it == dispatch_workers_.end()) continue;
            // A finished worker has already returned from run_dispatch_worker
            // and no longer touches the registry, so joining it here cannot
            // deadlock. Move the thread object out and join outside the lock.
            done.push_back(std::move(it->second));
            dispatch_workers_.erase(it);
        }
        finished_workers_.clear();
    }
    for (auto& worker : done) {
        try {
            if (worker.joinable()) worker.join();
        } catch (...) {
            // Reaping must never throw.
        }
    }
}

void NetworkRuntime::run_dispatch_worker(std::shared_ptr<PeerSession> session) {
    const NodeId node_id = session->peer_node_id();

    while (running_.load(std::memory_order_acquire) && session->is_open()) {
        boost::system::error_code ec;
        const std::size_t received = dispatcher_->dispatch(session, ec);
        if (ec || received == 0) {
            break;
        }
    }

    // Session is closed (or the runtime is stopping): remove it from admission
    // bookkeeping so counters and lifecycle state stay consistent.
    admission_->session_closed(node_id);

    // Mark this worker finished so the run loop reaps it. The worker must NOT
    // erase its own std::thread object from dispatch_workers_ (that would
    // destroy a joinable thread and call std::terminate); the run loop joins
    // and erases finished workers on its next tick.
    {
        std::lock_guard<std::mutex> lock(dispatch_workers_mutex_);
        finished_workers_.insert(node_id);
    }
}

std::uint64_t NetworkRuntime::now_ms() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace feinstat::network
