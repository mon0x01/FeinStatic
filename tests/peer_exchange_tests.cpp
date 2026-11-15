#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_store.hpp"

#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() {
    Bytes nid(32, 0x42);
    return nid;
}

SignedNodeRecord make_test_record(Ed25519KeyProvider& provider,
                                   const Bytes& network_id,
                                   std::uint64_t sequence = 1,
                                   std::uint64_t expiry = 0) {
    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = network_id;
    record.node_id = provider.node_id();
    record.key_algorithm = 1;
    record.public_key = provider.public_key();
    record.sequence = sequence;
    record.protocol_versions = {8};
    record.capabilities = {"bootstrap"};
    record.endpoints = {{TransportType::Tcp, "127.0.0.1", 30333}};
    record.expiry_unix_ms = expiry;

    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

// Minimal test authenticator: trusts any peer that has been registered.
class TestAuthenticator : public PeerAuthenticator {
public:
    std::optional<PublicKey> trusted_key(const NodeId& node_id) const override {
        auto it = trusted_.find(node_id);
        if (it == trusted_.end()) return std::nullopt;
        return it->second;
    }
    bool verify_peer_identity(const NodeId& node_id,
                               const PublicKey& public_key) const override {
        auto it = trusted_.find(node_id);
        return it != trusted_.end() && it->second == public_key;
    }
    void register_authenticated_peer(const NodeId& node_id,
                                      const PublicKey& public_key,
                                      std::uint64_t now_unix_ms) override {
        (void)now_unix_ms;
        trusted_[node_id] = public_key;
    }
    bool is_verified(const NodeId& node_id) const noexcept override {
        return trusted_.find(node_id) != trusted_.end();
    }

    void trust(const NodeId& node_id, const PublicKey& key) {
        trusted_[node_id] = key;
    }

private:
    std::unordered_map<NodeId, PublicKey> trusted_;
};

} // namespace

FS_TEST(peer_exchange_learns_valid_records) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto provider = Ed25519KeyProvider::generate();
    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    // Authenticate our node so the exchange can work
    auth.register_authenticated_peer(provider.node_id(), provider.public_key(), 1000000);

    // Insert our own record into the store so we're known
    SignedNodeRecord our_record = make_test_record(provider, network_id, 1, 2000000);
    store.upsert(our_record, DiscoverySource::Bootstrap, 1000000);

    // Peer sends us a new valid record
    SignedNodeRecord peer_record = make_test_record(peer_provider, network_id, 1, 2000000);

    PeerExchangeResult result = exchange.exchange(peer_provider.node_id(), {}, {peer_record}, 1000000);
    FS_REQUIRE(result.errors.empty());
    FS_REQUIRE(result.learned_records.size() == 1);
    FS_REQUIRE(result.learned_records[0].node_id == peer_provider.node_id());

    // Verify the record was stored
    auto entry = store.get(peer_provider.node_id());
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->discovery_source == DiscoverySource::PeerExchange);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_rejects_tampered_record) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto provider = Ed25519KeyProvider::generate();
    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    SignedNodeRecord peer_record = make_test_record(peer_provider, network_id, 1, 2000000);
    peer_record.signature[0] ^= 0xff; // tamper

    PeerExchangeResult result = exchange.exchange(peer_provider.node_id(), {}, {peer_record}, 1000000);
    FS_REQUIRE(result.learned_records.empty());
    FS_REQUIRE(!result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_rejects_expired_record) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    SignedNodeRecord expired = make_test_record(peer_provider, network_id, 1, 500000);
    PeerExchangeResult result = exchange.exchange(peer_provider.node_id(), {}, {expired}, 1000000);
    FS_REQUIRE(result.learned_records.empty());
    FS_REQUIRE(!result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_rejects_wrong_network) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    Bytes other_network(32, 0x99);

    SignedNodeRecord record = make_test_record(peer_provider, other_network, 1, 2000000);
    PeerExchangeResult result = exchange.exchange(peer_provider.node_id(), {}, {record}, 1000000);
    FS_REQUIRE(result.learned_records.empty());
    FS_REQUIRE(!result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_rate_limiting) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange::Config config{};
    config.max_records_per_peer_per_hour = 2;
    PeerExchange exchange(store, bootstrap, auth, config);

    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    SignedNodeRecord r1 = make_test_record(peer_provider, network_id, 1, 2000000);
    SignedNodeRecord r2 = make_test_record(peer_provider, network_id, 2, 2000000);
    SignedNodeRecord r3 = make_test_record(peer_provider, network_id, 3, 2000000);

    PeerExchangeResult result = exchange.exchange(peer_provider.node_id(), {}, {r1, r2, r3}, 1000000);
    FS_REQUIRE(!result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_duplicate_not_learned_twice) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    SignedNodeRecord record = make_test_record(peer_provider, network_id, 1, 2000000);

    PeerExchangeResult result1 = exchange.exchange(peer_provider.node_id(), {}, {record}, 1000000);
    FS_REQUIRE(result1.learned_records.size() == 1);

    PeerExchangeResult result2 = exchange.exchange(peer_provider.node_id(), {}, {record}, 2000000);
    FS_REQUIRE(result2.learned_records.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_select_records_to_share) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto provider = Ed25519KeyProvider::generate();
    auto peer_provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    SignedNodeRecord our_record = make_test_record(provider, network_id, 1, 2000000);
    SignedNodeRecord peer_record = make_test_record(peer_provider, network_id, 1, 2000000);

    store.upsert(our_record, DiscoverySource::Bootstrap, 1000000);
    store.upsert(peer_record, DiscoverySource::Bootstrap, 1000000);
    store.mark_verified(provider.node_id(), 1000000);
    store.mark_verified(peer_provider.node_id(), 1000000);

    auto to_share = exchange.select_records_to_share(peer_provider.node_id(), 10);
    FS_REQUIRE(to_share.size() == 1);
    FS_REQUIRE(to_share[0].node_id == provider.node_id());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_exchange_store_full_rejection) {
    PeerStore store(PeerStore::Config{1}); // max 1 peer
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    BootstrapDiscovery bootstrap(make_network_id());
    TestAuthenticator auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    auto p1 = Ed25519KeyProvider::generate();
    auto p2 = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    store.upsert(make_test_record(p1, network_id, 1, 2000000), DiscoverySource::Bootstrap, 1000000);

    SignedNodeRecord r2 = make_test_record(p2, network_id, 1, 2000000);
    PeerExchangeResult result = exchange.exchange(p2.node_id(), {}, {r2}, 1000000);
    FS_REQUIRE(result.learned_records.empty());
    FS_REQUIRE(!result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(store_peer_authenticator_trusted_key) {
    PeerStore store(PeerStore::Config{});
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-pstore-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                         ".db"))
                           .string();
    store.open(path);
    StorePeerAuthenticator authenticator(store, make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    store.upsert(record, DiscoverySource::Bootstrap, 1000000);

    FS_REQUIRE(!authenticator.trusted_key(provider.node_id()).has_value());
    FS_REQUIRE(!authenticator.is_verified(provider.node_id()));

    authenticator.register_authenticated_peer(provider.node_id(), provider.public_key(), 1000000);
    FS_REQUIRE(authenticator.is_verified(provider.node_id()));

    auto key = authenticator.trusted_key(provider.node_id());
    FS_REQUIRE(key.has_value());
    FS_REQUIRE(*key == provider.public_key());

    auto other = Ed25519KeyProvider::generate();
    FS_REQUIRE(!authenticator.verify_peer_identity(provider.node_id(), other.public_key()));
    FS_REQUIRE(authenticator.verify_peer_identity(provider.node_id(), provider.public_key()));

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}
