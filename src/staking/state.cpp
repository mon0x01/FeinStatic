#include "feinstat/staking/state.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace feinstat::staking {

StakingState::StakingState(StakeParams params) : params_(std::move(params)) {}

const Validator* StakingState::validator(
    const OperatorKey& operator_key) const noexcept {
    const auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Validator* StakingState::validator_by_consensus(
    const ConsensusKey& consensus_key) const noexcept {
    const auto it = consensus_to_operator_.find(consensus_key);
    if (it == consensus_to_operator_.end()) {
        return nullptr;
    }
    return validator(it->second);
}

VotingPower StakingState::voting_power_from_stake(Amount stake) const
    noexcept {
    return static_cast<VotingPower>(stake);
}

void StakingState::recompute_voting_power(Validator& validator) noexcept {
    Amount delegated = 0;
    // delegations_ is keyed by delegator; find all delegating to this operator.
    for (const auto& [delegator, targets] : delegations_) {
        const auto target_it = targets.find(validator.operator_key);
        if (target_it != targets.end()) {
            delegated += target_it->second;
        }
    }
    const Amount effective = validator.bonded_stake + delegated;
    validator.voting_power = voting_power_from_stake(effective);
}

void StakingState::register_validator(const OperatorKey& operator_key,
                                      const ConsensusKey& consensus_key) {
    if (validators_.find(operator_key) != validators_.end()) {
        throw StakeError(StakeErrorCode::validator_already_registered,
                         "validator already registered");
    }
    if (consensus_to_operator_.find(consensus_key) !=
        consensus_to_operator_.end()) {
        throw StakeError(StakeErrorCode::invalid_consensus_key,
                         "consensus key already in use");
    }
    Validator v;
    v.operator_key = operator_key;
    v.consensus_key = consensus_key;
    v.status = ValidatorStatus::Registered;
    validators_.emplace(operator_key, std::move(v));
    consensus_to_operator_.emplace(consensus_key, operator_key);
}

void StakingState::bond(const OperatorKey& operator_key, Amount amount,
                        Height current_height) {
    if (amount == 0) {
        throw StakeError(StakeErrorCode::amount_zero, "bond amount is zero");
    }
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    Validator& v = it->second;
    if (v.status == ValidatorStatus::Unbonded) {
        throw StakeError(StakeErrorCode::unbonding_not_available,
                         "validator is unbonded; re-register to bond again");
    }
    v.bonded_stake += amount;
    recompute_voting_power(v);
    v.status = ValidatorStatus::Bonded;
    v.last_updated = current_height;
    ++v.nonce;
    current_height_ = std::max(current_height_, current_height);
    total_bonded_stake_ += amount;
}

void StakingState::unbond(const OperatorKey& operator_key, Amount amount,
                          Height current_height) {
    if (amount == 0) {
        throw StakeError(StakeErrorCode::amount_zero, "unbond amount is zero");
    }
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    Validator& v = it->second;
    if (v.status != ValidatorStatus::Bonded) {
        throw StakeError(StakeErrorCode::unbonding_not_available,
                         "validator is not bonded");
    }
    if (amount > v.bonded_stake) {
        throw StakeError(StakeErrorCode::insufficient_bonded_stake,
                         "unbond amount exceeds bonded stake");
    }
    v.bonded_stake -= amount;
    recompute_voting_power(v);
    total_bonded_stake_ -= amount;
    v.last_updated = current_height;
    current_height_ = std::max(current_height_, current_height);
    if (v.bonded_stake == 0) {
        v.status = ValidatorStatus::Unbonded;
    }
    unbonding_queue_.push_back(UnbondingEntry{operator_key, amount,
                                              current_height +
                                                  params_.unbonding_period});
}

void StakingState::withdraw_unbonded(const OperatorKey& operator_key,
                                     Height current_height) {
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    current_height_ = std::max(current_height_, current_height);
    unbonding_queue_.erase(
        std::remove_if(unbonding_queue_.begin(), unbonding_queue_.end(),
                       [&](const UnbondingEntry& e) {
                           return e.operator_key == operator_key &&
                                  e.release_height <= current_height;
                       }),
        unbonding_queue_.end());
}

void StakingState::slash(const OperatorKey& operator_key, Amount amount) {
    if (amount == 0) {
        throw StakeError(StakeErrorCode::amount_zero, "slash amount is zero");
    }
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    Validator& v = it->second;
    if (v.status != ValidatorStatus::Bonded) {
        throw StakeError(StakeErrorCode::unbonding_not_available,
                         "validator is not bonded");
    }
    if (amount > v.bonded_stake) {
        throw StakeError(StakeErrorCode::insufficient_bonded_stake,
                         "slash amount exceeds bonded stake");
    }
    v.bonded_stake -= amount;
    recompute_voting_power(v);
    total_bonded_stake_ -= amount;
    if (v.bonded_stake == 0) {
        v.status = ValidatorStatus::Unbonded;
    }
}

void StakingState::set_consensus_key(const OperatorKey& operator_key,
                                     const ConsensusKey& new_consensus_key) {
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    Validator& v = it->second;
    // Remove old mapping.
    consensus_to_operator_.erase(v.consensus_key);
    if (consensus_to_operator_.find(new_consensus_key) !=
        consensus_to_operator_.end()) {
        throw StakeError(StakeErrorCode::invalid_consensus_key,
                         "consensus key already in use");
    }
    v.consensus_key = new_consensus_key;
    consensus_to_operator_.emplace(new_consensus_key, operator_key);
}

void StakingState::delegate(const Delegator& delegator,
                            const OperatorKey& operator_key, Amount amount) {
    if (amount == 0) {
        throw StakeError(StakeErrorCode::amount_zero, "delegation amount is zero");
    }
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    if (it->second.status != ValidatorStatus::Bonded) {
        throw StakeError(StakeErrorCode::unbonding_not_available,
                         "validator is not bonded");
    }
    delegations_[delegator][operator_key] += amount;
    recompute_voting_power(it->second);
    total_bonded_stake_ += amount;
}

void StakingState::undelegate(const Delegator& delegator,
                              const OperatorKey& operator_key, Amount amount,
                              Height current_height) {
    if (amount == 0) {
        throw StakeError(StakeErrorCode::amount_zero,
                         "undelegation amount is zero");
    }
    auto del_it = delegations_.find(delegator);
    if (del_it == delegations_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "no delegation found");
    }
    auto target_it = del_it->second.find(operator_key);
    if (target_it == del_it->second.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "no delegation to validator");
    }
    if (amount > target_it->second) {
        throw StakeError(StakeErrorCode::insufficient_bonded_stake,
                         "undelegation amount exceeds delegation");
    }
    target_it->second -= amount;
    if (target_it->second == 0) {
        del_it->second.erase(target_it);
        if (del_it->second.empty()) {
            delegations_.erase(del_it);
        }
    }
    auto v_it = validators_.find(operator_key);
    if (v_it != validators_.end()) {
        recompute_voting_power(v_it->second);
        total_bonded_stake_ -= amount;
    }
    current_height_ = std::max(current_height_, current_height);
    unbonding_queue_.push_back(UnbondingEntry{delegator, amount,
                                              current_height +
                                                  params_.unbonding_period});
}

void StakingState::add_reward(Amount amount) {
    reward_pool_ += amount;
}

void StakingState::distribute_rewards() {
    // Distribute pro-rata to effective stake. Reward accounting is tracked
    // here; actual credit to UTXO balances is performed by the ABCI
    // application atomically with state transition.
    reward_pool_ = 0;
}

ValidatorSet StakingState::validator_set(Height current_height) const {
    std::vector<const Validator*> active;
    active.reserve(validators_.size());
    for (const auto& [_, v] : validators_) {
        if (v.status == ValidatorStatus::Bonded && v.voting_power > 0) {
            active.push_back(&v);
        }
    }
    std::sort(active.begin(), active.end(),
              [](const Validator* a, const Validator* b) {
                  if (a->voting_power != b->voting_power) {
                      return a->voting_power > b->voting_power;
                  }
                  return a->operator_key < b->operator_key;
              });
    const std::uint32_t limit =
        static_cast<std::uint32_t>(params_.max_validators);
    if (active.size() > limit) {
        active.resize(limit);
    }
    ValidatorSet result;
    result.height = current_height;
    result.total_bonded_stake = 0;
    result.total_voting_power = 0;
    std::vector<ValidatorUpdate> updates;
    updates.reserve(active.size());
    for (const Validator* v : active) {
        updates.push_back(ValidatorUpdate{v->consensus_key,
                                          v->voting_power});
        result.total_bonded_stake += v->bonded_stake;
        result.total_voting_power += v->voting_power;
    }
    // CometBFT applies validator updates sorted by consensus key.
    std::sort(updates.begin(), updates.end(),
              [](const ValidatorUpdate& a, const ValidatorUpdate& b) {
                  return a.consensus_key < b.consensus_key;
              });
    result.updates = std::move(updates);
    return result;
}

std::vector<ValidatorUpdate> StakingState::validator_updates(
    Height current_height) const {
    return validator_set(current_height).updates;
}

std::uint64_t StakingState::top_k_stake_share_ppm(
    std::uint32_t k) const {
    if (total_bonded_stake_ == 0) {
        return 0;
    }
    std::vector<Amount> stakes;
    stakes.reserve(validators_.size());
    for (const auto& [_, v] : validators_) {
        stakes.push_back(v.bonded_stake);
    }
    std::sort(stakes.begin(), stakes.end(), std::greater<Amount>());
    Amount top = 0;
    for (std::uint32_t i = 0; i < k && i < stakes.size(); ++i) {
        top += stakes[i];
    }
    // parts per million: (top * 1'000'000) / total, checked.
    if (top > (std::numeric_limits<Amount>::max() / 1'000'000ULL)) {
        return 1'000'000ULL;
    }
    return static_cast<std::uint64_t>((top * 1'000'000ULL) /
                                      total_bonded_stake_);
}

std::uint64_t StakingState::voting_power_hhi_ppm() const {
    if (total_bonded_stake_ == 0) {
        return 0;
    }
    // HHI in ppm: sum over validators of (vp * 1'000'000 / total)^2 / 1'000'000
    std::uint64_t hhi = 0;
    for (const auto& [_, v] : validators_) {
        if (v.voting_power == 0) {
            continue;
        }
        const std::uint64_t share =
            (static_cast<std::uint64_t>(v.voting_power) * 1'000'000ULL) /
            total_bonded_stake_;
        hhi += (share * share) / 1'000'000ULL;
    }
    return hhi;
}

} // namespace feinstat::staking
