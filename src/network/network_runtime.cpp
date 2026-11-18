#include "feinstat/network/network_runtime.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/node_record.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
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

    // When a session is admitted, start its strand-bound async read loop so
    // incoming frames are delivered to the dispatcher. All SSL stream
    // operations for the session are then serialized on its strand; there is
    // no dedicated dispatch worker thread and no blocking read/write.
    admission_->on_session_established = [this](std::shared_ptr<PeerSession> peer_session) {
        SecureSession& session = peer_session->secure_session();
        session.set_on_session_closed([this, peer_session](std::shared_ptr<SecureSession>) {
            admission_->session_closed(peer_session->peer_node_id());
        });
        session.start_receive([this, peer_session](std::shared_ptr<SecureSession> /*session*/,
                                                    std::uint16_t type,
                                                    Bytes payload,
                                                    boost::system::error_code& handler_ec) {
            if (handler_ec) {
                // A read error already closed the session and notified admission
                // via on_session_closed_; nothing more to do here.
                return;
            }
            dispatcher_->handle_frame(peer_session, static_cast<PeerMessageType>(type), payload,
                                      handler_ec);
        });
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
}

void NetworkRuntime::tick(std::uint64_t now_unix_ms) {
    admission_->tick(now_unix_ms);
}

void NetworkRuntime::do_tick() {
    auto now = now_ms();
    admission_->tick(now);
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

std::uint64_t NetworkRuntime::now_ms() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace feinstat::network
