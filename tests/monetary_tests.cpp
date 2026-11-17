#include "test.hpp"

#include "feinstat/monetary/engine.hpp"
#include "feinstat/monetary/types.hpp"

#include <cstdint>

using namespace feinstat;
using namespace feinstat::monetary;

namespace {

MonetaryParams test_params() {
    MonetaryParams p;
    p.genesis_supply = 1'000'000ULL * kAtomsPerCoin;
    p.initial_block_issuance = 1000ULL;
    p.tail_block_issuance = 10ULL;
    p.issuance_halving_interval = 10;
    p.max_supply = 2'000'000ULL * kAtomsPerCoin;
    p.base_fee_min = 1ULL;
    p.base_fee_max = 1'000'000ULL;
    p.target_utilization_ppm = 500'000;
    p.max_base_fee_change_ppm = 125'000;
    p.priority_fee_cap = 1'000'000ULL;
    return p;
}

} // namespace

FS_TEST(monetary_genesis_snapshot) {
    MonetaryEngine engine(test_params());
    const auto s = engine.genesis();
    FS_REQUIRE(s.height == 0);
    FS_REQUIRE(s.total_supply == 1'000'000ULL * kAtomsPerCoin);
    FS_REQUIRE(s.burned == 0);
    FS_REQUIRE(s.total_bonded_stake == 0);
    FS_REQUIRE(s.liquid_supply == 1'000'000ULL * kAtomsPerCoin);
    FS_REQUIRE(s.cumulative_issued == 0);
    FS_REQUIRE(engine.supply_conserved());
}

FS_TEST(monetary_issuance_declines_to_tail) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE(engine.issuance_for_block(0) == 1000ULL);
    FS_REQUIRE(engine.issuance_for_block(9) == 1000ULL);
    FS_REQUIRE(engine.issuance_for_block(10) == 500ULL);
    FS_REQUIRE(engine.issuance_for_block(19) == 500ULL);
    FS_REQUIRE(engine.issuance_for_block(20) == 250ULL);
    // Eventually floors at the tail emission.
    FS_REQUIRE(engine.issuance_for_block(100) == 10ULL);
    FS_REQUIRE(engine.issuance_for_block(1000) == 10ULL);
}

FS_TEST(monetary_apply_issuance_updates_supply) {
    MonetaryEngine engine(test_params());
    const Amount issued = engine.apply_issuance(5);
    FS_REQUIRE(issued == 1000ULL);
    const auto s = engine.snapshot(5);
    FS_REQUIRE(s.cumulative_issued == 1000ULL);
    FS_REQUIRE(s.total_supply == 1'000'000ULL * kAtomsPerCoin + 1000ULL);
    FS_REQUIRE(s.burned == 0);
    FS_REQUIRE(engine.supply_conserved());
}

FS_TEST(monetary_apply_issuance_accumulates) {
    MonetaryEngine engine(test_params());
    engine.apply_issuance(5);
    engine.apply_issuance(15); // 500 issuance
    const auto s = engine.snapshot(15);
    FS_REQUIRE(s.cumulative_issued == 1500ULL);
    FS_REQUIRE(s.total_supply == 1'000'000ULL * kAtomsPerCoin + 1500ULL);
    FS_REQUIRE(engine.supply_conserved());
}

FS_TEST(monetary_fees_burn_base_fee_reward_priority) {
    MonetaryEngine engine(test_params());
    engine.apply_fees(100ULL, 25ULL);
    const auto s = engine.snapshot(0);
    FS_REQUIRE(s.burned == 100ULL);
    FS_REQUIRE(s.total_supply == 1'000'000ULL * kAtomsPerCoin - 100ULL);
    FS_REQUIRE(engine.supply_conserved());
}

FS_TEST(monetary_priority_fee_cap_enforced) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE_THROWS(engine.apply_fees(0ULL, 2'000'000ULL));
}

FS_TEST(monetary_base_fee_underflow) {
    MonetaryEngine engine(test_params());
    engine.apply_issuance(0);
    FS_REQUIRE_THROWS(engine.apply_fees(2'000'000ULL * kAtomsPerCoin, 0ULL));
}

FS_TEST(monetary_adjust_base_fee_target_unchanged) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE(engine.adjust_base_fee(1000ULL, 500'000) == 1000ULL);
}

FS_TEST(monetary_adjust_base_fee_above_target_increases) {
    MonetaryEngine engine(test_params());
    // Utilization 100% (double target) -> +12.5%.
    const Amount adjusted = engine.adjust_base_fee(1000ULL, 1'000'000);
    FS_REQUIRE(adjusted > 1000ULL);
    FS_REQUIRE(adjusted <= 1'000'000ULL);
}

FS_TEST(monetary_adjust_base_fee_below_target_decreases) {
    MonetaryEngine engine(test_params());
    // Utilization 0% -> decrease by up to 12.5%.
    const Amount adjusted = engine.adjust_base_fee(1000ULL, 0);
    FS_REQUIRE(adjusted < 1000ULL);
    FS_REQUIRE(adjusted >= 1ULL);
}

FS_TEST(monetary_adjust_base_fee_clamped_to_min) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE(engine.adjust_base_fee(1ULL, 0) == 1ULL);
}

FS_TEST(monetary_adjust_base_fee_clamped_to_max) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE(engine.adjust_base_fee(1'000'000ULL, 1'000'000) ==
               1'000'000ULL);
}

FS_TEST(monetary_bond_unbond_slash) {
    MonetaryEngine engine(test_params());
    engine.apply_issuance(0); // supply = genesis + 1000
    engine.bond(500ULL);
    FS_REQUIRE(engine.snapshot(0).total_bonded_stake == 500ULL);
    FS_REQUIRE(engine.snapshot(0).liquid_supply ==
               1'000'000ULL * kAtomsPerCoin + 1000ULL - 500ULL);
    FS_REQUIRE(engine.supply_conserved());

    engine.unbond(200ULL);
    FS_REQUIRE(engine.snapshot(0).total_bonded_stake == 300ULL);
    FS_REQUIRE(engine.supply_conserved());

    engine.slash(100ULL);
    FS_REQUIRE(engine.snapshot(0).total_bonded_stake == 200ULL);
    FS_REQUIRE(engine.snapshot(0).burned == 100ULL);
    // Slash reduces supply (burned).
    FS_REQUIRE(engine.snapshot(0).total_supply ==
               1'000'000ULL * kAtomsPerCoin + 1000ULL - 100ULL);
    FS_REQUIRE(engine.supply_conserved());
}

FS_TEST(monetary_bond_insufficient_liquid_rejected) {
    MonetaryEngine engine(test_params());
    // Bond everything.
    FS_REQUIRE_THROWS(engine.bond(engine.snapshot(0).liquid_supply + 1ULL));
}

FS_TEST(monetary_unbond_exceeds_bonded_rejected) {
    MonetaryEngine engine(test_params());
    FS_REQUIRE_THROWS(engine.unbond(1ULL));
}

FS_TEST(monetary_supply_always_conserved) {
    MonetaryEngine engine(test_params());
    engine.apply_issuance(0);
    engine.apply_fees(100ULL, 25ULL);
    engine.bond(500ULL);
    engine.slash(50ULL);
    FS_REQUIRE(engine.supply_conserved());
    // Snapshot arithmetic consistent.
    const auto s = engine.snapshot(0);
    FS_REQUIRE(s.liquid_supply == s.total_supply - s.total_bonded_stake);
}

FS_TEST(monetary_max_supply_overflow_rejected) {
    // Cap supply at genesis: any issuance must overflow.
    MonetaryParams p = test_params();
    p.max_supply = p.genesis_supply;
    MonetaryEngine tiny(p);
    FS_REQUIRE_THROWS(tiny.apply_issuance(0));
}
