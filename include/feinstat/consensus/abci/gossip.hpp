#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace feinstat::consensus::abci {

using feinstat::network::NodeId;

class ValidatorPeerSet;

// Gossip protocol for validator peer-to-peer message dissemination.
//
// Consensus messages (proposals, prevotes, precommits, vote extensions) are
// gossiped to validator peers rather than sent point-to-point, so that no
// single peer is overwhelmed and the network remains connected even if some
// peers are unreachable.
//
// Each gossip message is identified by a content hash bound to its sender,
// deduplicated, and forwarded to a fanout of healthy peers with a hop limit
// and TTL. The protocol provides message authentication beyond TLS:
// outgoing messages are signed with the local validator's consensus key
// (if a signing key is configured) and incoming messages are verified
// against the sender's consensus key from the peer set.
//
// Wire envelope: when a signing key is configured, gossip()/forward() prepend
// the 64-byte ed25519 signature to the payload (signature || payload). The
// transport layer splits the envelope before calling on_message(), which
// receives payload and signature separately.
//
// The GossipProtocol is transport-agnostic: the caller provides a send
// callback that delivers bytes to a peer (e.g. over the Phase 2 P2P network
// or the ABCI socket). The protocol itself handles dedup, fanout, hop
// counting, and TTL.
class GossipProtocol {
public:
    // Callback used to deliver a gossip payload to a peer. Returns true if
    // the payload was queued for delivery, false on failure.
    using SendFn = std::function<bool(const NodeId& peer, const Bytes& payload)>;

    struct Config {
        // Number of healthy peers to forward each new message to.
        std::size_t fanout{3};
        // Maximum number of hops a message may traverse before being dropped.
        std::size_t max_hops{10};
        // Time-to-live for a gossip message; messages older than this are
        // pruned from the dedup cache.
        std::chrono::steady_clock::duration ttl{std::chrono::seconds(30)};
    };

    GossipProtocol(Config config);

    // Set the send callback used to forward messages to peers.
    void set_send_fn(SendFn send_fn);

    // Set the validator peer set used to select forwarding targets. The peer
    // set also provides consensus keys for message authentication.
    void set_peer_set(std::shared_ptr<ValidatorPeerSet> peer_set);

    // Set the consensus key provider used to sign outgoing gossip messages
    // and to identify the local sender in message IDs. Without a configured
    // key, messages are forwarded unsigned and the sender is an empty NodeId.
    void set_signing_key(
        std::shared_ptr<const feinstat::network::KeyProvider> key_provider);

    // Originate a gossip message with `payload`. Returns the message ID.
    // The message ID is bound to the local sender (compute_message_id) and
    // the message is signed (if a signing key is configured) and forwarded
    // to up to `fanout` healthy peers. A message originated recently (same
    // ID still in the dedup cache) is not re-forwarded.
    std::string gossip(const Bytes& payload);

    // Handle an incoming gossip message from `from` with `hops_remaining`
    // hops left. `signature` is the ed25519 signature over `payload` signed
    // by the sender's consensus key. Returns true if the message was new
    // (not previously seen and authentic when the sender is in the peer set)
    // and should be processed by the application; false if it was a duplicate
    // or failed authentication. The application should call forward() to
    // re-gossip new messages. When no peer set is configured, signature
    // verification is skipped and every new message is accepted.
    bool on_message(const NodeId& from, const Bytes& payload,
                    const Bytes& signature, std::size_t hops_remaining);

    // Forward a message to up to `fanout` healthy peers, excluding `exclude`.
    // The message is signed before delivery if a signing key is configured.
    // Returns the number of peers the message was forwarded to.
    std::size_t forward(const Bytes& payload, const std::string& message_id,
                        std::size_t hops_remaining, const NodeId& exclude);

    // Sign a payload with the local validator's consensus key. Called before
    // sending a gossip message to provide message authentication beyond TLS.
    // Returns an empty Bytes if no signing key is configured.
    [[nodiscard]] Bytes sign_payload(const Bytes& payload) const;

    // Verify a signature over `payload` signed by the peer identified by
    // `from`. Returns true if the signature is valid. Returns false if no
    // peer set is configured, the peer is unknown, or the signature is
    // malformed.
    [[nodiscard]] bool verify_signature(const NodeId& from, const Bytes& payload,
                                        const Bytes& signature) const;

    [[nodiscard]] std::size_t seen_messages() const noexcept;
    [[nodiscard]] std::size_t forwarded_messages() const noexcept;
    [[nodiscard]] std::size_t duplicate_messages() const noexcept;

private:
    struct SeenMessage {
        std::string id;
        std::chrono::steady_clock::time_point received_at;
    };

    Config config_;
    SendFn send_fn_;
    std::shared_ptr<ValidatorPeerSet> peer_set_;
    std::shared_ptr<const feinstat::network::KeyProvider> signing_key_;

    mutable std::mutex mutex_;
    std::map<std::string, SeenMessage> seen_messages_;
    std::size_t forwarded_count_{0};
    std::size_t duplicate_count_{0};

    [[nodiscard]] std::string compute_message_id(const NodeId& sender,
                                                 const Bytes& payload) const;
    void prune_seen_messages();

    // Requires mutex_ to be held by the caller. Verifies `signature` over
    // `payload` using the consensus key of peer `from` from the peer set.
    [[nodiscard]] bool verify_signature_locked(const NodeId& from,
                                               const Bytes& payload,
                                               const Bytes& signature) const;

    // Requires mutex_ to be held by the caller. Forwards `payload` (signed
    // into a signature || payload envelope when a signing key is configured)
    // to up to `fanout` healthy peers excluding `exclude`.
    std::size_t forward_unlocked(const Bytes& payload,
                                 const std::string& message_id,
                                 std::size_t hops_remaining,
                                 const NodeId& exclude);
};

} // namespace feinstat::consensus::abci
