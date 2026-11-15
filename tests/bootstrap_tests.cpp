#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <vector>

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

} // namespace

FS_TEST(bootstrap_entry_management) {
    BootstrapDiscovery discovery(make_network_id());

    FS_REQUIRE(discovery.entries().empty());

    BootstrapEntry entry1{};
    entry1.id = "source-a";
    entry1.address = "127.0.0.1:9000";
    discovery.add_entry(entry1);

    FS_REQUIRE(discovery.entries().size() == 1);
    FS_REQUIRE(discovery.entries()[0].id == "source-a");
    FS_REQUIRE(discovery.entries()[0].address == "127.0.0.1:9000");

    BootstrapEntry entry2{};
    entry2.id = "source-b";
    entry2.address = "127.0.0.1:9001";
    discovery.add_entry(entry2);

    FS_REQUIRE(discovery.entries().size() == 2);
}

FS_TEST(bootstrap_seed_records) {
    BootstrapDiscovery discovery(make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    discovery.add_seed_record(record);
    FS_REQUIRE(discovery.seed_records().size() == 1);
    FS_REQUIRE(discovery.seed_records()[0].node_id == provider.node_id());

    // Seed records from entries are also included
    BootstrapEntry entry{};
    entry.id = "source-a";
    entry.address = "127.0.0.1:9000";
    entry.seed_records.push_back(record);
    discovery.add_entry(entry);

    FS_REQUIRE(discovery.seed_records().size() == 2);
}

FS_TEST(bootstrap_validate_records_valid) {
    BootstrapDiscovery discovery(make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    auto batch = discovery.validate_records({record}, 1000000);
    FS_REQUIRE(batch.valid.size() == 1);
    FS_REQUIRE(batch.errors.empty());
    FS_REQUIRE(batch.valid[0].node_id == provider.node_id());
}

FS_TEST(bootstrap_validate_records_wrong_network) {
    BootstrapDiscovery discovery(make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    Bytes other_network(32, 0x99);
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    // Create a bootstrap discovery for a different network
    BootstrapDiscovery other_discovery(other_network);
    auto batch = other_discovery.validate_records({record}, 1000000);
    FS_REQUIRE(batch.valid.empty());
    FS_REQUIRE(batch.errors.size() == 1);
    FS_REQUIRE(batch.errors[0].find("network") != std::string::npos);
}

FS_TEST(bootstrap_validate_records_expired) {
    BootstrapDiscovery discovery(make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 500000); // expired

    auto batch = discovery.validate_records({record}, 1000000);
    FS_REQUIRE(batch.valid.empty());
    FS_REQUIRE(batch.errors.size() == 1);
    FS_REQUIRE(batch.errors[0].find("expired") != std::string::npos);
}

FS_TEST(bootstrap_validate_records_tampered) {
    BootstrapDiscovery discovery(make_network_id());

    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    record.signature[0] ^= 0xff; // tamper

    auto batch = discovery.validate_records({record}, 1000000);
    FS_REQUIRE(batch.valid.empty());
    FS_REQUIRE(batch.errors.size() == 1);
    FS_REQUIRE(batch.errors[0].find("signature") != std::string::npos);
}

FS_TEST(bootstrap_multiple_sources_no_central_authority) {
    BootstrapDiscovery discovery(make_network_id());

    // Multiple independent sources
    for (int i = 0; i < 5; ++i) {
        BootstrapEntry entry{};
        entry.id = "source-" + std::to_string(i);
        entry.address = "127.0.0.1:" + std::to_string(9000 + i);
        discovery.add_entry(entry);
    }

    FS_REQUIRE(discovery.entries().size() == 5);
    // No single source is privileged; all are equal
    for (const auto& entry : discovery.entries()) {
        FS_REQUIRE(!entry.id.empty());
        FS_REQUIRE(!entry.address.empty());
    }
}
