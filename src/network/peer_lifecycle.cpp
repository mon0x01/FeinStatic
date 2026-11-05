#include "feinstat/network/peer_lifecycle.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace feinstat::network {
const char* peer_lifecycle_state_name(PeerLifecycleState state) noexcept {
    switch (state) {
    case PeerLifecycleState::Unknown: return "UNKNOWN";
    case PeerLifecycleState::Healthy: return "HEALTHY";
    case PeerLifecycleState::Degraded: return "DEGRADED";
    case PeerLifecycleState::Offline: return "OFFLINE";
    case PeerLifecycleState::HalfOpen: return "HALF_OPEN";
    case PeerLifecycleState::Disabled: return "DISABLED";
    }
    return "UNKNOWN";
}

PeerLifecycle::PeerLifecycle(std::string peer_id, PeerLifecycleConfig config)
    : peer_id_(std::move(peer_id)), config_(config) {
    if (peer_id_.empty() || peer_id_.size() > 128 ||
        config_.initial_backoff <= std::chrono::milliseconds::zero() ||
        config_.maximum_backoff < config_.initial_backoff ||
        config_.maximum_backoff > std::chrono::minutes(5) ||
        config_.jitter_percent > 50) {
        throw std::invalid_argument("peer lifecycle configuration is outside safe bounds");
    }
}

bool PeerLifecycle::can_attempt(TimePoint now) const noexcept {
    return enabled_ && !in_flight_ && now >= next_attempt_at_;
}

bool PeerLifecycle::begin_attempt(TimePoint now) noexcept {
    if (!can_attempt(now)) return false;
    in_flight_ = true;
    return true;
}

void PeerLifecycle::record_success(TimePoint, std::uint64_t wall_time_ms, std::uint64_t latency_us) noexcept {
    in_flight_ = false;
    consecutive_failures_ = 0;
    next_attempt_at_ = TimePoint{};
    last_success_unix_ms_ = wall_time_ms;
    has_success_ = true;
    if (latency_ewma_us_ == 0) {
        latency_ewma_us_ = latency_us;
    } else {
        // Integer EWMA: 80% previous estimate, 20% newest observation.
        latency_ewma_us_ = (latency_ewma_us_ * 4U + latency_us) / 5U;
    }
}

std::chrono::milliseconds PeerLifecycle::backoff_for_failure(std::uint32_t failures) const noexcept {
    const auto initial = static_cast<std::uint64_t>(config_.initial_backoff.count());
    const auto maximum = static_cast<std::uint64_t>(config_.maximum_backoff.count());
    const std::uint32_t shift = std::min<std::uint32_t>(failures > 0 ? failures - 1U : 0U, 31U);
    std::uint64_t base = initial;
    if (shift >= 63U || base > (maximum >> shift)) base = maximum;
    else base = std::min(maximum, base << shift);

    // Stable FNV-1a mixing of peer identity and failure generation. This
    // produces different retry windows for peers without synchronized storms.
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char character : peer_id_) {
        const auto byte = static_cast<unsigned char>(character);
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(failures) * 0x9e3779b97f4a7c15ULL;
    hash ^= hash >> 30U;
    hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27U;
    const std::uint64_t span = static_cast<std::uint64_t>(config_.jitter_percent) * 2U + 1U;
    const std::uint64_t offset = span == 0 ? 0 : hash % span;
    const std::int64_t signed_percent = static_cast<std::int64_t>(offset) -
        static_cast<std::int64_t>(config_.jitter_percent);
    const std::int64_t adjusted = static_cast<std::int64_t>(base) +
        (static_cast<std::int64_t>(base) * signed_percent) / 100;
    const auto bounded = std::clamp<std::int64_t>(adjusted, 1, static_cast<std::int64_t>(maximum));
    return std::chrono::milliseconds(bounded);
}

std::chrono::milliseconds PeerLifecycle::record_failure(TimePoint now, std::uint64_t wall_time_ms) noexcept {
    in_flight_ = false;
    last_failure_unix_ms_ = wall_time_ms;
    if (consecutive_failures_ != std::numeric_limits<std::uint32_t>::max()) {
        ++consecutive_failures_;
    }
    const auto delay = backoff_for_failure(consecutive_failures_);
    next_attempt_at_ = enabled_ ? now + delay : TimePoint::max();
    return delay;
}

void PeerLifecycle::set_enabled(bool enabled) noexcept {
    enabled_ = enabled;
    if (!enabled) {
        next_attempt_at_ = TimePoint::max();
        return;
    }
    consecutive_failures_ = 0;
    next_attempt_at_ = TimePoint{};
    has_success_ = false;
    last_success_unix_ms_ = 0;
    latency_ewma_us_ = 0;
}

PeerLifecycleSnapshot PeerLifecycle::snapshot(TimePoint now) const noexcept {
    PeerLifecycleSnapshot result;
    result.consecutive_failures = consecutive_failures_;
    result.last_success_unix_ms = last_success_unix_ms_;
    result.last_failure_unix_ms = last_failure_unix_ms_;
    result.latency_ewma_us = latency_ewma_us_;
    result.in_flight = in_flight_;
    if (!enabled_) {
        result.state = PeerLifecycleState::Disabled;
    } else if (in_flight_ && consecutive_failures_ >= kPeerOfflineFailureThreshold) {
        result.state = PeerLifecycleState::HalfOpen;
    } else if (consecutive_failures_ >= kPeerOfflineFailureThreshold) {
        result.state = PeerLifecycleState::Offline;
    } else if (consecutive_failures_ > 0) {
        result.state = PeerLifecycleState::Degraded;
    } else if (has_success_) {
        result.state = PeerLifecycleState::Healthy;
    } else {
        result.state = PeerLifecycleState::Unknown;
    }
    if (next_attempt_at_ > now && next_attempt_at_ != TimePoint::max()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next_attempt_at_ - now).count();
        result.retry_delay_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(1, remaining));
    }
    return result;
}

} // namespace feinstat::network
