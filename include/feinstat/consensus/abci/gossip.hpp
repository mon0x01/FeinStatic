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

class ValidatorPeerSet;

// Gossip protocol for validator peer-to-peer message dissemination.
//
// Consensus messages (proposals, prevotes, precommits, vote extensions) are
// gossiped to validator peers rather than sent point-to-point, so that no
// single peer is overwhelmed and the network remains connected even if some
// peers are unreachable.
//
// Each gossip message is identified by a content hash, deduplicated, and
// forwarded to a fanout of healthy peers with a hop limit and TTL. The
// protocol provides message authentication beyond TLS: outgoing messages may
// be signed with the local validator's consensus key, and incoming messages
// are verified against the sender's consensus key from the peer set.
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

    // Originate a gossip message with `payload`. Returns the message ID.
    // The message is signed (if a signing key is configured) and forwarded
    // to up to `fanout` healthy peers.
    std::string gossip(const Bytes& payload);

    // Handle an incoming gossip message from `from` with `hops_remaining`
    // hops left. Returns true if the message was new (not previously seen)
    // and should be processed by the application; false if it was a duplicate.
    // The application should call forward() to re-gossip new messages.
    bool on_message(const NodeId& from, const Bytes& payload,
                    std::size_t hops_remaining);

    // Forward a message to up to `fanout` healthy peers, excluding `exclude`.
    // Returns the number of peers the message was forwarded to.
    std::size_t forward(const Bytes& payload, const std::string& message_id,
                        std::size_t hops_remaining, const NodeId& exclude);

    // Sign a payload with the local validator's consensus key. Called before
    // sending a gossip message to provide message authentication beyond TLS.
    [[nodiscard]] Bytes sign_payload(const Bytes& payload) const;

    // Verify a signature over `payload` signed by the peer identified by
    // `from`. Returns true if the signature is valid.
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

    mutable std::mutex mutex_;
    std::map<std::string, SeenMessage> seen_messages_;
    std::size_t forwarded_count_{0};
    std::size_t duplicate_count_{0};

    [[nodiscard]] std::string compute_message_id(const NodeId& sender,
                                                 const Bytes& payload) const;
    void prune_seen_messages();
};

} // namespace feinstat::consensus::abci
