#pragma once

#include "feinstat/staking/types.hpp"

#include <map>
#include <set>
#include <vector>

namespace feinstat::staking {

// Deterministic proof-of-stake state. All transitions are pure functions of
// the prior state and the supplied inputs/height (no wall clock, no unordered
// iteration, no floating point). Bonded stake and voting power are derived
// deterministically so every correct node computes an identical validator set.
//
// Implements FST-REQ-0037 validator entry & exit (ADR-0003 / D-016): epoch
// transitions with inactivity leak and epoch-based committee rotation, plus
// an evidence-based slashing flow (submit -> verify -> slash). Slashing is
// evidence-based only: provable protocol violations (equivocation) produce
// verifiable evidence that triggers deterministic slashing of bonded stake.
// References:
// - V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget",
//   arXiv:1710.09437, 2017 (slashing, nothing-at-stake, inactivity leak).
// - docs/fst/adr/adr-0003-permissionless-pos-validator-eligibility.md (ADR-0003).
// - docs/fst/adr/adr-0004-bft-consensus-cometbft-integration.md (ADR-0004).
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

    // Record validator activity (vote, bond, or other participation) at a
    // height. Updates last_active_height for inactivity-leak accounting.
    void record_activity(const OperatorKey& operator_key,
                         Height current_height);

    // Current epoch for a chain height: current_epoch = height / epoch_length.
    [[nodiscard]] Height current_epoch(Height current_height) const noexcept;

    // Epoch bookkeeping: current epoch, epoch length, height of the last
    // applied epoch transition.
    [[nodiscard]] EpochInfo epoch_info(Height current_height) const noexcept;

    // Advance the epoch if current_height begins a new epoch: applies the
    // inactivity leak to inactive bonded validators and rotates the active
    // committee (recomputed from bonded stake at the epoch boundary).
    [[nodiscard]] EpochInfo transition_epoch(Height current_height);

    // Submit and verify slashing evidence (evidence-based slashing only,
    // ADR-0003). Validates the evidence, rejects duplicate submissions,
    // verifies the validator's cryptographic signatures over the conflicting
    // votes, and slashes the equivocating validator by
    // equivocation_slash_basis_points of bonded stake. Throws StakeError for
    // invalid or duplicate evidence.
    [[nodiscard]] SlashingOutcome submit_slashing_evidence(
        const SlashingEvidence& evidence, Height current_height);

    // Slashing events applied to validators (observability only).
    [[nodiscard]] const std::vector<SlashingEvent>& slashing_events() const
        noexcept { return slashing_events_; }

    // Accessors.
    [[nodiscard]] const std::map<OperatorKey, Validator>& validators() const
        noexcept { return validators_; }
    [[nodiscard]] Validator* validator(
        const OperatorKey& operator_key) noexcept;
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

    // Epoch bookkeeping: last epoch for which transition_epoch() was applied.
    Height last_epoch_{0};
    // Height of the last applied epoch transition.
    Height last_transition_height_{0};
    // Canonical hashes of slashing evidence already submitted (rejects
    // duplicate submissions so a validator is slashed once per evidence).
    std::set<feinstat::Hash> submitted_evidence_;
    // Slashing events applied to validators (observability only).
    std::vector<SlashingEvent> slashing_events_;
};

} // namespace feinstat::staking
