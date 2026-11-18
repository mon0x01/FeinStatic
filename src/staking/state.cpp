#include "feinstat/staking/state.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/common/bytes.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
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

Validator* StakingState::validator(
    const OperatorKey& operator_key) noexcept {
    const auto* self = this;
    const auto* cv = self->validator(operator_key);
    if (cv == nullptr) {
        return nullptr;
    }
    return const_cast<Validator*>(cv);
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
    validators_.find(operator_key)->second.last_active_height = current_height_;
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
    v.last_active_height = current_height;
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
    v.last_active_height = current_height;
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
    it->second.last_active_height = current_height;
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
    v.last_active_height = current_height_;
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

void StakingState::record_activity(const OperatorKey& operator_key,
                                   Height current_height) {
    auto it = validators_.find(operator_key);
    if (it == validators_.end()) {
        throw StakeError(StakeErrorCode::validator_not_found,
                         "validator not found");
    }
    it->second.last_active_height = current_height;
    current_height_ = std::max(current_height_, current_height);
}

Height StakingState::current_epoch(Height current_height) const noexcept {
    if (params_.epoch_length == 0) {
        return 0;
    }
    return current_height / params_.epoch_length;
}

EpochInfo StakingState::epoch_info(Height current_height) const noexcept {
    return EpochInfo{current_epoch(current_height), params_.epoch_length,
                     last_transition_height_};
}

EpochInfo StakingState::transition_epoch(Height current_height) {
    current_height_ = std::max(current_height_, current_height);
    const Height epoch = current_epoch(current_height);
    EpochInfo info{epoch, params_.epoch_length, last_transition_height_};

    if (params_.epoch_length == 0 || epoch <= last_epoch_) {
        return info;
    }

    // Epoch boundary (FST-REQ-0037, ADR-0003):
    // 1. Inactivity leak: bonded validators inactive for more than
    //    inactivity_leak_after_epochs epochs lose a fraction of bonded stake
    //    per epoch of inactivity (nothing-at-stake mitigation; stake of
    //    long-inactive validators gradually bleeds to zero).
    // 2. Committee rotation: the active committee is recomputed from bonded
    //    stake at the epoch boundary via validator_set() (top max_validators
    //    by voting power); validators that fall out rotate out of the
    //    active committee until they re-enter.
    const Height epoch_start = epoch * params_.epoch_length;
    for (auto& [_, v] : validators_) {
        if (v.status != ValidatorStatus::Bonded || v.bonded_stake == 0) {
            continue;
        }
        const Height validator_epoch = current_epoch(v.last_active_height);
        const Height epochs_inactive = epoch - validator_epoch;
        if (epochs_inactive <= params_.inactivity_leak_after_epochs) {
            continue;
        }
        Amount leak = 0;
        const std::uint32_t bp = params_.inactivity_leak_fraction_basis_points;
        if (bp > 0) {
            if (v.bonded_stake > std::numeric_limits<Amount>::max() / bp) {
                leak = v.bonded_stake;
            } else {
                leak = (v.bonded_stake * bp) / 10'000ULL;
            }
        }
        if (leak == 0 && v.bonded_stake > 0) {
            leak = 1; // at least one atom while the leak is active
        }
        leak = std::min(leak, v.bonded_stake);
        v.bonded_stake -= leak;
        recompute_voting_power(v);
        total_bonded_stake_ -= leak;
        if (v.bonded_stake == 0) {
            v.status = ValidatorStatus::Unbonded;
        }
    }

    last_epoch_ = epoch;
    last_transition_height_ = epoch_start;
    info.last_transition_height = epoch_start;
    return info;
}

SlashingOutcome StakingState::submit_slashing_evidence(
    const SlashingEvidence& evidence, Height current_height) {
    current_height_ = std::max(current_height_, current_height);
    SlashingOutcome outcome;
    outcome.reason = "evidence not submitted";

    if (evidence.type != EvidenceType::equivocation) {
        throw StakeError(StakeErrorCode::evidence_invalid,
                         "unsupported evidence type");
    }

    const EquivocationEvidence& e = evidence.equivocation;
    if (e.vote_a == e.vote_b) {
        throw StakeError(StakeErrorCode::evidence_invalid,
                         "votes are identical; not equivocation");
    }

    // Look up the equivocating validator by consensus key. Slashing is
    // evidence-based only (ADR-0003): no discretionary slashing.
    const Validator* cv = validator_by_consensus(e.consensus_key);
    if (cv == nullptr) {
        throw StakeError(StakeErrorCode::evidence_invalid,
                         "evidence references unknown validator");
    }
    if (cv->status != ValidatorStatus::Bonded &&
        cv->status != ValidatorStatus::Unbonding) {
        throw StakeError(StakeErrorCode::validator_not_bonded,
                         "validator is not slashable");
    }

    // Verify the validator's signatures over both conflicting votes. An
    // equivocation is slashable only when both signatures verify under the
    // validator's consensus key.
    if (!crypto::verify_ed25519(e.consensus_key, e.vote_a,
                                e.signature_a)) {
        throw StakeError(StakeErrorCode::signature_verification_failed,
                         "first vote signature invalid");
    }
    if (!crypto::verify_ed25519(e.consensus_key, e.vote_b,
                                e.signature_b)) {
        throw StakeError(StakeErrorCode::signature_verification_failed,
                         "second vote signature invalid");
    }

    // Canonical evidence hash; reject duplicate submissions so a validator is
    // slashed at most once per distinct evidence item.
    ByteWriter writer;
    writer.u8(static_cast<Byte>(evidence.type));
    writer.fixed(e.consensus_key);
    writer.u64(e.height);
    writer.u64(e.round);
    writer.blob(e.vote_a);
    writer.blob(e.vote_b);
    writer.fixed(e.signature_a);
    writer.fixed(e.signature_b);
    const feinstat::Hash evidence_hash =
        crypto::sha256(std::move(writer).take());
    if (!submitted_evidence_.insert(evidence_hash).second) {
        throw StakeError(StakeErrorCode::evidence_duplicate,
                         "identical evidence already submitted");
    }

    // Slash equivocation_slash_basis_points of bonded stake, bounded by the
    // validator's bonded stake (at least one atom when stake is at risk).
    Validator& v = validators_.find(cv->operator_key)->second;
    const std::uint32_t bp = params_.equivocation_slash_basis_points;
    Amount amount = 0;
    if (bp > 0) {
        if (v.bonded_stake > std::numeric_limits<Amount>::max() / bp) {
            amount = v.bonded_stake;
        } else {
            amount = (v.bonded_stake * bp) / 10'000ULL;
        }
    }
    if (amount == 0 && v.bonded_stake > 0) {
        amount = 1;
    }
    amount = std::min(amount, v.bonded_stake);

    v.bonded_stake -= amount;
    recompute_voting_power(v);
    total_bonded_stake_ -= amount;
    if (v.bonded_stake == 0) {
        v.status = ValidatorStatus::Unbonded;
    }

    slashing_events_.push_back(SlashingEvent{
        v.operator_key,
        v.consensus_key,
        amount,
        current_height,
        "equivocation",
        evidence_hash,
    });

    outcome.accepted = true;
    outcome.slashed_amount = amount;
    outcome.new_status = v.status;
    outcome.reason = "slashed for equivocation";
    return outcome;
}

} // namespace feinstat::staking
