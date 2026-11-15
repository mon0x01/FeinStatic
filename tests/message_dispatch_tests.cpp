#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_store.hpp"
#include "feinstat/network/secure_session.hpp"

#include <string>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() { return Bytes(32, 0x42); }

} // namespace

FS_TEST(message_dispatch_constants) {
    FS_REQUIRE(kMaxPingPayloadBytes == 64);
    FS_REQUIRE(kMaxPeerRecordBytes == 64 * 1024);
    FS_REQUIRE(kMaxCapabilityAdvertiseBytes == 256);
    FS_REQUIRE(kMaxErrorBytes == 1024);
}

FS_TEST(message_dispatch_config_default) {
    MessageDispatcher::Config cfg{};
    FS_REQUIRE(cfg.protocol_version == 8);
    FS_REQUIRE(cfg.max_message_queue == 1024);
}

FS_TEST(message_dispatch_message_types) {
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kReserved) == 0);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kProtocolNegotiate) == 1);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kPing) == 5);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kPong) == 6);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kPeerRecordAnnounce) == 7);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kPeerRecordResponse) == 9);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kCapabilityAdvertise) == 10);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kDisconnect) == 12);
    FS_REQUIRE(static_cast<std::uint16_t>(PeerMessageType::kError) == 255);
}

FS_TEST(message_dispatch_handler_registration) {
    Bytes network_id = make_network_id();
    auto tls_ctx = std::unique_ptr<SecureSessionContext>(
        new SecureSessionContext(network_id, 8, {"peer"},
                                 {{TransportType::Tcp, "127.0.0.1", 30331}}, 0));
    tls_ctx->generate_tls_credentials();

    auto store = std::make_unique<PeerStore>(PeerStore::Config{});
    auto bootstrap = std::make_unique<BootstrapDiscovery>(network_id);
    auto authenticator = std::make_unique<StorePeerAuthenticator>(*store, network_id);
    auto exchange = std::make_unique<PeerExchange>(*store, *bootstrap, *authenticator,
                                                    PeerExchange::Config{});

    MessageDispatcher dispatcher(*tls_ctx, *store, *exchange,
                                  MessageDispatcher::Config{8, {"peer"}});

    bool handler_called = false;
    dispatcher.register_handler(PeerMessageType::kPing,
        [&handler_called](std::shared_ptr<PeerSession>, PeerMessageType, const Bytes&, std::error_code& ec) {
            handler_called = true;
            ec = std::error_code{};
        });

    FS_REQUIRE(dispatcher.protocol_version() == 8);
    (void)handler_called;
}
