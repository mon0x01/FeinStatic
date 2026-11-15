#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_store.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::network {

class BootstrapDiscovery;

// PeerAuthenticator isolates session authentication from peer discovery.
// The discovery layer learns about peers through signed records; the
// authenticator decides whether a session may be established with a given
// identity. This keeps transport/session auth (existing NodeServer/PeerClient)
// separate from record-based discovery.
class PeerAuthenticator {
public:
    virtual ~PeerAuthenticator() = default;

    PeerAuthenticator() = default;
    PeerAuthenticator(const PeerAuthenticator&) = delete;
    PeerAuthenticator& operator=(const PeerAuthenticator&) = delete;
    PeerAuthenticator(PeerAuthenticator&&) = delete;
    PeerAuthenticator& operator=(PeerAuthenticator&&) = delete;

    // Returns the trusted public key for a node ID, if the peer is known and
    // verified. Used by session establishment to authorize an inbound or
    // outbound connection.
    [[nodiscard]] virtual std::optional<PublicKey> trusted_key(const NodeId& node_id) const = 0;

    // Verify that a public key matches a node ID and the peer is known.
    [[nodiscard]] virtual bool verify_peer_identity(const NodeId& node_id,
                                                     const PublicKey& public_key) const = 0;

    // Register a newly authenticated peer (called after a successful session
    // handshake). The peer is marked as verified in the store.
    virtual void register_authenticated_peer(const NodeId& node_id,
                                              const PublicKey& public_key,
                                              std::uint64_t now_unix_ms) = 0;

    // Check whether a node ID has been verified through a successful session.
    [[nodiscard]] virtual bool is_verified(const NodeId& node_id) const noexcept = 0;
};

// Store-backed PeerAuthenticator implementation. A peer is considered trusted
// when it has a validated signed record in the PeerStore; it becomes verified
// after a successful session handshake.
class StorePeerAuthenticator : public PeerAuthenticator {
public:
    StorePeerAuthenticator(PeerStore& store, Bytes network_identifier);

    [[nodiscard]] std::optional<PublicKey> trusted_key(const NodeId& node_id) const override;
    [[nodiscard]] bool verify_peer_identity(const NodeId& node_id,
                                             const PublicKey& public_key) const override;
    void register_authenticated_peer(const NodeId& node_id,
                                      const PublicKey& public_key,
                                      std::uint64_t now_unix_ms) override;
    [[nodiscard]] bool is_verified(const NodeId& node_id) const noexcept override;

private:
    PeerStore& store_;
    Bytes network_identifier_;
};

// Result of a peer-record exchange with a connected peer.
struct PeerExchangeResult {
    std::vector<SignedNodeRecord> learned_records; // Validated new records
    std::vector<NodeId> rejected_identities;       // Identities rejected during exchange
    std::vector<std::string> errors;               // Non-fatal error messages
};

// PeerExchange manages the exchange of signed peer records through authenticated
// connections. Connected peers share records of other peers they know, subject
// to rate limits, validation, and freshness checks. Records are claims about
// identity and reachability, not proof of correctness or maliciousness.
class PeerExchange {
public:
    struct Config {
        std::size_t max_records_per_exchange{32};
        std::size_t max_records_per_peer_per_hour{1024};
        std::uint64_t record_freshness_ttl_ms{24ULL * 60ULL * 60ULL * 1000ULL}; // 24 hours
    };

    PeerExchange(PeerStore& store, BootstrapDiscovery& bootstrap, PeerAuthenticator& authenticator,
                 Config config);

    // Exchange records with a peer over an authenticated connection.
    // `send_records` supplies records to send; `receive_records` supplies records
    // received from the peer. Returns validated records learned from the peer.
    PeerExchangeResult exchange(const NodeId& peer_node_id,
                                const std::vector<SignedNodeRecord>& send_records,
                                const std::vector<SignedNodeRecord>& receive_records,
                                std::uint64_t now_unix_ms);

    // Select records to share with a peer (bounded by config).
    [[nodiscard]] std::vector<SignedNodeRecord> select_records_to_share(
        const NodeId& peer_node_id, std::size_t max_records) const;

    [[nodiscard]] const PeerStore& store() const noexcept { return store_; }
    [[nodiscard]] PeerStore& store() noexcept { return store_; }

private:
    bool is_record_fresh(const SignedNodeRecord& record, std::uint64_t now_unix_ms) const;

    PeerStore& store_;
    BootstrapDiscovery& bootstrap_;
    PeerAuthenticator& authenticator_;
    Config config_;
    // Per-peer rate tracking: node_id -> (window_start, count)
    std::map<NodeId, std::pair<std::uint64_t, std::size_t>> exchange_rate_;
};

} // namespace feinstat::network
