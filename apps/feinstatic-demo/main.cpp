#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/peer_admission.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_identity.hpp"
#include "feinstat/network/peer_store.hpp"
#include "feinstat/network/secure_session.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

std::atomic<bool> g_stop{false};

void handle_signal(int) { g_stop = true; }

Bytes make_network_id() {
    return Bytes(32, 0x42);
}

SignedNodeRecord make_record(const KeyProvider& provider,
                              const Bytes& network_id,
                              std::uint64_t sequence,
                              std::uint64_t expiry,
                              const std::vector<NodeEndpoint>& endpoints) {
    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = network_id;
    record.node_id = provider.node_id();
    record.key_algorithm = 1;
    record.public_key = provider.public_key();
    record.sequence = sequence;
    record.protocol_versions = {8};
    record.capabilities = {"peer", "bootstrap"};
    record.endpoints = endpoints;
    record.expiry_unix_ms = expiry;
    ByteWriter dw;
    dw.string(std::string(SignedNodeRecord::kSignDomain));
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

int main_impl(int argc, char** argv);

int main(int argc, char** argv) {
    try {
        return main_impl(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "FATAL: unknown exception\n";
        return 1;
    }
}

int main_impl(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (argc < 2) {
        std::cerr << "Usage: feinstatic-demo <node-name> [--reset]\n";
        std::cerr << "  Nodes: alice, bob, carol\n";
        std::cerr << "  --reset: delete persisted identity and peer store before start\n";
        return 1;
    }

    std::string name = argv[1];
    bool reset = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--reset") reset = true;
    }

    Bytes network_id = make_network_id();

    std::uint16_t port = 30400;
    if (name == "alice") port = 30401;
    else if (name == "bob") port = 30402;
    else if (name == "carol") port = 30403;

    std::string identity_path = "/tmp/fst-demo-" + name + ".identity";
    std::string store_path = "/tmp/fst-demo-" + name + ".peers.db";

    // Only delete persistent material when --reset is explicitly requested.
    if (reset) {
        std::error_code ec;
        std::filesystem::remove(identity_path, ec);
        std::filesystem::remove(store_path, ec);
        std::filesystem::remove(store_path + "-wal", ec);
        std::filesystem::remove(store_path + "-shm", ec);
    }

    std::cout << "=== FST Demo Node: " << name << " ===\n";
    std::cout << "Network ID: " << crypto::hex(network_id.data(), network_id.size()) << "\n";
    std::cout << "Listening on 127.0.0.1:" << port << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    // Load existing identity or generate a new one. Persistent identity is
    // preserved across restarts unless --reset is passed.
    std::shared_ptr<const KeyProvider> identity;
    if (!reset && std::filesystem::exists(identity_path)) {
        FileKeyProvider fp = FileKeyProvider::load(identity_path);
        identity = std::make_shared<const FileKeyProvider>(std::move(fp));
    } else {
        NodeIdentity ni = NodeIdentity::generate();
        ni.save_new(identity_path);
        FileKeyProvider fp = FileKeyProvider::load(identity_path);
        identity = std::make_shared<const FileKeyProvider>(std::move(fp));
    }
    std::cout << "[" << name << "] Node ID: " << identity->node_id().value() << "\n";

    auto tls_ctx = std::unique_ptr<SecureSessionContext>(
        new SecureSessionContext(network_id, 8, {"peer"},
                                 {{TransportType::Tcp, "127.0.0.1", port}}, 0));
    tls_ctx->generate_tls_credentials();

    auto store = std::make_unique<PeerStore>(PeerStore::Config{});
    store->open(store_path);

    auto bootstrap = std::make_unique<BootstrapDiscovery>(network_id);

    auto own_record = make_record(*identity, network_id, 1, 0,
                                  {{TransportType::Tcp, "127.0.0.1", port}});
    store->upsert(own_record, DiscoverySource::Configured, now_ms());

    auto authenticator = std::make_unique<StorePeerAuthenticator>(*store, network_id);
    auto exchange = std::make_unique<PeerExchange>(*store, *bootstrap, *authenticator,
                                                    PeerExchange::Config{});

    AdmissionController::Config admission_cfg{};
    admission_cfg.max_connections = 16;
    admission_cfg.max_inbound_connections = 8;
    admission_cfg.max_outbound_connections = 8;
    admission_cfg.max_connections_per_peer = 2;

    auto admission = std::make_unique<AdmissionController>(*store, *bootstrap, *authenticator,
                                                            *exchange, *tls_ctx, identity,
                                                            admission_cfg);

    auto dispatcher = std::make_unique<MessageDispatcher>(*tls_ctx, *store, *exchange,
                                                           MessageDispatcher::Config{8, {"peer"}});

    dispatcher->register_handler(PeerMessageType::kPing,
        [&name](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes& payload, boost::system::error_code& ec) {
            session->send_message(static_cast<std::uint16_t>(PeerMessageType::kPong), payload, ec);
            std::cout << "[" << name << "] Ponged " << session->peer_node_id().value() << "\n";
        });
    dispatcher->register_handler(PeerMessageType::kPong,
        [&name](std::shared_ptr<PeerSession>, PeerMessageType, const Bytes&, boost::system::error_code& ec) {
            std::cout << "[" << name << "] Received pong\n";
            (void)ec;
        });
    dispatcher->register_handler(PeerMessageType::kPeerRecordAnnounce,
        [&name](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes& payload, boost::system::error_code& ec) {
            try {
                auto record = SignedNodeRecord::parse(payload);
                std::cout << "[" << name << "] Peer record from "
                          << session->peer_node_id().value() << " for " << record.node_id.value() << "\n";
            } catch (...) {
                std::cout << "[" << name << "] Failed to parse peer record\n";
            }
            (void)ec;
        });
    dispatcher->register_handler(PeerMessageType::kDisconnect,
        [&name](std::shared_ptr<PeerSession> session, PeerMessageType, const Bytes&, boost::system::error_code& ec) {
            std::cout << "[" << name << "] Disconnect from "
                      << session->peer_node_id().value() << "\n";
            (void)ec;
        });

    admission->start_listening("127.0.0.1", port);
    std::cout << "[" << name << "] Listening on port " << admission->listen_port() << "\n";

    // Alice discovers Bob through bootstrap and connects to him.
    if (name == "alice") {
        bootstrap->add_entry({"bootstrap-bob", "127.0.0.1:30402", {}});
        std::cout << "[" << name << "] Attempting connection to Bob at 127.0.0.1:30402\n";
        admission->connect_to_peer(std::nullopt, "127.0.0.1", 30402,
            [&name, &dispatcher](const boost::system::error_code& ec, std::shared_ptr<PeerSession> session) {
                std::cout << "[" << name << "] Connect callback: ec=" << ec.message() << "\n";
                if (!ec) {
                    std::cout << "[" << name << "] Connected to Bob: "
                              << session->peer_node_id().value() << "\n";
                    boost::system::error_code sec;
                    dispatcher->ping(session, sec);
                } else {
                    std::cout << "[" << name << "] Connect to Bob failed: " << ec.message() << "\n";
                }
            });
        std::cout << "[" << name << "] connect_to_peer returned\n";
    }

    while (!g_stop) {
        admission->tick(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    for (auto& session : admission->sessions()) {
        boost::system::error_code close_ec;
        dispatcher->disconnect(session, close_ec);
        session->close(close_ec);
    }

    admission->stop();
    std::cout << "[" << name << "] Node stopped.\n";
    return 0;
}
