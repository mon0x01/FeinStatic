#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace feinstat::consensus::abci {

// Validator peer set: tracks the set of validator peers known to this node,
// their consensus keys, voting power, and health status.
//
// Peers are added when they appear in the validator set (e.g. via InitChain
// or validator updates from CometBFT) and removed when they exit the validator
// set or are banned for misbehavior. Health tracking supports DoS mitigation
// by allowing the gossip protocol and rate limiter to avoid or deprioritize
// unhealthy peers.
class ValidatorPeerSet {
public:
    enum class Health : std::uint8_t {
        Healthy = 0,   // Peer is participating normally
        Suspect = 1,   // Peer is behaving suspiciously (rate-limit violations)
        Offline = 2,   // Peer has not been seen recently
        Banned = 3,    // Peer is banned (DoS mitigation)
    };

    struct PeerInfo {
        NodeId node_id;
        PublicKey consensus_key;
        std::int64_t voting_power{0};
        Health health{Health::Healthy};
        std::uint64_t last_seen_unix_ms{0};
        std::uint64_t messages_received{0};
        std::uint64_t messages_sent{0};
        std::uint64_t rate_limit_violations{0};
        bool is_leader{false};
    };

    ValidatorPeerSet() = default;

    // Add or update a validator peer. If the peer already exists, its
    // consensus key and voting power are updated and its health is preserved.
    void add_peer(const NodeId& node_id, const PublicKey& consensus_key,
                  std::int64_t voting_power);

    // Remove a validator peer (e.g. when it exits the validator set).
    void remove_peer(const NodeId& node_id);

    // Ban a peer for DoS mitigation. A banned peer is excluded from gossip
    // forwarding targets and its messages are rejected.
    void ban_peer(const NodeId& node_id);

    void set_healthy(const NodeId& node_id);
    void set_suspect(const NodeId& node_id);
    void set_offline(const NodeId& node_id);

    // Record a message received from a peer (updates last_seen and counters).
    void message_received(const NodeId& node_id);

    // Record a message sent to a peer.
    void message_sent(const NodeId& node_id);

    // Record a rate-limit violation by a peer.
    void rate_limit_violation(const NodeId& node_id);

    // Mark a peer as the current leader (block proposer). Only one peer may
    // be the leader at a time; calling set_leader() clears the previous leader.
    void set_leader(const NodeId& node_id);

    // Clear the leader mark from all peers.
    void clear_leader();

    // Look up a peer by node ID. Returns null if the peer is not in the set.
    [[nodiscard]] std::shared_ptr<const PeerInfo> get_peer(const NodeId& node_id) const;

    // Return a copy of all peer info entries.
    [[nodiscard]] std::vector<PeerInfo> peers() const;

    // Return the node IDs of healthy (non-banned, non-offline) peers.
    [[nodiscard]] std::vector<NodeId> healthy_peers() const;

    // Return the node IDs of banned peers.
    [[nodiscard]] std::vector<NodeId> banned_peers() const;

    // Return the current leader's node ID, if any.
    [[nodiscard]] std::optional<NodeId> leader() const;

    // Return true if `node_id` is a known validator peer.
    [[nodiscard]] bool is_peer(const NodeId& node_id) const;

    [[nodiscard]] std::size_t size() const noexcept { return peers_.size(); }

private:
    mutable std::mutex mutex_;
    std::map<NodeId, PeerInfo> peers_;
    std::optional<NodeId> leader_;
};

} // namespace feinstat::consensus::abci
