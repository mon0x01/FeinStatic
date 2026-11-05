#include "feinstat/network/peer_identity.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/common/bytes.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace feinstat::network {
namespace {

void write_all(int fd, const Byte* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t written = ::write(fd, data + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "write node identity");
        }
        if (written == 0) throw std::runtime_error("short write while storing node identity");
        offset += static_cast<std::size_t>(written);
    }
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

constexpr std::array<Byte, 8> kIdentityVaultMagic{'F', 'S', 'N', 'V', 'L', 'T', '1', '\n'};
constexpr std::size_t kIdentityVaultBytes = 8U + 4U + 16U + 12U + 32U + 16U;

Bytes read_private_file(const std::string& path, std::size_t expected_size, const char* purpose) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), std::string("open ") + purpose);
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0) {
        const int error = errno; (void)::close(fd);
        throw std::system_error(error, std::generic_category(), std::string("stat ") + purpose);
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0) {
        (void)::close(fd);
        throw std::runtime_error(std::string(purpose) + " must be a regular file owned by the current user with mode 0600");
    }
    if (metadata.st_size < 0 || static_cast<std::uint64_t>(metadata.st_size) != expected_size) {
        (void)::close(fd);
        throw std::runtime_error(std::string(purpose) + " has an invalid file size");
    }
    Bytes result(expected_size);
    std::size_t offset = 0;
    try {
        while (offset < result.size()) {
            const ssize_t count = ::read(fd, result.data() + offset, result.size() - offset);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), std::string("read ") + purpose);
            }
            if (count == 0) throw std::runtime_error(std::string(purpose) + " is truncated");
            offset += static_cast<std::size_t>(count);
        }
        if (::close(fd) != 0) throw std::system_error(errno, std::generic_category(), std::string("close ") + purpose);
    } catch (...) {
        OPENSSL_cleanse(result.data(), result.size());
        (void)::close(fd);
        throw;
    }
    return result;
}

void write_new_private_file(const std::string& path, const Bytes& bytes, const char* purpose) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), std::string("create ") + purpose);
    bool open_fd = true;
    try {
        if (::fchmod(fd, 0600) != 0) throw std::system_error(errno, std::generic_category(), std::string("chmod ") + purpose);
        write_all(fd, bytes.data(), bytes.size());
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), std::string("fsync ") + purpose);
        if (::close(fd) != 0) { open_fd = false; throw std::system_error(errno, std::generic_category(), std::string("close ") + purpose); }
        open_fd = false;
        const std::filesystem::path target(path);
        const auto parent = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
        const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd >= 0) { (void)::fsync(dir_fd); (void)::close(dir_fd); }
    } catch (...) {
        if (open_fd) (void)::close(fd);
        (void)::unlink(path.c_str());
        throw;
    }
}

bool valid_cluster_id(const std::string& cluster_id) {
    if (cluster_id.empty() || cluster_id.size() > 128) return false;
    return std::all_of(cluster_id.begin(), cluster_id.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
    });
}

} // namespace

NodeIdentity::NodeIdentity(const PublicKey& public_key, const SecretKey& secret_key)
    : public_key_(public_key), secret_key_(secret_key), initialized_(true) {}

NodeIdentity NodeIdentity::generate() {
    auto pair = crypto::generate_ed25519_keypair();
    NodeIdentity identity(pair.public_key, pair.secret_key);
    OPENSSL_cleanse(pair.secret_key.data(), pair.secret_key.size());
    return identity;
}

NodeIdentity NodeIdentity::load(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open node identity file");
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0) {
        const int error = errno;
        (void)::close(fd);
        throw std::system_error(error, std::generic_category(), "stat node identity file");
    }
    if (!S_ISREG(metadata.st_mode)) {
        (void)::close(fd);
        throw std::runtime_error("node identity path must be a regular file");
    }
    if (metadata.st_uid != ::geteuid()) {
        (void)::close(fd);
        throw std::runtime_error("node identity file must be owned by the current user");
    }
    if ((metadata.st_mode & 0077) != 0) {
        (void)::close(fd);
        throw std::runtime_error("node identity permissions are too broad; require mode 0600");
    }
    if (metadata.st_size != 32) {
        (void)::close(fd);
        throw std::runtime_error("node identity file must contain exactly 32 bytes");
    }

    SecretKey secret{};
    std::size_t offset = 0;
    bool descriptor_open = true;
    try {
        while (offset < secret.size()) {
            const ssize_t count = ::read(fd, secret.data() + offset, secret.size() - offset);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::system_error(errno, std::generic_category(), "read node identity file");
            }
            if (count == 0) throw std::runtime_error("node identity file is truncated");
            offset += static_cast<std::size_t>(count);
        }
        Byte extra{};
        const ssize_t extra_count = ::read(fd, &extra, 1);
        if (extra_count != 0) throw std::runtime_error("node identity file has an invalid length");
        if (::close(fd) != 0) {
            const int error = errno;
            descriptor_open = false;
            throw std::system_error(error, std::generic_category(), "close node identity file");
        }
        descriptor_open = false;
        const PublicKey public_key = crypto::derive_ed25519_public_key(secret);
        NodeIdentity identity(public_key, secret);
        OPENSSL_cleanse(secret.data(), secret.size());
        return identity;
    } catch (...) {
        OPENSSL_cleanse(secret.data(), secret.size());
        if (descriptor_open) (void)::close(fd);
        throw;
    }
}

NodeIdentity::NodeIdentity(NodeIdentity&& other) noexcept
    : public_key_(other.public_key_), secret_key_(other.secret_key_), initialized_(other.initialized_) {
    OPENSSL_cleanse(other.secret_key_.data(), other.secret_key_.size());
    other.public_key_.fill(0);
    other.initialized_ = false;
}

NodeIdentity& NodeIdentity::operator=(NodeIdentity&& other) noexcept {
    if (this != &other) {
        OPENSSL_cleanse(secret_key_.data(), secret_key_.size());
        public_key_ = other.public_key_;
        secret_key_ = other.secret_key_;
        initialized_ = other.initialized_;
        OPENSSL_cleanse(other.secret_key_.data(), other.secret_key_.size());
        other.public_key_.fill(0);
        other.initialized_ = false;
    }
    return *this;
}

NodeIdentity::~NodeIdentity() {
    OPENSSL_cleanse(secret_key_.data(), secret_key_.size());
    public_key_.fill(0);
    initialized_ = false;
}

void NodeIdentity::save_new(const std::string& path) const {
    if (!initialized_) throw std::logic_error("cannot save an empty node identity");
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "create node identity file");
    bool keep_file = false;
    try {
        if (::fchmod(fd, 0600) != 0) {
            throw std::system_error(errno, std::generic_category(), "set node identity permissions");
        }
        write_all(fd, secret_key_.data(), secret_key_.size());
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "sync node identity file");
        keep_file = true;
    } catch (...) {
        (void)::close(fd);
        (void)::unlink(path.c_str());
        throw;
    }
    if (::close(fd) != 0 && keep_file) {
        (void)::unlink(path.c_str());
        throw std::system_error(errno, std::generic_category(), "close node identity file");
    }
}

bool NodeIdentity::is_encrypted_vault_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open node identity file");
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0) {
        const int error = errno; (void)::close(fd);
        throw std::system_error(error, std::generic_category(), "stat node identity file");
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0) {
        (void)::close(fd);
        throw std::runtime_error("node identity file must be a regular file owned by the current user with mode 0600");
    }
    std::array<Byte, 8> prefix{};
    std::size_t offset = 0;
    while (offset < prefix.size()) {
        const ssize_t count = ::read(fd, prefix.data() + offset, prefix.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            const int error = errno; (void)::close(fd);
            throw std::system_error(error, std::generic_category(), "read node identity format");
        }
        if (count == 0) break;
        offset += static_cast<std::size_t>(count);
    }
    (void)::close(fd);
    return offset == prefix.size() && prefix == kIdentityVaultMagic;
}

NodeIdentity NodeIdentity::load_vault(const std::string& path, const std::string& passphrase) {
    Bytes encoded = read_private_file(path, kIdentityVaultBytes, "encrypted node identity vault");
    try {
        ByteReader reader(encoded);
        const Bytes magic = reader.bytes(kIdentityVaultMagic.size());
        if (!std::equal(magic.begin(), magic.end(), kIdentityVaultMagic.begin())) {
            throw std::runtime_error("node identity vault magic/version mismatch");
        }
        crypto::EncryptedSecret encrypted{};
        encrypted.iterations = reader.u32();
        if (encrypted.iterations < 100'000 || encrypted.iterations > 5'000'000) {
            throw std::runtime_error("node identity vault PBKDF2 work factor is outside supported bounds");
        }
        encrypted.salt = reader.fixed<16>();
        encrypted.nonce = reader.fixed<12>();
        encrypted.ciphertext = reader.bytes(32);
        encrypted.tag = reader.fixed<16>();
        if (!reader.empty()) throw std::runtime_error("node identity vault contains trailing data");
        SecretKey secret = crypto::decrypt_secret(encrypted, passphrase);
        struct Wiper { SecretKey& value; ~Wiper() { OPENSSL_cleanse(value.data(), value.size()); } } wiper{secret};
        const PublicKey public_key = crypto::derive_ed25519_public_key(secret);
        return NodeIdentity(public_key, secret);
    } catch (...) {
        OPENSSL_cleanse(encoded.data(), encoded.size());
        throw;
    }
}

void NodeIdentity::save_vault_new(const std::string& path, const std::string& passphrase,
                                  std::uint32_t iterations) const {
    if (!initialized_) throw std::logic_error("cannot vault an empty node identity");
    const auto encrypted = crypto::encrypt_secret(secret_key_, passphrase, iterations);
    ByteWriter writer;
    writer.raw(kIdentityVaultMagic.data(), kIdentityVaultMagic.size());
    writer.u32(encrypted.iterations);
    writer.fixed(encrypted.salt);
    writer.fixed(encrypted.nonce);
    writer.raw(encrypted.ciphertext.data(), encrypted.ciphertext.size());
    writer.fixed(encrypted.tag);
    Bytes encoded = std::move(writer).take();
    try { write_new_private_file(path, encoded, "encrypted node identity vault"); }
    catch (...) { OPENSSL_cleanse(encoded.data(), encoded.size()); throw; }
    OPENSSL_cleanse(encoded.data(), encoded.size());
}

std::string NodeIdentity::peer_id() const {
    if (!initialized_) throw std::logic_error("node identity is not initialized");
    return peer_id_from_public_key(public_key_);
}

Signature NodeIdentity::sign(const Bytes& message) const {
    if (!initialized_) throw std::logic_error("node identity is not initialized");
    return crypto::sign_ed25519(secret_key_, message);
}

std::string peer_id_from_public_key(const PublicKey& public_key) {
    const Bytes bytes(public_key.begin(), public_key.end());
    const Hash digest = crypto::sha256(bytes);
    return "fn1" + crypto::hex(digest.data(), digest.size());
}

namespace detail {

Bytes authenticated_peer_session_transcript(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce) {
    PublicKey client_ephemeral{};
    PublicKey server_ephemeral{};
    return authenticated_peer_session_transcript(chain_parameters, client_id, server_id,
        client_nonce, server_nonce, client_ephemeral, server_ephemeral);
}

Bytes authenticated_peer_session_transcript(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce,
    const PublicKey& client_ephemeral, const PublicKey& server_ephemeral) {
    ByteWriter writer;
    writer.string("FeinStatic authenticated encrypted peer session v1");
    writer.blob(chain_parameters);
    writer.string(client_id);
    writer.string(server_id);
    writer.fixed(client_nonce);
    writer.fixed(server_nonce);
    writer.fixed(client_ephemeral);
    writer.fixed(server_ephemeral);
    return std::move(writer).take();
}

Hash authenticated_peer_session_binding(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce) {
    return crypto::sha256(authenticated_peer_session_transcript(
        chain_parameters, client_id, server_id, client_nonce, server_nonce));
}

Hash authenticated_peer_session_binding(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce,
    const PublicKey& client_ephemeral, const PublicKey& server_ephemeral) {
    return crypto::sha256(authenticated_peer_session_transcript(chain_parameters, client_id,
        server_id, client_nonce, server_nonce, client_ephemeral, server_ephemeral));
}

Bytes authenticated_frame_signing_message(
    const Hash& session_binding, std::uint16_t message_type,
    std::uint64_t sequence, const Bytes& payload) {
    ByteWriter writer;
    writer.string("FeinStatic authenticated TCP frame v2");
    writer.fixed(session_binding);
    writer.u32(message_type);
    writer.u64(sequence);
    writer.blob(payload);
    return std::move(writer).take();
}

} // namespace detail

void PeerTrustStore::trust(const PublicKey& public_key, PeerRole role) {
    const std::string id = peer_id_from_public_key(public_key);
    const auto existing = peers_.find(id);
    if (existing != peers_.end()) {
        if (existing->second.public_key != public_key) throw std::logic_error("peer identity hash collision in trust store");
        if (existing->second.revoked) throw std::logic_error("revoked peer identity cannot be reactivated; use identity rotation");
        existing->second.role = role;
        return;
    }
    if (peers_.size() >= 4096) throw std::length_error("peer trust store cannot exceed 4096 identities");
    peers_.emplace(id, Entry{public_key, role, false});
}

void PeerTrustStore::revoke(const std::string& peer_id) {
    const auto found = peers_.find(peer_id);
    if (found == peers_.end()) throw std::invalid_argument("cannot revoke an unknown peer identity");
    found->second.revoked = true;
}

void PeerTrustStore::rotate(const std::string& old_peer_id, const PublicKey& replacement_public_key) {
    const auto old = peers_.find(old_peer_id);
    if (old == peers_.end() || old->second.revoked) throw std::invalid_argument("old identity is unknown or already revoked");
    const auto new_id = peer_id_from_public_key(replacement_public_key);
    if (new_id == old_peer_id) throw std::invalid_argument("replacement identity must be different");
    if (peers_.contains(new_id)) throw std::invalid_argument("replacement identity already exists in the trust store");
    const PeerRole role = old->second.role;
    old->second.revoked = true;
    peers_.emplace(new_id, Entry{replacement_public_key, role, false});
}

bool PeerTrustStore::trusts(const std::string& peer_id) const noexcept {
    const auto found = peers_.find(peer_id);
    return found != peers_.end() && !found->second.revoked;
}

bool PeerTrustStore::is_operator(const std::string& peer_id) const noexcept {
    const auto found = peers_.find(peer_id);
    return found != peers_.end() && !found->second.revoked && found->second.role == PeerRole::Operator;
}

bool PeerTrustStore::is_revoked(const std::string& peer_id) const noexcept {
    const auto found = peers_.find(peer_id);
    return found != peers_.end() && found->second.revoked;
}

std::size_t PeerTrustStore::active_size() const noexcept {
    std::size_t count = 0;
    for (const auto& [id, entry] : peers_) { (void)id; if (!entry.revoked) ++count; }
    return count;
}

const PublicKey* PeerTrustStore::find(const std::string& peer_id) const noexcept {
    const auto found = peers_.find(peer_id);
    return found == peers_.end() || found->second.revoked ? nullptr : &found->second.public_key;
}

std::vector<TrustPolicyEntry> PeerTrustStore::policy_entries() const {
    std::vector<TrustPolicyEntry> result;
    result.reserve(peers_.size());
    for (const auto& [id, entry] : peers_) {
        result.push_back({id, entry.public_key, entry.role, entry.revoked});
    }
    return result;
}

Bytes PeerTrustStore::create_signed_policy(const std::string& cluster_id,
                                           std::uint64_t epoch,
                                           const NodeIdentity& signer) const {
    if (!valid_cluster_id(cluster_id)) throw std::invalid_argument("trust policy cluster ID must be 1-128 ASCII letters, digits, '.', '_' or '-'");
    if (epoch == 0 || epoch <= policy_epoch_) throw std::invalid_argument("trust policy epoch must be greater than the local policy epoch");
    if (!policy_cluster_id_.empty() && policy_cluster_id_ != cluster_id) {
        throw std::invalid_argument("trust store is already bound to a different cluster policy");
    }
    if (!is_operator(signer.peer_id())) throw std::invalid_argument("trust policy signer must be an active operator in the local trust store");
    ByteWriter body;
    body.string("FeinStatic signed trust policy v1");
    body.string(cluster_id);
    body.u64(epoch);
    body.string(signer.peer_id());
    body.u32(static_cast<std::uint32_t>(peers_.size()));
    for (const auto& [id, entry] : peers_) {
        body.string(id);
        body.fixed(entry.public_key);
        body.u8(static_cast<Byte>(entry.role));
        body.u8(entry.revoked ? 1U : 0U);
    }
    const Bytes unsigned_body = body.data();
    const Signature signature = signer.sign(unsigned_body);
    ByteWriter bundle;
    bundle.blob(unsigned_body);
    bundle.fixed(signature);
    Bytes result = std::move(bundle).take();
    if (result.size() > 1024U * 1024U) throw std::length_error("signed trust policy exceeds 1 MiB");
    return result;
}

void PeerTrustStore::apply_signed_policy(const Bytes& bundle,
                                         const std::string& expected_cluster_id,
                                         const std::string& authenticated_signer_peer_id) {
    if (!valid_cluster_id(expected_cluster_id)) throw std::invalid_argument("expected trust policy cluster ID is invalid");
    if (bundle.empty() || bundle.size() > 1024U * 1024U) throw std::length_error("signed trust policy size is invalid");
    ByteReader outer(bundle);
    const Bytes body = outer.blob(1024U * 1024U);
    const Signature signature = outer.fixed<64>();
    if (!outer.empty()) throw std::runtime_error("signed trust policy has trailing data");
    ByteReader reader(body);
    if (reader.string(64) != "FeinStatic signed trust policy v1") {
        throw std::runtime_error("signed trust policy format/version mismatch");
    }
    const std::string cluster_id = reader.string(128);
    const std::uint64_t epoch = reader.u64();
    const std::string signer_id = reader.string(128);
    const auto count = reader.u32();
    if (!valid_cluster_id(cluster_id) || cluster_id != expected_cluster_id) {
        throw std::runtime_error("signed trust policy belongs to a different Raft cluster");
    }
    if (epoch == 0 || epoch <= policy_epoch_) {
        throw std::runtime_error("signed trust policy is stale or replayed");
    }
    if (!policy_cluster_id_.empty() && policy_cluster_id_ != cluster_id) {
        throw std::runtime_error("local trust store is already bound to a different cluster policy");
    }
    if (signer_id != authenticated_signer_peer_id || !is_operator(authenticated_signer_peer_id)) {
        throw std::runtime_error("trust policy signer is not the authenticated active operator");
    }
    const PublicKey* signer_key = find(authenticated_signer_peer_id);
    if (signer_key == nullptr || !crypto::verify_ed25519(*signer_key, body, signature)) {
        throw std::runtime_error("trust policy signature is invalid");
    }
    if (count == 0 || count > 4096) throw std::length_error("trust policy identity count is outside supported bounds");
    std::map<std::string, Entry> replacement;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::string id = reader.string(128);
        const PublicKey public_key = reader.fixed<32>();
        const Byte role_value = reader.u8();
        const Byte revoked_value = reader.u8();
        if (role_value > static_cast<Byte>(PeerRole::Operator) || revoked_value > 1U) {
            throw std::runtime_error("trust policy contains an invalid role or status");
        }
        if (peer_id_from_public_key(public_key) != id) throw std::runtime_error("trust policy peer ID does not match its public key");
        if (!replacement.emplace(id, Entry{public_key, static_cast<PeerRole>(role_value), revoked_value == 1U}).second) {
            throw std::runtime_error("trust policy contains a duplicate peer identity");
        }
    }
    if (!reader.empty()) throw std::runtime_error("signed trust policy contains trailing body data");

    // Revocation is monotonic. A policy cannot reactivate a locally revoked ID,
    // and omission cannot erase its tombstone.
    for (const auto& [id, entry] : peers_) {
        if (!entry.revoked) continue;
        const auto candidate = replacement.find(id);
        if (candidate != replacement.end() && !candidate->second.revoked) {
            throw std::runtime_error("trust policy attempts to resurrect a revoked identity");
        }
        if (candidate == replacement.end()) replacement.emplace(id, entry);
    }
    const bool active_operator = std::any_of(replacement.begin(), replacement.end(), [](const auto& item) {
        return !item.second.revoked && item.second.role == PeerRole::Operator;
    });
    if (!active_operator) throw std::runtime_error("trust policy would remove every active operator identity");
    peers_ = std::move(replacement);
    policy_cluster_id_ = cluster_id;
    policy_epoch_ = epoch;
}

PeerTrustStore PeerTrustStore::load(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("unable to open peer trust store: " + path);
    const auto file_size = std::filesystem::file_size(path);
    if (file_size > 1024U * 1024U) throw std::length_error("peer trust store exceeds 1 MiB");
    PeerTrustStore result;
    std::string line;
    std::size_t line_number = 0;
    bool policy_metadata_seen = false;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.size() > 512) throw std::length_error("peer trust entry exceeds 512 bytes");
        if (line_number > 4096) throw std::length_error("peer trust store exceeds 4096 lines");
        line = trim(std::move(line));
        if (line.empty()) continue;
        if (line.rfind("# policy-v1 ", 0) == 0) {
            if (policy_metadata_seen) throw std::runtime_error("duplicate trust policy metadata at line " + std::to_string(line_number));
            std::istringstream metadata(line.substr(12));
            std::string cluster_hex, extra;
            std::uint64_t epoch = 0;
            if (!(metadata >> cluster_hex >> epoch) || (metadata >> extra)) {
                throw std::runtime_error("invalid trust policy metadata at line " + std::to_string(line_number));
            }
            if (cluster_hex != "-") {
                const Bytes decoded_cluster = crypto::unhex(cluster_hex);
                result.policy_cluster_id_.assign(decoded_cluster.begin(), decoded_cluster.end());
                if (!valid_cluster_id(result.policy_cluster_id_)) throw std::runtime_error("invalid trust policy cluster metadata");
            }
            result.policy_epoch_ = epoch;
            policy_metadata_seen = true;
            continue;
        }
        if (line.front() == '#') continue;
        std::istringstream row(line);
        std::string id, key_hex, role_text, extra;
        if (!(row >> id >> key_hex)) throw std::runtime_error("invalid peer trust entry at line " + std::to_string(line_number));
        const Bytes decoded = crypto::unhex(key_hex);
        if (decoded.size() != 32) throw std::runtime_error("peer public key must be 32 bytes at line " + std::to_string(line_number));
        PublicKey public_key{};
        std::copy(decoded.begin(), decoded.end(), public_key.begin());
        if (peer_id_from_public_key(public_key) != id) throw std::runtime_error("peer ID does not match public key at line " + std::to_string(line_number));
        if (result.peers_.contains(id)) throw std::runtime_error("duplicate peer identity at line " + std::to_string(line_number));
        bool revoked = false;
        PeerRole role = PeerRole::Peer;
        if (row >> role_text) {
            if (role_text == "operator") role = PeerRole::Operator;
            else if (role_text == "peer") role = PeerRole::Peer;
            else if (role_text == "revoked-operator") { role = PeerRole::Operator; revoked = true; }
            else if (role_text == "revoked-peer") { role = PeerRole::Peer; revoked = true; }
            else throw std::runtime_error("invalid peer role/status at line " + std::to_string(line_number));
        }
        if (row >> extra) throw std::runtime_error("unexpected trust store fields at line " + std::to_string(line_number));
        result.trust(public_key, role);
        if (revoked) result.revoke(id);
    }
    if (!input.eof()) throw std::runtime_error("error reading peer trust store");
    return result;
}

void PeerTrustStore::save_atomic(const std::string& path) const {
    const std::filesystem::path target(path);
    const std::filesystem::path temporary = target.string() + ".tmp." + std::to_string(::getpid());
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "create peer trust store temporary file");
    bool open_fd = true;
    try {
        std::string content = "# FeinStatic trust store v3: signed cluster trust policies\n";
        const Bytes cluster_bytes(policy_cluster_id_.begin(), policy_cluster_id_.end());
        content += "# policy-v1 " + (policy_cluster_id_.empty() ? std::string("-") : crypto::hex(cluster_bytes.data(), cluster_bytes.size())) + " " + std::to_string(policy_epoch_) + "\n";
        for (const auto& [id, entry] : peers_) {
            const std::string role = entry.revoked
                ? (entry.role == PeerRole::Operator ? "revoked-operator" : "revoked-peer")
                : (entry.role == PeerRole::Operator ? "operator" : "peer");
            content += id + " " + crypto::hex(entry.public_key.data(), entry.public_key.size()) + " " + role + "\n";
        }
        write_all(fd, reinterpret_cast<const Byte*>(content.data()), content.size());
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "sync peer trust store");
        if (::close(fd) != 0) { open_fd = false; throw std::system_error(errno, std::generic_category(), "close peer trust store"); }
        open_fd = false;
        if (::rename(temporary.c_str(), target.c_str()) != 0) throw std::system_error(errno, std::generic_category(), "replace peer trust store");
        const std::filesystem::path parent = target.has_parent_path() ? target.parent_path() : ".";
        const int directory_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory_fd >= 0) { (void)::fsync(directory_fd); (void)::close(directory_fd); }
    } catch (...) {
        if (open_fd) (void)::close(fd);
        (void)::unlink(temporary.c_str());
        throw;
    }
}

} // namespace feinstat::network
