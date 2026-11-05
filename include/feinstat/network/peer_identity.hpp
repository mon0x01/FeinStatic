#pragma once

#include "feinstat/common/types.hpp"

#include <map>
#include <optional>
#include <string>

namespace feinstat::network {

// A node identity is independent from Ledger customer wallets. Its secret is
// stored as a raw 32-byte Ed25519 seed in a mode-0600 file and is never written
// to a Ledger database or trust store.
class NodeIdentity {
public:
    static NodeIdentity generate();
    static NodeIdentity load(const std::string& private_key_path);
    static bool is_encrypted_vault_file(const std::string& private_key_path);
    static NodeIdentity load_vault(const std::string& private_key_path, const std::string& passphrase);

    NodeIdentity(const NodeIdentity&) = delete;
    NodeIdentity& operator=(const NodeIdentity&) = delete;
    NodeIdentity(NodeIdentity&& other) noexcept;
    NodeIdentity& operator=(NodeIdentity&& other) noexcept;
    ~NodeIdentity();

    void save_new(const std::string& private_key_path) const;
    void save_vault_new(const std::string& private_key_path, const std::string& passphrase,
                        std::uint32_t iterations = 310'000) const;
    [[nodiscard]] const PublicKey& public_key() const noexcept { return public_key_; }
    [[nodiscard]] std::string peer_id() const;
    [[nodiscard]] Signature sign(const Bytes& message) const;

private:
    NodeIdentity(const PublicKey& public_key, const SecretKey& secret_key);
    PublicKey public_key_{};
    SecretKey secret_key_{};
    bool initialized_{false};
};

enum class PeerRole : std::uint8_t { Peer = 0, Operator = 1 };

struct TrustPolicyEntry {
    std::string peer_id;
    PublicKey public_key{};
    PeerRole role{PeerRole::Peer};
    bool revoked{false};
};

// Explicit allow-list with authorization roles and retained revocation records.
// Two-column legacy rows load as ordinary peers; new rows carry a role/status.
class PeerTrustStore {
public:
    void trust(const PublicKey& public_key, PeerRole role = PeerRole::Peer);
    void revoke(const std::string& peer_id);
    void rotate(const std::string& old_peer_id, const PublicKey& replacement_public_key);
    [[nodiscard]] bool is_operator(const std::string& peer_id) const noexcept;
    [[nodiscard]] bool is_revoked(const std::string& peer_id) const noexcept;
    [[nodiscard]] bool trusts(const std::string& peer_id) const noexcept;
    [[nodiscard]] const PublicKey* find(const std::string& peer_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return peers_.size(); }
    [[nodiscard]] std::size_t active_size() const noexcept;
    [[nodiscard]] std::uint64_t policy_epoch() const noexcept { return policy_epoch_; }
    [[nodiscard]] const std::string& policy_cluster_id() const noexcept { return policy_cluster_id_; }
    [[nodiscard]] std::vector<TrustPolicyEntry> policy_entries() const;

    // A full-state policy is signed by an active operator. Application is
    // monotonic: epochs must increase and local revocation tombstones survive.
    [[nodiscard]] Bytes create_signed_policy(const std::string& cluster_id,
                                             std::uint64_t epoch,
                                             const NodeIdentity& signer) const;
    void apply_signed_policy(const Bytes& bundle,
                             const std::string& expected_cluster_id,
                             const std::string& authenticated_signer_peer_id);

    static PeerTrustStore load(const std::string& path);
    void save_atomic(const std::string& path) const;

private:
    struct Entry { PublicKey public_key{}; PeerRole role{PeerRole::Peer}; bool revoked{false}; };
    std::map<std::string, Entry> peers_;
    std::string policy_cluster_id_;
    std::uint64_t policy_epoch_{0};
};

[[nodiscard]] std::string peer_id_from_public_key(const PublicKey& public_key);

// Internal protocol primitives exposed under detail so the exact cryptographic
// transcript and frame-signing context can be regression-tested independently.
namespace detail {
[[nodiscard]] Bytes authenticated_peer_session_transcript(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce);
[[nodiscard]] Bytes authenticated_peer_session_transcript(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce,
    const PublicKey& client_ephemeral, const PublicKey& server_ephemeral);
[[nodiscard]] Hash authenticated_peer_session_binding(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce);
[[nodiscard]] Hash authenticated_peer_session_binding(
    const Bytes& chain_parameters, const std::string& client_id,
    const std::string& server_id, const Hash& client_nonce, const Hash& server_nonce,
    const PublicKey& client_ephemeral, const PublicKey& server_ephemeral);
[[nodiscard]] Bytes authenticated_frame_signing_message(
    const Hash& session_binding, std::uint16_t message_type,
    std::uint64_t sequence, const Bytes& payload);
} // namespace detail

} // namespace feinstat::network
