#include "test.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_store.hpp"

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

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

std::string temp_db_path(const char* suffix) {
    return (std::filesystem::temp_directory_path() /
            (std::string("fst-peerstore-") + suffix + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".db"))
        .string();
}

} // namespace

FS_TEST(peer_store_insert_and_get) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("insert");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    UpsertResult result = store.upsert(record, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(result == UpsertResult::Inserted);

    auto entry = store.get(record.node_id);
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->node_id == record.node_id);
    FS_REQUIRE(entry->record.sequence == 1);
    FS_REQUIRE(entry->discovery_source == DiscoverySource::Bootstrap);
    FS_REQUIRE(entry->first_seen_unix_ms == 1000000);
    FS_REQUIRE(entry->last_seen_unix_ms == 1000000);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_update_higher_sequence) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("update");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record_v1 = make_test_record(provider, network_id, 1, 2000000);
    SignedNodeRecord record_v2 = make_test_record(provider, network_id, 2, 3000000);

    UpsertResult r1 = store.upsert(record_v1, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r1 == UpsertResult::Inserted);

    UpsertResult r2 = store.upsert(record_v2, DiscoverySource::PeerExchange, 2000000);
    FS_REQUIRE(r2 == UpsertResult::Updated);

    auto entry = store.get(record_v1.node_id);
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->record.sequence == 2);
    FS_REQUIRE(entry->discovery_source == DiscoverySource::PeerExchange);
    FS_REQUIRE(entry->last_seen_unix_ms == 2000000);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_rejects_stale_sequence) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("stale");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record_v1 = make_test_record(provider, network_id, 1, 2000000);
    SignedNodeRecord record_v0 = make_test_record(provider, network_id, 0, 2000000);

    UpsertResult r1 = store.upsert(record_v1, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r1 == UpsertResult::Inserted);

    UpsertResult r2 = store.upsert(record_v0, DiscoverySource::PeerExchange, 2000000);
    FS_REQUIRE(r2 == UpsertResult::RejectedStale);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_rejects_conflicting_identity) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("conflict");
    store.open(path);

    auto provider_a = Ed25519KeyProvider::generate();
    auto provider_b = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    // Create a record with provider_a's key but provider_b's node ID
    SignedNodeRecord record_a = make_test_record(provider_a, network_id, 1, 2000000);
    SignedNodeRecord record_b_conflict = record_a;
    record_b_conflict.node_id = provider_b.node_id();
    // Re-sign under provider_a's key but with provider_b's node ID (mismatched)
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record_b_conflict.canonical_body());
    record_b_conflict.signature = provider_a.sign(std::move(dw).take());

    // This would fail validation (node_id doesn't match public key), but we're
    // testing the store's conflict detection. The store checks if public_key
    // differs for the same node_id. Let's insert record_a first, then try a
    // record with the same node_id but different key.
    UpsertResult r1 = store.upsert(record_a, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r1 == UpsertResult::Inserted);

    // Now create a record with same node_id but different key
    SignedNodeRecord conflict = record_a;
    conflict.public_key = provider_b.public_key();
    conflict.node_id = provider_b.node_id(); // different node ID actually
    // To test conflict: same node ID, different key
    SignedNodeRecord same_id_diff_key = record_a;
    same_id_diff_key.public_key = provider_b.public_key();
    // NodeId is from provider_a, key is from provider_b = conflict
    UpsertResult r2 = store.upsert(same_id_diff_key, DiscoverySource::PeerExchange, 2000000);
    FS_REQUIRE(r2 == UpsertResult::RejectedConflict);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_duplicate_is_unchanged) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("duplicate");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    UpsertResult r1 = store.upsert(record, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r1 == UpsertResult::Inserted);

    UpsertResult r2 = store.upsert(record, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r2 == UpsertResult::Unchanged);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_persistence_and_restart) {
    std::string path = temp_db_path("restart");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);

    {
        PeerStore store(PeerStore::Config{});
        store.open(path);

        auto provider = Ed25519KeyProvider::generate();
        Bytes network_id = make_network_id();
        SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

        UpsertResult r = store.upsert(record, DiscoverySource::Bootstrap, 1000000);
        FS_REQUIRE(r == UpsertResult::Inserted);
        store.save();
    }
    // First store instance destroyed; data should persist in SQLite

    {
        PeerStore store(PeerStore::Config{});
        store.open(path); // reloads from disk

        auto provider = Ed25519KeyProvider::generate();
        // We can't reproduce the same key, so just check the store has data
        auto entries = store.list();
        FS_REQUIRE(entries.size() == 1);
        FS_REQUIRE(entries[0].record.sequence == 1);
        FS_REQUIRE(entries[0].discovery_source == DiscoverySource::Bootstrap);
    }

    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_cleanup_expired) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("cleanup");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();

    // Record that expires at 1000000
    SignedNodeRecord expiring = make_test_record(provider, network_id, 1, 1000000);
    UpsertResult r = store.upsert(expiring, DiscoverySource::Bootstrap, 1000000);
    FS_REQUIRE(r == UpsertResult::Inserted);

    // Cleanup at time 2000000 (past expiry)
    std::size_t removed = store.cleanup_expired(2000000);
    FS_REQUIRE(removed == 1);
    FS_REQUIRE(store.get(expiring.node_id) == std::nullopt);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_bounded_storage) {
    PeerStore store(PeerStore::Config{2}); // max 2 peers
    std::string path = temp_db_path("bounded");
    store.open(path);

    Bytes network_id = make_network_id();

    auto p1 = Ed25519KeyProvider::generate();
    auto p2 = Ed25519KeyProvider::generate();
    auto p3 = Ed25519KeyProvider::generate();

    SignedNodeRecord r1 = make_test_record(p1, network_id, 1, 2000000);
    SignedNodeRecord r2 = make_test_record(p2, network_id, 1, 2000000);
    SignedNodeRecord r3 = make_test_record(p3, network_id, 1, 2000000);

    FS_REQUIRE(store.upsert(r1, DiscoverySource::Bootstrap, 1000000) == UpsertResult::Inserted);
    FS_REQUIRE(store.upsert(r2, DiscoverySource::Bootstrap, 1000000) == UpsertResult::Inserted);
    FS_REQUIRE(store.upsert(r3, DiscoverySource::Bootstrap, 1000000) == UpsertResult::RejectedStoreFull);

    FS_REQUIRE(store.size() == 2);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_connection_state_tracking) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("connstate");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    store.upsert(record, DiscoverySource::Bootstrap, 1000000);

    store.mark_connected(record.node_id, true);
    store.record_connection_attempt(record.node_id, true, 1100000);
    store.record_connection_attempt(record.node_id, false, 1200000);
    store.touch(record.node_id, 1300000);

    auto entry = store.get(record.node_id);
    FS_REQUIRE(entry.has_value());
    FS_REQUIRE(entry->connection_state.connected == true);
    FS_REQUIRE(entry->connection_state.connection_attempts == 2);
    FS_REQUIRE(entry->connection_state.connection_successes == 1);
    FS_REQUIRE(entry->connection_state.connection_failures == 1);
    FS_REQUIRE(entry->connection_state.last_success_unix_ms == 1100000);
    FS_REQUIRE(entry->connection_state.last_failure_unix_ms == 1200000);
    FS_REQUIRE(entry->last_seen_unix_ms == 1300000);

    store.mark_connected(record.node_id, false);
    entry = store.get(record.node_id);
    FS_REQUIRE(entry->connection_state.connected == false);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_mark_verified) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("verified");
    store.open(path);

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    store.upsert(record, DiscoverySource::Bootstrap, 1000000);

    FS_REQUIRE(!store.get(record.node_id)->connection_state.verified);

    store.mark_verified(record.node_id, 1500000);
    auto entry = store.get(record.node_id);
    FS_REQUIRE(entry->connection_state.verified);
    FS_REQUIRE(entry->last_verified_unix_ms == 1500000);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

FS_TEST(peer_store_list_verified_only) {
    PeerStore store(PeerStore::Config{});
    std::string path = temp_db_path("listverified");
    store.open(path);

    Bytes network_id = make_network_id();
    auto p1 = Ed25519KeyProvider::generate();
    auto p2 = Ed25519KeyProvider::generate();

    SignedNodeRecord r1 = make_test_record(p1, network_id, 1, 2000000);
    SignedNodeRecord r2 = make_test_record(p2, network_id, 1, 2000000);

    store.upsert(r1, DiscoverySource::Bootstrap, 1000000);
    store.upsert(r2, DiscoverySource::Bootstrap, 1000000);

    FS_REQUIRE(store.list().size() == 2);
    FS_REQUIRE(store.list(true).size() == 0); // none verified yet

    store.mark_verified(r1.node_id, 1500000);
    FS_REQUIRE(store.list(true).size() == 1);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}
