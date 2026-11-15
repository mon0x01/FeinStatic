#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace feinstat::network {

// Transport types advertised in a node record. The identity and discovery layers
// are transport-independent; this enum merely classifies endpoint addresses so a
// transport adapter can select the appropriate dialer/listener.
enum class TransportType : std::uint8_t {
    Unknown = 0,
    Tcp = 1,
    Quic = 2,
    Mesh = 3,
    Radio = 4,
    Satellite = 5,
    Dtn = 6,
};

// A single network endpoint advertised by a node record.
struct NodeEndpoint {
    TransportType transport{TransportType::Unknown};
    std::string host;
    std::uint16_t port{0};

    bool operator==(const NodeEndpoint& other) const noexcept {
        return transport == other.transport && host == other.host && port == other.port;
    }
};

// A signed, versioned node record. Records are self-certifying: the signature
// binds the node ID, public key, endpoints, and metadata to the node's signing
// key. A record never grants validator status, account authority, or permission
// to transfer funds — it is solely a claim about node identity and reachability.
struct SignedNodeRecord {
    SignedNodeRecord() = default;

    std::uint8_t format_version{1};
    Bytes network_identifier; // 32-byte network/genesis hash (fork protection, D-036)
    NodeId node_id;
    std::uint8_t key_algorithm{1}; // 1 = Ed25519
    PublicKey public_key{};
    std::uint64_t sequence{0};
    std::vector<std::uint16_t> protocol_versions;
    std::vector<std::string> capabilities;
    std::vector<NodeEndpoint> endpoints;
    std::uint64_t expiry_unix_ms{0};
    Signature signature{};

    // Returns the canonical, deterministic byte representation of the record
    // body (excluding the signature). Two records with identical fields produce
    // identical bytes.
    [[nodiscard]] Bytes canonical_body() const;

    // Returns the full serialized record (canonical body + signature).
    [[nodiscard]] Bytes serialize() const;

    // Parse a record from its serialized form. Throws on truncation or invalid
    // encoding. Signature is not verified by parse(); use validate() for that.
    [[nodiscard]] static SignedNodeRecord parse(const Bytes& data);

    // Domain used for signing node records. All records for all networks share
    // this domain; the network_identifier field provides fork separation.
    static constexpr std::string_view kSignDomain = "FST Signed Node Record v1";
};

// Validation result for a signed node record.
struct RecordValidationResult {
    bool valid{false};
    std::string error;
};

// Validation context: the network this node participates in, the current time,
// and the last accepted sequence per node ID (for replay/stale-sequence protection).
struct RecordValidationContext {
    Bytes network_identifier; // expected network/genesis hash
    std::uint64_t now_unix_ms{0}; // current time for expiry checks
    // Last accepted sequence per node ID. A record with sequence <= this value
    // is rejected as stale/rolled back. Empty means no prior record known.
    const std::unordered_map<NodeId, std::uint64_t>* last_sequence{nullptr};
};

// Validation rules:
// - format_version must be supported (1)
// - network_identifier must match the expected network (wrong-network rejection)
// - node_id must match peer_id_from_public_key(public_key)
// - signature must verify over the canonical body under the embedded public key
// - expiry must be in the future
// - sequence must be strictly greater than the last accepted sequence for this node id
// - strict bounds on record size, endpoint count, and field lengths
[[nodiscard]] RecordValidationResult validate_record(
    const SignedNodeRecord& record,
    const RecordValidationContext& context);

// Strict limits to prevent resource-exhaustion attacks through record ingestion.
inline constexpr std::size_t kMaxRecordBytes = 64 * 1024;
inline constexpr std::size_t kMaxEndpointsPerRecord = 16;
inline constexpr std::size_t kMaxCapabilitiesPerRecord = 32;
inline constexpr std::size_t kMaxProtocolVersionsPerRecord = 16;
inline constexpr std::size_t kMaxCapabilityBytes = 64;
inline constexpr std::size_t kMaxHostBytes = 253;

// Supported format versions. Version 1 is the only supported version; any other
// version is rejected as unsupported (critical-version rejection).
inline constexpr std::uint8_t kSupportedFormatVersion = 1;

// Supported key algorithms. Algorithm 1 = Ed25519.
inline constexpr std::uint8_t kSupportedKeyAlgorithm = 1;

} // namespace feinstat::network
