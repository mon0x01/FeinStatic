#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_admission.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_store.hpp"
#include "feinstat/network/secure_session.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() { return Bytes(32, 0x42); }

} // namespace

FS_TEST(peer_admission_config_defaults) {
    AdmissionController::Config cfg{};
    FS_REQUIRE(cfg.max_connections == 64);
    FS_REQUIRE(cfg.max_inbound_connections == 32);
    FS_REQUIRE(cfg.max_outbound_connections == 32);
    FS_REQUIRE(cfg.max_connections_per_peer == 2);
}

FS_TEST(peer_admission_tick_no_sessions) {
    Bytes network_id = make_network_id();
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-admission-tick-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                        ".db")).string();

    auto store = std::make_unique<PeerStore>(PeerStore::Config{});
    store->open(path);
    auto bootstrap = std::make_unique<BootstrapDiscovery>(network_id);
    auto authenticator = std::make_unique<StorePeerAuthenticator>(*store, network_id);
    auto exchange = std::make_unique<PeerExchange>(*store, *bootstrap, *authenticator,
                                                    PeerExchange::Config{});
    auto tls_ctx = std::unique_ptr<SecureSessionContext>(
        new SecureSessionContext(network_id, 8, {"peer"},
                                 {{TransportType::Tcp, "127.0.0.1", 30331}}, 0));
    tls_ctx->generate_tls_credentials();
    Ed25519KeyProvider provider = Ed25519KeyProvider::generate();
    std::shared_ptr<const Ed25519KeyProvider> identity(&provider, [](const Ed25519KeyProvider*) {});

    AdmissionController::Config cfg{};
    cfg.idle_timeout = std::chrono::seconds(1);

    AdmissionController admission(*store, *bootstrap, *authenticator, *exchange,
                                   *tls_ctx, identity, cfg);

    admission.tick(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
    FS_REQUIRE(admission.connection_count() == 0);
    FS_REQUIRE(admission.inbound_count() == 0);
    FS_REQUIRE(admission.outbound_count() == 0);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_admission_connection_limits_config) {
    AdmissionController::Config cfg{};
    cfg.max_connections = 10;
    cfg.max_inbound_connections = 5;
    cfg.max_outbound_connections = 5;
    cfg.max_connections_per_peer = 1;

    FS_REQUIRE(cfg.max_connections == 10);
    FS_REQUIRE(cfg.max_inbound_connections == 5);
    FS_REQUIRE(cfg.max_outbound_connections == 5);
    FS_REQUIRE(cfg.max_connections_per_peer == 1);
}
