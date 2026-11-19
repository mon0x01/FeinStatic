#include "feinstat/consensus/abci/gossip.hpp"
#include "feinstat/consensus/abci/validator_peer_set.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/common/bytes.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace feinstat::consensus::abci {

using feinstat::network::NodeId;

namespace {

// Domain separation for gossip signatures: a gossip signature cannot be
// reused as a node-record signature, session handshake signature, or any
// other context.
constexpr std::string_view kGossipSigningDomain = "FST Consensus Gossip v1";

// ed25519 signatures are 64 bytes; that is the size of the signed prefix
// prepended to gossip payloads on the wire (signature || payload envelope).
constexpr std::size_t kEd25519SignatureSize = 64;

} // namespace

GossipProtocol::GossipProtocol(Config config) : config_(std::move(config)) {}

void GossipProtocol::set_send_fn(SendFn send_fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_fn_ = std::move(send_fn);
}

void GossipProtocol::set_peer_set(std::shared_ptr<ValidatorPeerSet> peer_set) {
    std::lock_guard<std::mutex> lock(mutex_);
    peer_set_ = std::move(peer_set);
}

void GossipProtocol::set_signing_key(
    std::shared_ptr<const feinstat::network::KeyProvider> key_provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    signing_key_ = std::move(key_provider);
}

std::string GossipProtocol::gossip(const Bytes& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    const NodeId sender = signing_key_ ? signing_key_->node_id() : NodeId{};
    const std::string message_id = compute_message_id(sender, payload);

    // Do not re-gossip a message we recently originated ourselves: the
    // sender-bound message ID doubles as sender authentication for our own
    // origin.
    if (seen_messages_.find(message_id) != seen_messages_.end()) {
        return message_id;
    }
    seen_messages_.emplace(message_id,
                           SeenMessage{message_id, std::chrono::steady_clock::now()});
    prune_seen_messages(); // issue (5): trigger TTL pruning on origin

    forward_unlocked(payload, message_id, config_.max_hops, NodeId{});
    return message_id;
}

bool GossipProtocol::on_message(const NodeId& from, const Bytes& payload,
                                const Bytes& signature, std::size_t hops_remaining) {
    (void)hops_remaining; // hop accounting is the caller's responsibility

    std::lock_guard<std::mutex> lock(mutex_);

    // Issue (4): authenticate the sender. The message ID is bound to the
    // sender (compute_message_id(from, payload)), and a known validator peer
    // must additionally produce a valid consensus-key signature over the
    // payload, so a peer cannot impersonate another or replay another peer's
    // message under its own identity.
    if (peer_set_ && !verify_signature_locked(from, payload, signature)) {
        return false;
    }

    const std::string message_id = compute_message_id(from, payload);
    auto it = seen_messages_.find(message_id);
    if (it != seen_messages_.end()) {
        ++duplicate_count_;
        prune_seen_messages(); // issue (5): trigger TTL pruning on receipt
        return false;
    }
    seen_messages_.emplace(message_id,
                           SeenMessage{message_id, std::chrono::steady_clock::now()});
    prune_seen_messages(); // issue (5): trigger TTL pruning on receipt
    return true;
}

std::size_t GossipProtocol::forward(const Bytes& payload,
                                    const std::string& message_id,
                                    std::size_t hops_remaining,
                                    const NodeId& exclude) {
    std::lock_guard<std::mutex> lock(mutex_);
    return forward_unlocked(payload, message_id, hops_remaining, exclude);
}

std::size_t GossipProtocol::forward_unlocked(
    const Bytes& payload, const std::string& message_id,
    std::size_t hops_remaining, const NodeId& exclude) {
    (void)message_id; // message_id is for the caller's dedup; forwarding
                      // targets are selected from the peer set
    if (hops_remaining == 0 || !send_fn_) {
        return 0;
    }

    // Build the wire envelope: signature || payload when a signing key is
    // configured, so every gossiped message carries sender authentication
    // beyond TLS.
    Bytes wire_payload = payload;
    if (signing_key_) {
        const Signature signature =
            signing_key_->sign_domain(std::string(kGossipSigningDomain), payload);
        wire_payload.insert(wire_payload.begin(), signature.begin(),
                            signature.end());
    }

    // Select up to fanout healthy (non-banned, non-offline) peers, excluding
    // `exclude` and never sending to ourselves.
    std::vector<NodeId> targets;
    const NodeId local = signing_key_ ? signing_key_->node_id() : NodeId{};
    if (peer_set_) {
        const std::vector<NodeId> healthy = peer_set_->healthy_peers();
        targets.reserve(std::min(healthy.size(), config_.fanout));
        for (const NodeId& peer : healthy) {
            if (peer == exclude || peer == local) {
                continue;
            }
            targets.push_back(peer);
            if (targets.size() >= config_.fanout) {
                break;
            }
        }
    }

    std::size_t sent = 0;
    for (const NodeId& peer : targets) {
        if (send_fn_(peer, wire_payload)) {
            ++sent;
        }
    }
    forwarded_count_ += sent;
    return sent;
}

[[nodiscard]] Bytes GossipProtocol::sign_payload(const Bytes& payload) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!signing_key_) {
        return {};
    }
    const Signature signature =
        signing_key_->sign_domain(std::string(kGossipSigningDomain), payload);
    return Bytes(signature.begin(), signature.end());
}

[[nodiscard]] bool GossipProtocol::verify_signature(
    const NodeId& from, const Bytes& payload, const Bytes& signature) const {
    std::shared_ptr<const ValidatorPeerSet> peer_set;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        peer_set = peer_set_;
    }
    if (!peer_set) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return verify_signature_locked(from, payload, signature);
}

[[nodiscard]] bool GossipProtocol::verify_signature_locked(
    const NodeId& from, const Bytes& payload, const Bytes& signature) const {
    if (!peer_set_) {
        return false;
    }
    if (signature.size() != kEd25519SignatureSize) {
        return false;
    }
    auto info = peer_set_->get_peer(from);
    if (!info) {
        return false; // unknown sender: cannot authenticate
    }
    Signature sig{};
    std::copy(signature.begin(), signature.end(), sig.begin());
    // Signatures are produced with domain separation (sign_domain over the
    // gossip domain prefix || payload), so reconstruct the signed message
    // before verifying.
    ByteWriter writer;
    writer.string(std::string(kGossipSigningDomain));
    writer.blob(payload);
    return crypto::verify_ed25519(info->consensus_key, std::move(writer).take(), sig);
}

[[nodiscard]] std::string GossipProtocol::compute_message_id(
    const NodeId& sender, const Bytes& payload) const {
    ByteWriter writer;
    writer.string(sender.value());
    writer.blob(payload);
    const Hash digest = crypto::sha256(std::move(writer).take());
    return crypto::hex(digest.data(), digest.size());
}

void GossipProtocol::prune_seen_messages() {
    const auto now = std::chrono::steady_clock::now();
    auto it = seen_messages_.begin();
    while (it != seen_messages_.end()) {
        if (now - it->second.received_at > config_.ttl) {
            it = seen_messages_.erase(it);
        } else {
            ++it;
        }
    }
}

[[nodiscard]] std::size_t GossipProtocol::seen_messages() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return seen_messages_.size();
}

[[nodiscard]] std::size_t GossipProtocol::forwarded_messages() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return forwarded_count_;
}

[[nodiscard]] std::size_t GossipProtocol::duplicate_messages() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return duplicate_count_;
}

} // namespace feinstat::consensus::abci
