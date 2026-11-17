#pragma once

#include "feinstat/staking/types.hpp"

#include <map>
#include <vector>

namespace feinstat::staking {

// Deterministic proof-of-stake state. All transitions are pure functions of
// the prior state and the supplied inputs/height (no wall clock, no unordered
// iteration, no floating point). Bonded stake and voting power are derived
// deterministically so every correct node computes an identical validator set.
class StakingState {
public:
    StakingState() = default;
    explicit StakingState(StakeParams params);

    // Register a new validator with an operator key and a consensus key.
    void register_validator(const OperatorKey& operator_key,
                            const ConsensusKey& consensus_key);

    // Bond `amount` to a validator (increases bonded stake and voting power).
    void bond(const OperatorKey& operator_key, Amount amount,
              Height current_height);

    // Unbond `amount` from a validator; stake locks until release height.
    void unbond(const OperatorKey& operator_key, Amount amount,
                Height current_height);

    // Withdraw unbonded stake whose release height has passed.
    void withdraw_unbonded(const OperatorKey& operator_key,
                           Height current_height);

    // Slash `amount` from a validator's bonded stake (evidence-based only).
    void slash(const OperatorKey& operator_key, Amount amount);

    // Rotate a validator's consensus key.
    void set_consensus_key(const OperatorKey& operator_key,
                           const ConsensusKey& new_consensus_key);

    // Delegate `amount` from a delegator to a validator.
    void delegate(const Delegator& delegator, const OperatorKey& operator_key,
                  Amount amount);

    // Undelegate `amount` from a validator (enters unbonding for the
    // delegator's stake).
    void undelegate(const Delegator& delegator, const OperatorKey& operator_key,
                    Amount amount, Height current_height);

    // Add to the global reward pool (issuance + priority fees).
    void add_reward(Amount amount);

    // Distribute the reward pool pro-rata to bonded stake and clear it.
    void distribute_rewards();

    // Recompute voting powers and the active validator set at a height.
    ValidatorSet validator_set(Height current_height) const;

    // The deterministic validator set updates to apply (CometBFT
    // activation-height semantics: updates returned at height H take effect
    // at H+2; the ABCI application applies them accordingly).
    std::vector<ValidatorUpdate> validator_updates(
        Height current_height) const;

    // Accessors.
    [[nodiscard]] const std::map<OperatorKey, Validator>& validators() const
        noexcept { return validators_; }
    [[nodiscard]] const Validator* validator(
        const OperatorKey& operator_key) const noexcept;
    [[nodiscard]] const Validator* validator_by_consensus(
        const ConsensusKey& consensus_key) const noexcept;
    [[nodiscard]] Amount total_bonded_stake() const noexcept {
        return total_bonded_stake_;
    }
    [[nodiscard]] Amount reward_pool() const noexcept { return reward_pool_; }
    [[nodiscard]] const StakeParams& params() const noexcept {
        return params_;
    }
    [[nodiscard]] StakeParams& params() noexcept { return params_; }

    // Concentration metrics: share of total bonded stake held by the top-k
    // operators and a Herfindahl-Hirschman-style index over voting power,
    // both in parts-per-million (0 .. 1'000'000). Observables only, not
    // consensus-critical.
    [[nodiscard]] std::uint64_t top_k_stake_share_ppm(
        std::uint32_t k) const;
    [[nodiscard]] std::uint64_t voting_power_hhi_ppm() const;

private:
    void recompute_voting_power(Validator& validator) noexcept;
    [[nodiscard]] VotingPower voting_power_from_stake(Amount stake) const
        noexcept;

    StakeParams params_;
    std::map<OperatorKey, Validator> validators_;
    std::map<ConsensusKey, OperatorKey> consensus_to_operator_;
    std::vector<UnbondingEntry> unbonding_queue_;
    std::map<Delegator, std::map<OperatorKey, Amount>> delegations_;
    Amount total_bonded_stake_{0};
    Amount reward_pool_{0};
    Height current_height_{0};
};

} // namespace feinstat::staking
