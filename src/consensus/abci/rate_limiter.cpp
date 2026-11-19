#include "feinstat/consensus/abci/rate_limiter.hpp"

#include <algorithm>
#include <chrono>

namespace feinstat::consensus::abci {

using feinstat::network::NodeId;

PeerRateLimiter::PeerRateLimiter(Config config) : config_(std::move(config)) {}

bool PeerRateLimiter::allow(const NodeId& peer, std::size_t message_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& b = bucket_for(peer);
    refill(b);
    // A message larger than the single-message limit is always rejected,
    // even with a full bucket.
    if (message_bytes > config_.max_single_message_bytes) {
        return false;
    }
    const double cost = static_cast<double>(message_bytes);
    if (b.tokens < cost) {
        // Bucket empty: rate-limited. No state is modified so a failed
        // check never consumes tokens.
        return false;
    }
    b.tokens -= cost;
    return true;
}

void PeerRateLimiter::record(const NodeId& peer, std::size_t message_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& b = bucket_for(peer);
    refill(b);
    const double cost = static_cast<double>(message_bytes);
    if (b.tokens >= cost) {
        b.tokens -= cost;
    } else {
        // Not enough tokens: drain to zero rather than going negative.
        b.tokens = 0.0;
    }
}

[[nodiscard]] double PeerRateLimiter::available_tokens(const NodeId& peer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buckets_.find(peer);
    if (it == buckets_.end()) {
        return 0.0; // unknown peer: no accounting yet
    }
    Bucket b = it->second;
    refill(b);
    return b.tokens;
}

[[nodiscard]] bool PeerRateLimiter::peek(const NodeId& peer,
                                         std::size_t message_bytes) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (message_bytes > config_.max_single_message_bytes) {
        return false;
    }
    const double cost = static_cast<double>(message_bytes);
    auto it = buckets_.find(peer);
    if (it == buckets_.end()) {
        // Unknown peer: evaluate against a fresh full bucket without
        // creating one.
        return static_cast<double>(config_.burst_allowance) >= cost;
    }
    Bucket b = it->second;
    refill(b);
    return b.tokens >= cost;
}

PeerRateLimiter::Bucket& PeerRateLimiter::bucket_for(const NodeId& peer) {
    auto it = buckets_.find(peer);
    if (it == buckets_.end()) {
        Bucket b;
        b.tokens = static_cast<double>(config_.burst_allowance);
        b.last_update = std::chrono::steady_clock::now();
        it = buckets_.emplace(peer, std::move(b)).first;
    }
    return it->second;
}

void PeerRateLimiter::refill(Bucket& b) const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - b.last_update;
    if (elapsed <= std::chrono::steady_clock::duration::zero()) {
        return;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    b.tokens += seconds * config_.tokens_per_second;
    const double cap = static_cast<double>(config_.burst_allowance);
    if (b.tokens > cap) {
        b.tokens = cap;
    }
    b.last_update = now;
}

} // namespace feinstat::consensus::abci
