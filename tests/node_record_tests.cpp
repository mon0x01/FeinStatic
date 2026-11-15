#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <algorithm>
#include <cstring>
#include <string>
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
    record.key_algorithm = 1; // Ed25519
    record.public_key = provider.public_key();
    record.sequence = sequence;
    record.protocol_versions = {8, 9};
    record.capabilities = {"bootstrap", "relay"};
    record.endpoints = {
        {TransportType::Tcp, "127.0.0.1", 30333},
        {TransportType::Quic, "127.0.0.1", 30334},
    };
    record.expiry_unix_ms = expiry;

    // Sign with domain separation
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());
    return record;
}

RecordValidationContext make_context(const Bytes& network_id,
                                      std::uint64_t now_ms = 1000000) {
    RecordValidationContext ctx{};
    ctx.network_identifier = network_id;
    ctx.now_unix_ms = now_ms;
    return ctx;
}

} // namespace

FS_TEST(node_record_serialization_roundtrip) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord original = make_test_record(provider, network_id);

    Bytes serialized = original.serialize();
    SignedNodeRecord parsed = SignedNodeRecord::parse(serialized);

    FS_REQUIRE(parsed.format_version == original.format_version);
    FS_REQUIRE(parsed.network_identifier == original.network_identifier);
    FS_REQUIRE(parsed.node_id == original.node_id);
    FS_REQUIRE(parsed.key_algorithm == original.key_algorithm);
    FS_REQUIRE(parsed.public_key == original.public_key);
    FS_REQUIRE(parsed.sequence == original.sequence);
    FS_REQUIRE(parsed.protocol_versions == original.protocol_versions);
    FS_REQUIRE(parsed.capabilities == original.capabilities);
    FS_REQUIRE(parsed.endpoints == original.endpoints);
    FS_REQUIRE(parsed.expiry_unix_ms == original.expiry_unix_ms);
    FS_REQUIRE(parsed.signature == original.signature);
}

FS_TEST(node_record_canonical_body_deterministic) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id);

    Bytes body1 = record.canonical_body();
    Bytes body2 = record.canonical_body();
    FS_REQUIRE(body1 == body2);
}

FS_TEST(node_record_valid_record_passes_validation) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(result.valid);
    FS_REQUIRE(result.error.empty());
}

FS_TEST(node_record_tampered_signature_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    // Tamper with the signature
    record.signature[0] ^= 0xff;

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("signature") != std::string::npos);
}

FS_TEST(node_record_wrong_network_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    Bytes other_network(32, 0x99);
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    RecordValidationContext ctx = make_context(other_network, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("network") != std::string::npos);
}

FS_TEST(node_record_expired_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 500000); // expired

    RecordValidationContext ctx = make_context(network_id, 1000000); // now is after expiry
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("expired") != std::string::npos);
}

FS_TEST(node_record_not_expired_when_expiry_is_zero) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 0); // no expiry

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(result.valid);
}

FS_TEST(node_record_sequence_rollback_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 5, 2000000);

    // Last accepted sequence is 10, so sequence 5 is stale
    RecordValidationContext ctx = make_context(network_id, 1000000);
    std::unordered_map<NodeId, std::uint64_t> last_seq;
    last_seq[record.node_id] = 10;
    ctx.last_sequence = &last_seq;

    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("sequence") != std::string::npos);
}

FS_TEST(node_record_sequence_equal_is_stale) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 5, 2000000);

    RecordValidationContext ctx = make_context(network_id, 1000000);
    std::unordered_map<NodeId, std::uint64_t> last_seq;
    last_seq[record.node_id] = 5; // equal sequence is stale
    ctx.last_sequence = &last_seq;

    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
}

FS_TEST(node_record_sequence_higher_accepted) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 5, 2000000);

    RecordValidationContext ctx = make_context(network_id, 1000000);
    std::unordered_map<NodeId, std::uint64_t> last_seq;
    last_seq[record.node_id] = 4; // lower sequence, so 5 is accepted
    ctx.last_sequence = &last_seq;

    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(result.valid);
}

FS_TEST(node_record_node_id_mismatch_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    // Use a different node's ID
    auto other = Ed25519KeyProvider::generate();
    record.node_id = other.node_id();
    // But keep the original signature (signed under original node_id)
    // Re-signing would be correct; we want to test mismatched ID+key
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = provider.sign(std::move(dw).take());

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("node id") != std::string::npos ||
               result.error.find("public key") != std::string::npos);
}

FS_TEST(node_record_unsupported_version_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    record.format_version = 255; // unsupported

    RecordValidationContext ctx = make_context(network_id, 1000000);
    RecordValidationResult result = validate_record(record, ctx);
    FS_REQUIRE(!result.valid);
    FS_REQUIRE(result.error.find("version") != std::string::npos);
}

FS_TEST(node_record_parse_rejects_truncated) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    Bytes serialized = record.serialize();
    // Truncate the last byte
    serialized.pop_back();
    FS_REQUIRE_THROWS(SignedNodeRecord::parse(serialized));
}

FS_TEST(node_record_strict_bounds_max_endpoints) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);

    // Add too many endpoints
    for (int i = 0; i < 20; ++i) {
        record.endpoints.push_back({TransportType::Tcp, "127.0.0.1",
                                     static_cast<std::uint16_t>(30333 + i)});
    }
    Bytes serialized = record.serialize();
    FS_REQUIRE_THROWS(SignedNodeRecord::parse(serialized));
}

FS_TEST(node_record_invalid_host_rejected) {
    auto provider = Ed25519KeyProvider::generate();
    Bytes network_id = make_network_id();
    SignedNodeRecord record = make_test_record(provider, network_id, 1, 2000000);
    record.endpoints.push_back({TransportType::Tcp, "invalid host with spaces", 30333});
    Bytes serialized = record.serialize();
    FS_REQUIRE_THROWS(SignedNodeRecord::parse(serialized));
}

FS_TEST(node_record_different_nodes_produce_different_ids) {
    auto a = Ed25519KeyProvider::generate();
    auto b = Ed25519KeyProvider::generate();
    FS_REQUIRE(a.node_id() != b.node_id());
}
