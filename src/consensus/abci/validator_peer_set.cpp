#include "feinstat/consensus/abci/validator_peer_set.hpp"

#include <algorithm>

namespace feinstat::consensus::abci {

using feinstat::network::NodeId;

namespace {

// Maximum rate-limit violations before a peer is banned for DoS mitigation.
constexpr std::uint64_t kBanViolationThreshold = 5;
// A peer not seen within this window is transitioned to Offline.
constexpr std::uint64_t kOfflineTimeoutUnixMs = 5ULL * 60ULL * 1000ULL; // 5 minutes

std::uint64_t now_unix_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace

void ValidatorPeerSet::add_peer(const NodeId& node_id, const PublicKey& consensus_key,
                                std::int64_t voting_power) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        PeerInfo info;
        info.node_id = node_id;
        info.consensus_key = consensus_key;
        info.voting_power = voting_power;
        info.health = Health::Healthy;
        info.last_seen_unix_ms = now_unix_ms();
        peers_.emplace(node_id, std::move(info));
    } else {
        // Update key and voting power; preserve health, counters, and the
        // leader flag of an existing peer.
        it->second.consensus_key = consensus_key;
        it->second.voting_power = voting_power;
    }
}

void ValidatorPeerSet::remove_peer(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.erase(node_id);
    if (leader_ && *leader_ == node_id) {
        leader_ = std::nullopt;
    }
}

void ValidatorPeerSet::ban_peer(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    it->second.health = Health::Banned;
}

void ValidatorPeerSet::set_healthy(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    it->second.health = Health::Healthy;
}

void ValidatorPeerSet::set_suspect(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    it->second.health = Health::Suspect;
}

void ValidatorPeerSet::set_offline(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    it->second.health = Health::Offline;
}

void ValidatorPeerSet::message_received(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    it->second.last_seen_unix_ms = now_unix_ms();
    ++it->second.messages_received;
}

void ValidatorPeerSet::message_sent(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    ++it->second.messages_sent;
}

void ValidatorPeerSet::rate_limit_violation(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return;
    }
    PeerInfo& peer = it->second;
    ++peer.rate_limit_violations;
    if (peer.rate_limit_violations >= kBanViolationThreshold) {
        // DoS mitigation: ban the peer once the violation threshold is
        // reached. Banned peers are excluded from gossip forwarding and
        // their messages are rejected by on_message().
        peer.health = Health::Banned;
    } else if (peer.health == Health::Healthy) {
        peer.health = Health::Suspect;
    }
}

void ValidatorPeerSet::set_leader(const NodeId& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    leader_ = node_id;
    for (auto& [nid, peer] : peers_) {
        peer.is_leader = (nid == node_id);
    }
}

void ValidatorPeerSet::clear_leader() {
    std::lock_guard<std::mutex> lock(mutex_);
    leader_ = std::nullopt;
    for (auto& [nid, peer] : peers_) {
        peer.is_leader = false;
    }
}

[[nodiscard]] std::shared_ptr<const ValidatorPeerSet::PeerInfo>
ValidatorPeerSet::get_peer(const NodeId& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) {
        return nullptr;
    }
    return std::make_shared<const PeerInfo>(it->second);
}

[[nodiscard]] std::vector<ValidatorPeerSet::PeerInfo> ValidatorPeerSet::peers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto& [nid, peer] : peers_) {
        result.push_back(peer);
    }
    return result;
}

[[nodiscard]] std::vector<NodeId> ValidatorPeerSet::healthy_peers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeId> result;
    for (const auto& [nid, peer] : peers_) {
        if (peer.health != Health::Banned && peer.health != Health::Offline) {
            result.push_back(nid);
        }
    }
    return result;
}

[[nodiscard]] std::vector<NodeId> ValidatorPeerSet::banned_peers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeId> result;
    for (const auto& [nid, peer] : peers_) {
        if (peer.health == Health::Banned) {
            result.push_back(nid);
        }
    }
    return result;
}

[[nodiscard]] std::optional<NodeId> ValidatorPeerSet::leader() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return leader_;
}

[[nodiscard]] bool ValidatorPeerSet::is_peer(const NodeId& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_.find(node_id) != peers_.end();
}

void ValidatorPeerSet::tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t now_ms = now_unix_ms();
    for (auto& [node_id, peer] : peers_) {
        if (peer.health == Health::Banned) {
            continue; // bans persist until explicitly lifted or removed
        }
        if (peer.rate_limit_violations >= kBanViolationThreshold) {
            peer.health = Health::Banned;
            continue;
        }
        const bool absent =
            now_ms - peer.last_seen_unix_ms > kOfflineTimeoutUnixMs;
        if (peer.rate_limit_violations > 0) {
            // Healthy -> Suspect on violations; Suspect -> Offline when
            // absent.
            peer.health = absent ? Health::Offline : Health::Suspect;
        } else if (absent) {
            // Healthy -> Offline when not seen within the timeout.
            peer.health = Health::Offline;
        }
    }
}

} // namespace feinstat::consensus::abci
