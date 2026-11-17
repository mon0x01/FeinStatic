#include "test.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/staking/state.hpp"
#include "feinstat/staking/types.hpp"

#include <algorithm>
#include <cstdint>

using namespace feinstat;
using namespace feinstat::staking;

namespace {

Height h0 = 0;

feinstat::crypto::KeyPair make_kp() {
    return feinstat::crypto::generate_ed25519_keypair();
}

} // namespace

FS_TEST(staking_register_validator) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v != nullptr);
    FS_REQUIRE(v->consensus_key == cons.public_key);
    FS_REQUIRE(v->status == ValidatorStatus::Registered);
    FS_REQUIRE(v->bonded_stake == 0);
    FS_REQUIRE(v->voting_power == 0);
    FS_REQUIRE(state.validator_by_consensus(cons.public_key) != nullptr);
}

FS_TEST(staking_register_duplicate_rejected) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    FS_REQUIRE_THROWS(state.register_validator(op.public_key, cons.public_key));
}

FS_TEST(staking_register_duplicate_consensus_key_rejected) {
    StakingState state;
    const auto op1 = make_kp();
    const auto op2 = make_kp();
    const auto cons = make_kp();
    state.register_validator(op1.public_key, cons.public_key);
    FS_REQUIRE_THROWS(state.register_validator(op2.public_key, cons.public_key));
}

FS_TEST(staking_bond_increases_stake_and_power) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v != nullptr);
    FS_REQUIRE(v->status == ValidatorStatus::Bonded);
    FS_REQUIRE(v->bonded_stake == 1'000ULL * kAtomsPerCoin);
    FS_REQUIRE(v->voting_power == 1'000ULL * kAtomsPerCoin);
    FS_REQUIRE(state.total_bonded_stake() == 1'000ULL * kAtomsPerCoin);
}

FS_TEST(staking_bond_zero_rejected) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    FS_REQUIRE_THROWS(state.bond(op.public_key, 0, h0));
}

FS_TEST(staking_bond_unknown_validator_rejected) {
    StakingState state;
    const auto op = make_kp();
    FS_REQUIRE_THROWS(state.bond(op.public_key, kAtomsPerCoin, h0));
}

FS_TEST(staking_unbond_locks_stake) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    state.unbond(op.public_key, 400ULL * kAtomsPerCoin, h0);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v->bonded_stake == 600ULL * kAtomsPerCoin);
    FS_REQUIRE(v->voting_power == 600ULL * kAtomsPerCoin);
    FS_REQUIRE(state.total_bonded_stake() == 600ULL * kAtomsPerCoin);
}

FS_TEST(staking_unbond_exceeds_stake_rejected) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 100ULL * kAtomsPerCoin, h0);
    FS_REQUIRE_THROWS(
        state.unbond(op.public_key, 200ULL * kAtomsPerCoin, h0));
}

FS_TEST(staking_withdraw_unbonded_after_release) {
    StakingState state;
    state.params().unbonding_period = 10;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    state.unbond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    // Before release height: nothing changes in bonded stake (already
    // removed), but entry still locked.
    state.withdraw_unbonded(op.public_key, h0 + 5);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v->status == ValidatorStatus::Unbonded);
    // After release height: entry cleared.
    state.withdraw_unbonded(op.public_key, h0 + 10);
}

FS_TEST(staking_slash_reduces_stake_and_power) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    state.slash(op.public_key, 300ULL * kAtomsPerCoin);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v->bonded_stake == 700ULL * kAtomsPerCoin);
    FS_REQUIRE(v->voting_power == 700ULL * kAtomsPerCoin);
    FS_REQUIRE(state.total_bonded_stake() == 700ULL * kAtomsPerCoin);
}

FS_TEST(staking_slash_to_zero_unbonds) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    state.slash(op.public_key, 1'000ULL * kAtomsPerCoin);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v->status == ValidatorStatus::Unbonded);
    FS_REQUIRE(v->bonded_stake == 0);
    FS_REQUIRE(v->voting_power == 0);
}

FS_TEST(staking_slash_exceeds_stake_rejected) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 100ULL * kAtomsPerCoin, h0);
    FS_REQUIRE_THROWS(state.slash(op.public_key, 200ULL * kAtomsPerCoin));
}

FS_TEST(staking_set_consensus_key) {
    StakingState state;
    const auto op = make_kp();
    const auto cons1 = make_kp();
    const auto cons2 = make_kp();
    state.register_validator(op.public_key, cons1.public_key);
    state.set_consensus_key(op.public_key, cons2.public_key);
    FS_REQUIRE(state.validator_by_consensus(cons2.public_key) != nullptr);
    FS_REQUIRE(state.validator_by_consensus(cons1.public_key) == nullptr);
    FS_REQUIRE(state.validator(op.public_key)->consensus_key ==
               cons2.public_key);
}

FS_TEST(staking_validator_set_ordering_and_limit) {
    StakingState state;
    state.params().max_validators = 2;
    auto op1 = make_kp();
    auto cons1 = make_kp();
    auto op2 = make_kp();
    auto cons2 = make_kp();
    auto op3 = make_kp();
    auto cons3 = make_kp();
    state.register_validator(op1.public_key, cons1.public_key);
    state.register_validator(op2.public_key, cons2.public_key);
    state.register_validator(op3.public_key, cons3.public_key);
    state.bond(op1.public_key, 300ULL * kAtomsPerCoin, h0);
    state.bond(op2.public_key, 100ULL * kAtomsPerCoin, h0);
    state.bond(op3.public_key, 200ULL * kAtomsPerCoin, h0);

    const ValidatorSet vs = state.validator_set(h0);
    FS_REQUIRE(vs.updates.size() == 2); // max_validators = 2
    // op1 (300) and op3 (200) are active; op2 (100) is below the limit.
    std::vector<ConsensusKey> active_keys;
    for (const auto& u : vs.updates) {
        active_keys.push_back(u.consensus_key);
    }
    FS_REQUIRE(std::find(active_keys.begin(), active_keys.end(),
                         cons1.public_key) != active_keys.end());
    FS_REQUIRE(std::find(active_keys.begin(), active_keys.end(),
                         cons3.public_key) != active_keys.end());
    FS_REQUIRE(std::find(active_keys.begin(), active_keys.end(),
                         cons2.public_key) == active_keys.end());
    // Voting powers attached to the right keys.
    for (const auto& u : vs.updates) {
        if (u.consensus_key == cons1.public_key) {
            FS_REQUIRE(u.voting_power == 300ULL * kAtomsPerCoin);
        }
        if (u.consensus_key == cons3.public_key) {
            FS_REQUIRE(u.voting_power == 200ULL * kAtomsPerCoin);
        }
    }
    FS_REQUIRE(vs.total_voting_power == 500ULL * kAtomsPerCoin);
    // Updates sorted by consensus key ascending.
    FS_REQUIRE(vs.updates[0].consensus_key < vs.updates[1].consensus_key);
}

FS_TEST(staking_delegate_and_undelegate) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    const auto del = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    state.delegate(del.public_key, op.public_key, 500ULL * kAtomsPerCoin);
    const Validator* v = state.validator(op.public_key);
    FS_REQUIRE(v->voting_power == 1'500ULL * kAtomsPerCoin); // self + delegation
    FS_REQUIRE(state.total_bonded_stake() == 1'500ULL * kAtomsPerCoin);

    state.undelegate(del.public_key, op.public_key, 200ULL * kAtomsPerCoin, h0);
    FS_REQUIRE(v->voting_power == 1'300ULL * kAtomsPerCoin);
}

FS_TEST(staking_reward_pool) {
    StakingState state;
    state.add_reward(1'000ULL * kAtomsPerCoin);
    FS_REQUIRE(state.reward_pool() == 1'000ULL * kAtomsPerCoin);
    state.distribute_rewards();
    FS_REQUIRE(state.reward_pool() == 0);
}

FS_TEST(staking_concentration_metrics) {
    StakingState state;
    const auto op = make_kp();
    const auto cons = make_kp();
    state.register_validator(op.public_key, cons.public_key);
    state.bond(op.public_key, 1'000ULL * kAtomsPerCoin, h0);
    // Single validator holds 100%.
    FS_REQUIRE(state.top_k_stake_share_ppm(1) == 1'000'000ULL);
    FS_REQUIRE(state.voting_power_hhi_ppm() == 1'000'000ULL);
}

FS_TEST(staking_empty_state) {
    StakingState state;
    const ValidatorSet vs = state.validator_set(h0);
    FS_REQUIRE(vs.updates.empty());
    FS_REQUIRE(state.total_bonded_stake() == 0);
    FS_REQUIRE(state.voting_power_hhi_ppm() == 0);
    FS_REQUIRE(state.top_k_stake_share_ppm(5) == 0);
}
