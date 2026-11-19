#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>

namespace feinstat::consensus::abci {

using feinstat::network::NodeId;

// Per-peer token-bucket rate limiter for consensus networking.
//
// Each peer maintains an independent token bucket that refills at a
// configured rate. Sending or receiving a message consumes tokens
// proportional to the message size. When a peer's bucket is empty, the
// peer is rate-limited and its messages are rejected until tokens
// refill.
//
// This provides application-level rate limiting that complements TLS
// transport authentication: a peer that is authenticated via TLS can
// still be rate-limited if it floods the connection.
class PeerRateLimiter {
public:
    struct Config {
        // Tokens added per second (equivalent to messages per second for
        // 1-byte messages; larger messages consume more tokens).
        double tokens_per_second{100.0};
        // Maximum tokens a bucket can hold (burst allowance).
        std::size_t burst_allowance{10};
        // Maximum message size in bytes that a single message may consume.
        // Messages larger than this are always rejected.
        std::size_t max_single_message_bytes{1'000'000}; // 1 MB
    };

    explicit PeerRateLimiter(Config config);

    // Try to consume `message_bytes` worth of tokens from `peer`'s bucket.
    // Returns true if the message is allowed (tokens were consumed),
    // false if the peer is rate-limited (bucket empty).
    // The bucket state is updated only when the message is allowed.
    bool allow(const NodeId& peer, std::size_t message_bytes);

    // Record that `message_bytes` were exchanged with `peer`, consuming
    // tokens from its bucket. Equivalent to allow() but always consumes
    // tokens; used when the caller has already decided to accept the
    // message and only wants to update accounting.
    void record(const NodeId& peer, std::size_t message_bytes);

    // Peek at `peer`'s bucket without consuming tokens: returns true if a
    // message of `message_bytes` would be allowed by allow() right now
    // (bucket has enough tokens and the message is within the single-message
    // size limit), false otherwise. No bucket state is modified and no new
    // bucket is created. An unknown peer is evaluated against a fresh full
    // bucket.
    [[nodiscard]] bool peek(const NodeId& peer, std::size_t message_bytes) const;

    // Returns the currently available tokens for `peer` (0 if
    // rate-limited or unknown).
    [[nodiscard]] double available_tokens(const NodeId& peer) const;

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    struct Bucket {
        double tokens{0.0};
        std::chrono::steady_clock::time_point last_update;
    };

    Config config_;
    mutable std::mutex mutex_;
    std::map<NodeId, Bucket> buckets_;

    [[nodiscard]] Bucket& bucket_for(const NodeId& peer);
    void refill(Bucket& b) const;
};

} // namespace feinstat::consensus::abci
