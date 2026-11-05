#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace feinstat::network {

inline constexpr std::uint32_t kPeerOfflineFailureThreshold = 5;

enum class PeerLifecycleState : std::uint8_t {
    Unknown = 0,
    Healthy = 1,
    Degraded = 2,
    Offline = 3,
    HalfOpen = 4,
    Disabled = 5
};

[[nodiscard]] const char* peer_lifecycle_state_name(PeerLifecycleState state) noexcept;

struct PeerLifecycleConfig {
    std::chrono::milliseconds initial_backoff{250};
    std::chrono::milliseconds maximum_backoff{30000};
    // Deterministic peer-specific jitter in [0, jitter_percent]. This spreads
    // reconnects across peers while keeping unit/integration tests reproducible.
    std::uint32_t jitter_percent{20};
};

struct PeerLifecycleSnapshot {
    PeerLifecycleState state{PeerLifecycleState::Unknown};
    std::uint32_t consecutive_failures{0};
    std::uint64_t last_success_unix_ms{0};
    std::uint64_t last_failure_unix_ms{0};
    std::uint64_t retry_delay_ms{0};
    std::uint64_t latency_ewma_us{0};
    bool in_flight{false};
};

// Single-owner peer circuit breaker. The NodeServer protects each instance
// with its outbound queue mutex, so this class deliberately has no internal
// lock. A single in-flight operation per peer is the half-open probe.
class PeerLifecycle {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit PeerLifecycle(std::string peer_id, PeerLifecycleConfig config = {});

    [[nodiscard]] bool can_attempt(TimePoint now) const noexcept;
    [[nodiscard]] bool begin_attempt(TimePoint now) noexcept;
    void record_success(TimePoint now, std::uint64_t wall_time_ms, std::uint64_t latency_us) noexcept;
    [[nodiscard]] std::chrono::milliseconds record_failure(TimePoint now, std::uint64_t wall_time_ms) noexcept;
    void set_enabled(bool enabled) noexcept;

    [[nodiscard]] PeerLifecycleSnapshot snapshot(TimePoint now) const noexcept;
    [[nodiscard]] TimePoint next_attempt_at() const noexcept { return next_attempt_at_; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
    [[nodiscard]] std::chrono::milliseconds backoff_for_failure(std::uint32_t failures) const noexcept;

    std::string peer_id_;
    PeerLifecycleConfig config_;
    bool enabled_{true};
    bool has_success_{false};
    bool in_flight_{false};
    std::uint32_t consecutive_failures_{0};
    std::uint64_t last_success_unix_ms_{0};
    std::uint64_t last_failure_unix_ms_{0};
    std::uint64_t latency_ewma_us_{0};
    TimePoint next_attempt_at_{};
};

} // namespace feinstat::network
