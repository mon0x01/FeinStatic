#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_store.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() {
    Bytes nid(32, 0x42);
    return nid;
}

std::string temp_db_path(const char* suffix) {
    return (std::filesystem::temp_directory_path() /
            (std::string("fst-demo-") + suffix + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".db"))
        .string();
}

SignedNodeRecord make_record(Ed25519KeyProvider& provider,
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
    record.capabilities = {"bootstrap", "peer"};
    record.endpoints = endpoints;
    record.expiry_unix_ms = expiry;

    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

RecordValidationContext make_context(const Bytes& network_id,
                                      std::uint64_t now_ms) {
    RecordValidationContext ctx{};
    ctx.network_identifier = network_id;
    ctx.now_unix_ms = now_ms;
    return ctx;
}

// Simple test authenticator for the demo
class TestAuthenticatorForDemo : public PeerAuthenticator {
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

FS_TEST(demo_three_node_identities_are_distinct_and_stable) {
    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    Ed25519KeyProvider bob = Ed25519KeyProvider::generate();
    Ed25519KeyProvider carol = Ed25519KeyProvider::generate();

    FS_REQUIRE(alice.node_id() != bob.node_id());
    FS_REQUIRE(bob.node_id() != carol.node_id());
    FS_REQUIRE(alice.node_id() != carol.node_id());

    std::string msg_str = "demo-message";
    Bytes msg(msg_str.begin(), msg_str.end());
    Signature sig_alice = alice.sign(msg);
    FS_REQUIRE(verify_ed25519(alice.public_key(), msg, sig_alice));
    FS_REQUIRE(!verify_ed25519(bob.public_key(), msg, sig_alice));

    Signature sig_record = alice.sign_domain(
        std::string(SignedNodeRecord::kSignDomain), msg);
    Signature sig_session = alice.sign_domain("FST Session Handshake v1", msg);
    FS_REQUIRE(sig_record != sig_session);
}

FS_TEST(demo_signed_record_creation_and_verification) {
    Bytes network_id = make_network_id();
    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();

    SignedNodeRecord record = make_record(
        alice, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30331}});

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(result.valid);

    SignedNodeRecord tampered = record;
    tampered.signature[0] ^= 0xff;
    RecordValidationResult bad = validate_record(tampered, ctx);
    FS_REQUIRE(!bad.valid);

    Bytes other_network(32, 0x99);
    RecordValidationContext bad_ctx = make_context(other_network, 1000000);
    RecordValidationResult wrong_net = validate_record(record, bad_ctx);
    FS_REQUIRE(!wrong_net.valid);

    Bytes serialized = record.serialize();
    SignedNodeRecord parsed = SignedNodeRecord::parse(serialized);
    FS_REQUIRE(parsed.signature == record.signature);
    FS_REQUIRE(parsed.node_id == record.node_id);
}

FS_TEST(demo_bootstrap_discovery_from_multiple_sources) {
    Bytes network_id = make_network_id();

    BootstrapDiscovery discovery(network_id);
    discovery.add_entry({"source-a", "127.0.0.1:9001", {}});
    discovery.add_entry({"source-b", "127.0.0.1:9002", {}});
    discovery.add_entry({"source-c", "127.0.0.1:9003", {}});
    FS_REQUIRE(discovery.entries().size() == 3);

    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    SignedNodeRecord alice_record = make_record(
        alice, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30331}});
    discovery.add_seed_record(alice_record);

    auto batch = discovery.validate_records({alice_record}, 1000000);
    FS_REQUIRE(batch.valid.size() == 1);
    FS_REQUIRE(batch.errors.empty());

    Bytes other_network(32, 0x99);
    BootstrapDiscovery other_discovery(other_network);
    auto bad_batch = other_discovery.validate_records({alice_record}, 1000000);
    FS_REQUIRE(bad_batch.valid.empty());
    FS_REQUIRE(!bad_batch.errors.empty());
}

FS_TEST(demo_peer_store_persistence_and_restart) {
    Bytes network_id = make_network_id();
    std::string path = temp_db_path("restart");

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
        Ed25519KeyProvider bob = Ed25519KeyProvider::generate();

        SignedNodeRecord alice_record = make_record(
            alice, network_id, 1, 2000000,
            {{TransportType::Tcp, "127.0.0.1", 30331}});
        SignedNodeRecord bob_record = make_record(
            bob, network_id, 1, 2000000,
            {{TransportType::Tcp, "127.0.0.1", 30332}});

        FS_REQUIRE(store.upsert(alice_record, DiscoverySource::Bootstrap, 1000000) ==
                   UpsertResult::Inserted);
        FS_REQUIRE(store.upsert(bob_record, DiscoverySource::Bootstrap, 1000000) ==
                   UpsertResult::Inserted);

        store.mark_connected(alice.node_id(), true);
        store.mark_connected(bob.node_id(), true);
        store.mark_verified(alice.node_id(), 1000000);
        store.mark_verified(bob.node_id(), 1000000);
        store.save();
    }

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        auto peers = store.list(true);
        FS_REQUIRE(peers.size() == 2);

        for (const auto& peer : peers) {
            FS_REQUIRE(peer.connection_state.verified);
            FS_REQUIRE(peer.connection_state.connected);
        }
        FS_REQUIRE(store.size() == 2);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(demo_peer_exchange_through_authenticated_connection) {
    Bytes network_id = make_network_id();
    std::string path = temp_db_path("exchange");

    PeerStore store(PeerStore::Config{});
    store.open(path);
    BootstrapDiscovery bootstrap(network_id);
    TestAuthenticatorForDemo auth;
    PeerExchange exchange(store, bootstrap, auth, PeerExchange::Config{});

    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    Ed25519KeyProvider bob = Ed25519KeyProvider::generate();
    Ed25519KeyProvider carol = Ed25519KeyProvider::generate();

    auth.trust(alice.node_id(), alice.public_key());
    auth.trust(bob.node_id(), bob.public_key());
    auth.trust(carol.node_id(), carol.public_key());

    SignedNodeRecord alice_record = make_record(
        alice, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30331}});
    SignedNodeRecord bob_record = make_record(
        bob, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30332}});
    SignedNodeRecord carol_record = make_record(
        carol, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30333}});

    store.upsert(alice_record, DiscoverySource::Bootstrap, 1000000);
    store.upsert(bob_record, DiscoverySource::Bootstrap, 1000000);
    store.mark_verified(alice.node_id(), 1000000);
    store.mark_verified(bob.node_id(), 1000000);
    store.mark_verified(carol.node_id(), 1000000);

    PeerExchangeResult result = exchange.exchange(
        bob.node_id(), {}, {carol_record}, 1000000);
    FS_REQUIRE(result.errors.empty());
    FS_REQUIRE(result.learned_records.size() == 1);
    FS_REQUIRE(result.learned_records[0].node_id == carol.node_id());

    auto carol_entry = store.get(carol.node_id());
    FS_REQUIRE(carol_entry.has_value());
    FS_REQUIRE(carol_entry->discovery_source == DiscoverySource::PeerExchange);

    PeerExchangeResult dup = exchange.exchange(
        bob.node_id(), {}, {carol_record}, 2000000);
    FS_REQUIRE(dup.learned_records.empty());

    SignedNodeRecord tampered = carol_record;
    tampered.signature[0] ^= 0xff;
    PeerExchangeResult bad = exchange.exchange(
        bob.node_id(), {}, {tampered}, 3000000);
    FS_REQUIRE(bad.learned_records.empty());
    FS_REQUIRE(!bad.errors.empty());

    SignedNodeRecord expired = make_record(
        carol, network_id, 2, 500000, {{TransportType::Tcp, "127.0.0.1", 30333}});
    PeerExchangeResult expired_result = exchange.exchange(
        bob.node_id(), {}, {expired}, 1000000);
    FS_REQUIRE(expired_result.learned_records.empty());

    SignedNodeRecord stale = make_record(
        carol, network_id, 0, 2000000, {{TransportType::Tcp, "127.0.0.1", 30333}});
    PeerExchangeResult stale_result = exchange.exchange(
        bob.node_id(), {}, {stale}, 4000000);
    FS_REQUIRE(stale_result.learned_records.empty());
    FS_REQUIRE(!stale_result.errors.empty());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(demo_duplicate_identity_conflict_handling) {
    Bytes network_id = make_network_id();
    std::string path = temp_db_path("conflict");

    PeerStore store(PeerStore::Config{});
    store.open(path);

    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    Ed25519KeyProvider alice_imposter = Ed25519KeyProvider::generate();

    SignedNodeRecord alice_record = make_record(
        alice, network_id, 1, 2000000,
        {{TransportType::Tcp, "127.0.0.1", 30331}});
    FS_REQUIRE(store.upsert(alice_record, DiscoverySource::Bootstrap, 1000000) ==
               UpsertResult::Inserted);

    SignedNodeRecord imposter_record = alice_record;
    imposter_record.public_key = alice_imposter.public_key();
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(imposter_record.canonical_body());
    imposter_record.signature = alice_imposter.sign(std::move(dw).take());

    UpsertResult result = store.upsert(imposter_record, DiscoverySource::PeerExchange,
                                        2000000);
    FS_REQUIRE(result == UpsertResult::RejectedConflict);

    auto entry = store.get(alice.node_id());
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->record.public_key == alice.public_key());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(demo_bounded_storage_and_admission) {
    Bytes network_id = make_network_id();
    std::string path = temp_db_path("bounded");

    PeerStore store(PeerStore::Config{2});
    store.open(path);

    Ed25519KeyProvider a = Ed25519KeyProvider::generate();
    Ed25519KeyProvider b = Ed25519KeyProvider::generate();
    Ed25519KeyProvider c = Ed25519KeyProvider::generate();

    SignedNodeRecord ra = make_record(a, network_id, 1, 2000000,
                                       {{TransportType::Tcp, "127.0.0.1", 30331}});
    SignedNodeRecord rb = make_record(b, network_id, 1, 2000000,
                                       {{TransportType::Tcp, "127.0.0.1", 30332}});
    SignedNodeRecord rc = make_record(c, network_id, 1, 2000000,
                                       {{TransportType::Tcp, "127.0.0.1", 30333}});

    FS_REQUIRE(store.upsert(ra, DiscoverySource::Bootstrap, 1000000) ==
               UpsertResult::Inserted);
    FS_REQUIRE(store.upsert(rb, DiscoverySource::Bootstrap, 1000000) ==
               UpsertResult::Inserted);
    FS_REQUIRE(store.upsert(rc, DiscoverySource::Bootstrap, 1000000) ==
               UpsertResult::RejectedStoreFull);

    FS_REQUIRE(store.size() == 2);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}
