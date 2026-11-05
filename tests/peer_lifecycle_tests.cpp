#include "test.hpp"

#include <algorithm>
#include "feinstat/network/peer_lifecycle.hpp"

#include <chrono>
#include <string>

using namespace std::chrono_literals;
using feinstat::network::PeerLifecycle;
using feinstat::network::PeerLifecycleConfig;
using feinstat::network::PeerLifecycleState;

FS_TEST(peer_lifecycle_starts_unknown_and_becomes_healthy_after_success) {
    PeerLifecycle peer("peer-alpha");
    const auto now = PeerLifecycle::Clock::now();
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Unknown);
    FS_REQUIRE(peer.begin_attempt(now));
    FS_REQUIRE(!peer.begin_attempt(now));
    peer.record_success(now, 1000, 500);
    const auto state = peer.snapshot(now);
    FS_REQUIRE(state.state == PeerLifecycleState::Healthy);
    FS_REQUIRE(state.consecutive_failures == 0);
    FS_REQUIRE(state.latency_ewma_us == 500);
    FS_REQUIRE(state.last_success_unix_ms == 1000);
}

FS_TEST(peer_lifecycle_enters_degraded_then_offline_and_uses_half_open_probe) {
    PeerLifecycle peer("peer-beta");
    auto now = PeerLifecycle::Clock::now();
    for (std::uint32_t failure = 1; failure <= 4; ++failure) {
        FS_REQUIRE(peer.begin_attempt(now));
        const auto delay = peer.record_failure(now, 2000 + failure);
        FS_REQUIRE(delay >= 200ms);
        FS_REQUIRE(delay <= 30s);
        FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Degraded);
        FS_REQUIRE(!peer.can_attempt(now));
        now += delay;
    }
    FS_REQUIRE(peer.begin_attempt(now));
    const auto fifth_delay = peer.record_failure(now, 2005);
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Offline);
    FS_REQUIRE(!peer.can_attempt(now));
    now += fifth_delay;
    FS_REQUIRE(peer.begin_attempt(now));
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::HalfOpen);
    peer.record_success(now, 3000, 900);
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Healthy);
    FS_REQUIRE(peer.snapshot(now).consecutive_failures == 0);
}

FS_TEST(peer_lifecycle_backoff_is_bounded_and_peer_specific) {
    PeerLifecycle alpha("peer-alpha");
    PeerLifecycle beta("peer-beta");
    auto now = PeerLifecycle::Clock::now();
    std::chrono::milliseconds alpha_delay{0};
    std::chrono::milliseconds beta_delay{0};
    for (int attempt = 0; attempt < 12; ++attempt) {
        FS_REQUIRE(alpha.begin_attempt(now));
        FS_REQUIRE(beta.begin_attempt(now));
        alpha_delay = alpha.record_failure(now, 4000 + static_cast<std::uint64_t>(attempt));
        beta_delay = beta.record_failure(now, 5000 + static_cast<std::uint64_t>(attempt));
        FS_REQUIRE(alpha_delay <= 30s);
        FS_REQUIRE(beta_delay <= 30s);
        FS_REQUIRE(alpha_delay > 0ms);
        FS_REQUIRE(beta_delay > 0ms);
        now += std::max(alpha_delay, beta_delay);
    }
    FS_REQUIRE(alpha_delay != beta_delay);
}

FS_TEST(peer_lifecycle_disable_blocks_attempts_and_enable_resets_breaker) {
    PeerLifecycle peer("peer-gamma");
    auto now = PeerLifecycle::Clock::now();
    for (int i = 0; i < 5; ++i) {
        FS_REQUIRE(peer.begin_attempt(now));
        const auto delay = peer.record_failure(now, 6000 + static_cast<std::uint64_t>(i));
        now += delay;
    }
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Offline);
    peer.set_enabled(false);
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Disabled);
    FS_REQUIRE(!peer.can_attempt(now + 1h));
    peer.set_enabled(true);
    FS_REQUIRE(peer.snapshot(now).state == PeerLifecycleState::Unknown);
    FS_REQUIRE(peer.can_attempt(now));
}

FS_TEST(peer_lifecycle_validates_bounds) {
    FS_REQUIRE_THROWS(PeerLifecycle(""));
    PeerLifecycleConfig invalid;
    invalid.initial_backoff = 0ms;
    FS_REQUIRE_THROWS(PeerLifecycle("peer-delta", invalid));
    invalid = {};
    invalid.maximum_backoff = 10ms;
    FS_REQUIRE_THROWS(PeerLifecycle("peer-delta", invalid));
    invalid = {};
    invalid.jitter_percent = 51;
    FS_REQUIRE_THROWS(PeerLifecycle("peer-delta", invalid));
}
