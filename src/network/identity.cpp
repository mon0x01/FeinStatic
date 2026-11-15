#include "feinstat/network/identity.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/common/bytes.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace feinstat::network {
namespace {

constexpr std::string_view kNodeIdPrefixV1{"fn1"};

} // namespace

//------------------------------------------------------------------------------
// NodeId
//------------------------------------------------------------------------------

NodeId NodeId::from_public_key(const PublicKey& public_key) noexcept {
    const Bytes bytes(public_key.begin(), public_key.end());
    const Hash digest = crypto::sha256(bytes);
    std::string value(kNodeIdPrefixV1);
    value += crypto::hex(digest.data(), digest.size());
    return NodeId(std::move(value), kCurrentVersion, digest);
}

NodeId NodeId::parse(const std::string& value) {
    if (value.size() < kNodeIdPrefixV1.size() + 2) {
        throw std::invalid_argument("node id is too short");
    }
    std::string prefix = value.substr(0, kNodeIdPrefixV1.size());
    if (prefix != std::string(kNodeIdPrefixV1)) {
        throw std::invalid_argument("unknown node id format prefix");
    }
    // v1: "fn1" + 64 hex chars = 67 total
    if (value.size() != kNodeIdPrefixV1.size() + 64) {
        throw std::invalid_argument("node id hash length is invalid for version 1");
    }
    const std::string hash_hex = value.substr(kNodeIdPrefixV1.size());
    Hash hash{};
    try {
        Bytes decoded = crypto::unhex(hash_hex);
        std::copy(decoded.begin(), decoded.end(), hash.begin());
    } catch (const std::exception&) {
        throw std::invalid_argument("node id hash is not valid hexadecimal");
    }
    return NodeId(value, kCurrentVersion, hash);
}

bool NodeId::matches(const PublicKey& public_key) const noexcept {
    const Bytes bytes(public_key.begin(), public_key.end());
    const Hash digest = crypto::sha256(bytes);
    return version_ == kCurrentVersion && digest == hash_;
}

//------------------------------------------------------------------------------
// Ed25519KeyProvider
//------------------------------------------------------------------------------

Ed25519KeyProvider Ed25519KeyProvider::generate() {
    auto pair = crypto::generate_ed25519_keypair();
    PublicKey public_key = pair.public_key;
    SecretKey secret = pair.secret_key;
    OPENSSL_cleanse(pair.secret_key.data(), pair.secret_key.size());
    return Ed25519KeyProvider(public_key, secret);
}

Ed25519KeyProvider Ed25519KeyProvider::from_secret(const SecretKey& secret_key) {
    PublicKey public_key = crypto::derive_ed25519_public_key(secret_key);
    return Ed25519KeyProvider(public_key, secret_key);
}

Ed25519KeyProvider::~Ed25519KeyProvider() {
    OPENSSL_cleanse(secret_key_.data(), secret_key_.size());
    public_key_.fill(0);
}

Ed25519KeyProvider::Ed25519KeyProvider(PublicKey public_key, SecretKey secret_key)
    : public_key_(std::move(public_key)), secret_key_(std::move(secret_key)) {}

PublicKey Ed25519KeyProvider::public_key() const {
    return public_key_;
}

NodeId Ed25519KeyProvider::node_id() const {
    return NodeId::from_public_key(public_key_);
}

Signature Ed25519KeyProvider::sign(const Bytes& message) const {
    return crypto::sign_ed25519(secret_key_, message);
}

Signature Ed25519KeyProvider::sign_domain(const std::string& domain,
                                           const Bytes& message) const {
    ByteWriter writer;
    writer.string(domain);
    writer.blob(message);
    return crypto::sign_ed25519(secret_key_, std::move(writer).take());
}

//------------------------------------------------------------------------------
// FileKeyProvider
//------------------------------------------------------------------------------

FileKeyProvider FileKeyProvider::load(const std::string& path) {
    return FileKeyProvider(NodeIdentity::load(path));
}

FileKeyProvider FileKeyProvider::load_vault(const std::string& path,
                                             const std::string& passphrase) {
    return FileKeyProvider(NodeIdentity::load_vault(path, passphrase));
}

void FileKeyProvider::save_new(const std::string& path) const {
    identity_.save_new(path);
}

void FileKeyProvider::save_vault_new(const std::string& path,
                                      const std::string& passphrase,
                                      std::uint32_t iterations) const {
    identity_.save_vault_new(path, passphrase, iterations);
}

FileKeyProvider::~FileKeyProvider() = default;

FileKeyProvider::FileKeyProvider(NodeIdentity identity)
    : identity_(std::move(identity)) {}

FileKeyProvider::FileKeyProvider(FileKeyProvider&& other) noexcept
    : identity_(std::move(other.identity_)) {}

FileKeyProvider& FileKeyProvider::operator=(FileKeyProvider&& other) noexcept {
    if (this != &other) {
        identity_ = std::move(other.identity_);
    }
    return *this;
}

PublicKey FileKeyProvider::public_key() const {
    return identity_.public_key();
}

NodeId FileKeyProvider::node_id() const {
    return NodeId::from_public_key(identity_.public_key());
}

Signature FileKeyProvider::sign(const Bytes& message) const {
    return identity_.sign(message);
}

Signature FileKeyProvider::sign_domain(const std::string& domain,
                                        const Bytes& message) const {
    ByteWriter writer;
    writer.string(domain);
    writer.blob(message);
    return identity_.sign(std::move(writer).take());
}

} // namespace feinstat::network
