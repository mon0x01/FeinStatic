#include "feinstat/network/node_record.hpp"

#include "feinstat/common/bytes.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace feinstat::network {
namespace {

bool valid_host(const std::string& host) {
    if (host.empty() || host.size() > kMaxHostBytes) return false;
    for (char c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':')) {
            return false;
        }
    }
    return true;
}

void write_u16(ByteWriter& writer, std::uint16_t value) {
    writer.u8(static_cast<Byte>(value >> 8U));
    writer.u8(static_cast<Byte>(value & 0xffU));
}

std::uint16_t read_u16(ByteReader& reader) {
    const Byte hi = reader.u8();
    const Byte lo = reader.u8();
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hi) << 8U) | lo);
}

} // namespace

//------------------------------------------------------------------------------
// SignedNodeRecord serialization
//------------------------------------------------------------------------------

Bytes SignedNodeRecord::canonical_body() const {
    ByteWriter writer;
    writer.u8(format_version);
    writer.blob(network_identifier);
    writer.string(node_id.value());
    writer.u8(key_algorithm);
    writer.fixed(public_key);
    writer.u64(sequence);

    writer.u32(static_cast<std::uint32_t>(protocol_versions.size()));
    for (std::uint16_t version : protocol_versions) {
        write_u16(writer, version);
    }

    writer.u32(static_cast<std::uint32_t>(capabilities.size()));
    for (const std::string& cap : capabilities) {
        writer.string(cap);
    }

    writer.u32(static_cast<std::uint32_t>(endpoints.size()));
    for (const NodeEndpoint& ep : endpoints) {
        writer.u8(static_cast<Byte>(ep.transport));
        writer.string(ep.host);
        write_u16(writer, ep.port);
    }

    writer.u64(expiry_unix_ms);
    return std::move(writer).take();
}

Bytes SignedNodeRecord::serialize() const {
    Bytes body = canonical_body();
    Bytes result;
    result.reserve(body.size() + signature.size());
    result.insert(result.end(), body.begin(), body.end());
    result.insert(result.end(), signature.begin(), signature.end());
    return result;
}

SignedNodeRecord SignedNodeRecord::parse(const Bytes& data) {
    if (data.size() > kMaxRecordBytes) {
        throw std::length_error("signed node record exceeds maximum size");
    }
    ByteReader reader(data);

    SignedNodeRecord record;
    record.format_version = reader.u8();
    record.network_identifier = reader.blob(32);
    record.node_id = NodeId::parse(reader.string(128));
    record.key_algorithm = reader.u8();
    record.public_key = reader.fixed<32>();
    record.sequence = reader.u64();

    const auto version_count = reader.u32();
    if (version_count > kMaxProtocolVersionsPerRecord) {
        throw std::length_error("too many protocol versions in node record");
    }
    for (std::uint32_t i = 0; i < version_count; ++i) {
        record.protocol_versions.push_back(read_u16(reader));
    }

    const auto cap_count = reader.u32();
    if (cap_count > kMaxCapabilitiesPerRecord) {
        throw std::length_error("too many capabilities in node record");
    }
    for (std::uint32_t i = 0; i < cap_count; ++i) {
        std::string cap = reader.string(kMaxCapabilityBytes);
        record.capabilities.push_back(std::move(cap));
    }

    const auto ep_count = reader.u32();
    if (ep_count > kMaxEndpointsPerRecord) {
        throw std::length_error("too many endpoints in node record");
    }
    for (std::uint32_t i = 0; i < ep_count; ++i) {
        NodeEndpoint ep;
        ep.transport = static_cast<TransportType>(reader.u8());
        ep.host = reader.string(kMaxHostBytes);
        ep.port = read_u16(reader);
        if (!valid_host(ep.host)) {
            throw std::runtime_error("invalid endpoint host in node record");
        }
        record.endpoints.push_back(std::move(ep));
    }

    record.expiry_unix_ms = reader.u64();
    record.signature = reader.fixed<64>();

    if (!reader.empty()) {
        throw std::runtime_error("signed node record contains trailing data");
    }
    return record;
}

//------------------------------------------------------------------------------
// Validation
//------------------------------------------------------------------------------

RecordValidationResult validate_record(const SignedNodeRecord& record,
                                        const RecordValidationContext& context) {
    RecordValidationResult result;

    // 1. Format version check (unsupported critical-version rejection).
    if (record.format_version != kSupportedFormatVersion) {
        result.error = "unsupported node record format version";
        return result;
    }

    // 2. Key algorithm check.
    if (record.key_algorithm != kSupportedKeyAlgorithm) {
        result.error = "unsupported key algorithm in node record";
        return result;
    }

    // 3. Wrong-network rejection (fork protection, D-036).
    if (record.network_identifier != context.network_identifier) {
        result.error = "node record network identifier does not match expected network";
        return result;
    }

    // 4. Node ID must match the embedded public key.
    if (!record.node_id.matches(record.public_key)) {
        result.error = "node id does not match the embedded public key";
        return result;
    }

    // 5. Expiry check.
    if (record.expiry_unix_ms != 0 && record.expiry_unix_ms <= context.now_unix_ms) {
        result.error = "node record has expired";
        return result;
    }

    // 6. Stale-sequence rejection: sequence must be strictly greater than the
    // last accepted sequence for this node id (rollback protection).
    if (context.last_sequence != nullptr) {
        const auto it = context.last_sequence->find(record.node_id);
        if (it != context.last_sequence->end() && record.sequence <= it->second) {
            result.error = "node record sequence is not greater than the last accepted sequence";
            return result;
        }
    }

    // 7. Signature verification (domain-separated over canonical body).
    ByteWriter domain_writer;
    domain_writer.string(SignedNodeRecord::kSignDomain);
    domain_writer.blob(record.canonical_body());
    const Bytes signed_message = std::move(domain_writer).take();
    if (!crypto::verify_ed25519(record.public_key, signed_message, record.signature)) {
        result.error = "node record signature is invalid";
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace feinstat::network
