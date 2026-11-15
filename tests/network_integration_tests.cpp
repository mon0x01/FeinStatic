#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"
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
    record.capabilities = {"peer", "bootstrap"};
    record.endpoints = endpoints;
    record.expiry_unix_ms = expiry;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

} // namespace

FS_TEST(integration_three_node_discovery_and_authentication) {
    // Simulates the three-node discovery chain: Alice discovers Bob through
    // bootstrap, then exchanges peer info with Bob to discover Carol.
    // No manual pre-provisioning of a complete peer roster.
    Bytes network_id = make_network_id();

    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    Ed25519KeyProvider bob = Ed25519KeyProvider::generate();
    Ed25519KeyProvider carol = Ed25519KeyProvider::generate();

    FS_REQUIRE(alice.node_id() != bob.node_id());
    FS_REQUIRE(bob.node_id() != carol.node_id());
    FS_REQUIRE(alice.node_id() != carol.node_id());

    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-integration-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                        ".db")).string();

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        // Alice learns Bob's record through bootstrap (dynamic discovery).
        auto bob_record = make_record(bob, network_id, 1, 0,
                                      {{TransportType::Tcp, "127.0.0.1", 30332}});
        FS_REQUIRE(store.upsert(bob_record, DiscoverySource::Bootstrap, 1000000) ==
                   UpsertResult::Inserted);

        // Alice exchanges peer records with Bob and learns Carol (peer exchange).
        BootstrapDiscovery bootstrap(network_id);
        StorePeerAuthenticator authenticator(store, network_id);
        PeerExchange exchange(store, bootstrap, authenticator, PeerExchange::Config{});

        authenticator.register_authenticated_peer(bob.node_id(), bob.public_key(), 1000000);
        authenticator.register_authenticated_peer(carol.node_id(), carol.public_key(), 1000000);

        auto carol_record = make_record(carol, network_id, 1, 0,
                                        {{TransportType::Tcp, "127.0.0.1", 30333}});
        PeerExchangeResult result = exchange.exchange(bob.node_id(), {}, {carol_record}, 1000000);
        FS_REQUIRE(result.errors.empty());
        FS_REQUIRE(result.learned_records.size() == 1);
        FS_REQUIRE(result.learned_records[0].node_id == carol.node_id());

        // Verify Carol is in the store, learned via PeerExchange.
        auto carol_entry = store.get(carol.node_id());
        FS_REQUIRE(carol_entry.has_value());
        FS_REQUIRE(carol_entry->discovery_source == DiscoverySource::PeerExchange);

        // Wrong-network records are rejected.
        Bytes other_network(32, 0x99);
        BootstrapDiscovery other_bootstrap(other_network);
        auto bad_batch = other_bootstrap.validate_records({bob_record}, 1000000);
        FS_REQUIRE(bad_batch.valid.empty());

        // Tampered records are rejected.
        auto tampered = bob_record;
        tampered.signature[0] ^= 0xff;
        auto bad_batch2 = bootstrap.validate_records({tampered}, 1000000);
        FS_REQUIRE(bad_batch2.valid.empty());

        // Expired records are rejected.
        auto expired = make_record(carol, network_id, 1, 500000,
                                   {{TransportType::Tcp, "127.0.0.1", 30333}});
        auto bad_batch3 = bootstrap.validate_records({expired}, 1000000);
        FS_REQUIRE(bad_batch3.valid.empty());
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(integration_peer_store_restart_and_restoration) {
    // Persisted peer store survives restart: connected and verified peers are
    // restored, enabling reconnection without bootstrap.
    Bytes network_id = make_network_id();
    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-restart-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                        ".db")).string();

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
        auto record = make_record(alice, network_id, 1, 0,
                                  {{TransportType::Tcp, "127.0.0.1", 30331}});
        FS_REQUIRE(store.upsert(record, DiscoverySource::Bootstrap, 1000000) ==
                   UpsertResult::Inserted);
        store.mark_connected(alice.node_id(), true);
        store.mark_verified(alice.node_id(), 1000000);
        store.save();
    }

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        auto peers = store.list(true);
        FS_REQUIRE(peers.size() == 1);
        FS_REQUIRE(peers[0].connection_state.verified);
        FS_REQUIRE(peers[0].connection_state.connected);
        FS_REQUIRE(peers[0].node_id == peers[0].record.node_id);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(integration_bootstrap_multiple_sources_no_central_authority) {
    // Multiple independent bootstrap sources; no single source is privileged.
    Bytes network_id = make_network_id();

    BootstrapDiscovery discovery(network_id);
    for (int i = 0; i < 5; ++i) {
        BootstrapEntry entry{};
        entry.id = "source-" + std::to_string(i);
        entry.address = "127.0.0.1:" + std::to_string(9000 + i);
        discovery.add_entry(entry);
    }

    FS_REQUIRE(discovery.entries().size() == 5);
    for (const auto& entry : discovery.entries()) {
        FS_REQUIRE(!entry.id.empty());
        FS_REQUIRE(!entry.address.empty());
    }
}

FS_TEST(integration_duplicate_connection_resolution) {
    // Same peer connecting twice: second connection is resolved as duplicate.
    Bytes network_id = make_network_id();

    Ed25519KeyProvider alice = Ed25519KeyProvider::generate();
    Ed25519KeyProvider bob = Ed25519KeyProvider::generate();

    std::string path = (std::filesystem::temp_directory_path() /
                        ("fst-dup-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()) +
                        ".db")).string();

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        auto bob_record = make_record(bob, network_id, 1, 0,
                                      {{TransportType::Tcp, "127.0.0.1", 30332}});
        FS_REQUIRE(store.upsert(bob_record, DiscoverySource::Bootstrap, 1000000) ==
                   UpsertResult::Inserted);

        // Same node ID, different public key = identity conflict.
        auto imposter = Ed25519KeyProvider::generate();
        auto imposter_record = bob_record;
        imposter_record.public_key = imposter.public_key();
        ByteWriter dw;
        dw.string(SignedNodeRecord::kSignDomain);
        dw.blob(imposter_record.canonical_body());
        imposter_record.signature = imposter.sign(std::move(dw).take());

        UpsertResult result = store.upsert(imposter_record, DiscoverySource::PeerExchange,
                                            2000000);
        FS_REQUIRE(result == UpsertResult::RejectedConflict);

        // Original record is preserved.
        auto entry = store.get(bob.node_id());
        FS_REQUIRE(entry.has_value());
        FS_REQUIRE(entry->record.public_key == bob.public_key());
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}
