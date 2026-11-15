#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/peer_identity.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::network {

// Versioned, self-certifying node identifier.
//
// v1 format: "fn1" + hex(sha256(public_key)) — backward compatible with the
// existing peer_id_from_public_key(). Future versions use a different leading
// prefix (e.g. "fn2") and may use a different hash or key algorithm. The NodeId
// class parses and validates the string, so callers never need to understand the
// wire format.
class NodeId {
public:
    static constexpr std::uint8_t kCurrentVersion = 1;

    // Create a NodeId from a public key using the current version (v1).
    static NodeId from_public_key(const PublicKey& public_key) noexcept;

    // Parse a NodeId from its string representation. Throws std::invalid_argument
    // on an invalid format, unknown version, or mismatched length.
    static NodeId parse(const std::string& value);

    // Verify that a public key corresponds to this NodeId.
    bool matches(const PublicKey& public_key) const noexcept;

    const std::string& value() const noexcept { return value_; }
    std::uint8_t version() const noexcept { return version_; }
    const Hash& hash() const noexcept { return hash_; }

    bool operator==(const NodeId& other) const noexcept { return value_ == other.value_; }
    bool operator!=(const NodeId& other) const noexcept { return value_ != other.value_; }
    bool operator<(const NodeId& other) const noexcept { return value_ < other.value_; }

    // Default constructor for containers. The resulting NodeId is empty and
    // does not match any public key.
    NodeId() = default;

    std::string value_;
    std::uint8_t version_{0};
    Hash hash_{};

private:
    NodeId(std::string value, std::uint8_t version, Hash hash)
        : value_(std::move(value)), version_(version), hash_(hash) {}
};
// Implementations may hold keys in memory, in a mode-0600 file, in an encrypted
// vault, or in an HSM/TPM. The interface deliberately does not expose secret key
// material.
class KeyProvider {
public:
    virtual ~KeyProvider() = default;

    KeyProvider() = default;
    KeyProvider(const KeyProvider&) = delete;
    KeyProvider& operator=(const KeyProvider&) = delete;
    KeyProvider(KeyProvider&&) = delete;
    KeyProvider& operator=(KeyProvider&&) = delete;

    virtual PublicKey public_key() const = 0;
    virtual NodeId node_id() const = 0;
    virtual Signature sign(const Bytes& message) const = 0;
    // Domain-separated signing: signs canonical(domain) || message, preventing
    // cross-context signature reuse (a node-record signature cannot be reused
    // for a session handshake, for example).
    virtual Signature sign_domain(const std::string& domain, const Bytes& message) const = 0;
};

// In-memory Ed25519 key provider. Suitable for tests and short-lived processes.
// The private key is held in memory and cleansed on destruction. No plaintext
// private key is ever persisted by this class.
class Ed25519KeyProvider : public KeyProvider {
public:
    static Ed25519KeyProvider generate();
    static Ed25519KeyProvider from_secret(const SecretKey& secret_key);

    Ed25519KeyProvider(const Ed25519KeyProvider&) = delete;
    Ed25519KeyProvider& operator=(const Ed25519KeyProvider&) = delete;
    Ed25519KeyProvider(Ed25519KeyProvider&&) = delete;
    Ed25519KeyProvider& operator=(Ed25519KeyProvider&&) = delete;
    ~Ed25519KeyProvider() override;

    PublicKey public_key() const override;
    NodeId node_id() const override;
    Signature sign(const Bytes& message) const override;
    Signature sign_domain(const std::string& domain, const Bytes& message) const override;

private:
    Ed25519KeyProvider(PublicKey public_key, SecretKey secret_key);

    PublicKey public_key_{};
    SecretKey secret_key_{};
};

// File-backed key provider using the existing NodeIdentity secure storage
// (mode-0600 plaintext seed file or AES-256-GCM/PBKDF2 encrypted vault). Private
// key material never leaves the NodeIdentity wrapper; this provider only signs.
class FileKeyProvider : public KeyProvider {
public:
    static FileKeyProvider load(const std::string& path);
    static FileKeyProvider load_vault(const std::string& path, const std::string& passphrase);

    void save_new(const std::string& path) const;
    void save_vault_new(const std::string& path, const std::string& passphrase,
                        std::uint32_t iterations = 310'000) const;

    FileKeyProvider(const FileKeyProvider&) = delete;
    FileKeyProvider& operator=(const FileKeyProvider&) = delete;
    FileKeyProvider(FileKeyProvider&&) noexcept;
    FileKeyProvider& operator=(FileKeyProvider&&) noexcept;
    ~FileKeyProvider() override;

    PublicKey public_key() const override;
    NodeId node_id() const override;
    Signature sign(const Bytes& message) const override;
    Signature sign_domain(const std::string& domain, const Bytes& message) const override;

private:
    explicit FileKeyProvider(NodeIdentity identity);

    NodeIdentity identity_;
};

// Key rotation record: binds an old NodeId to a new NodeId, signed by the old key.
// Peers that accept the rotation update their peer store to the new NodeId while
// preserving the monotonic sequence boundary of the old identity.
struct RotationRecord {
    NodeId old_node_id;
    NodeId new_node_id;
    Signature signature; // signed by old key over "FST Node Identity Rotation v1" || old || new
};

} // namespace feinstat::network

template <>
struct std::hash<feinstat::network::NodeId> {
    std::size_t operator()(const feinstat::network::NodeId& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};
