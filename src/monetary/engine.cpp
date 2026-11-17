#include "feinstat/monetary/engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace feinstat::monetary {

namespace {

Amount checked_add(Amount a, Amount b) {
    if (b > (std::numeric_limits<Amount>::max() - a)) {
        throw MonetaryError(MonetaryErrorCode::overflow, "amount overflow");
    }
    return a + b;
}

Amount checked_sub(Amount a, Amount b) {
    if (b > a) {
        throw MonetaryError(MonetaryErrorCode::underflow, "amount underflow");
    }
    return a - b;
}

} // namespace

MonetaryEngine::MonetaryEngine(MonetaryParams params)
    : params_(std::move(params)),
      genesis_supply_(params_.genesis_supply),
      total_supply_(params_.genesis_supply),
      burned_(0),
      total_bonded_stake_(0),
      cumulative_issued_(0),
      height_(0) {}

SupplySnapshot MonetaryEngine::genesis() const {
    return SupplySnapshot{
        0,
        total_supply_,
        burned_,
        total_bonded_stake_,
        total_supply_ - total_bonded_stake_,
        cumulative_issued_,
    };
}

Amount MonetaryEngine::issuance_for_block(Height height) const {
    if (params_.issuance_halving_interval == 0) {
        return params_.initial_block_issuance;
    }
    const std::uint64_t halvings =
        static_cast<std::uint64_t>(height) / params_.issuance_halving_interval;
    const std::uint64_t shift = std::min(halvings, std::uint64_t{63});
    Amount issuance = params_.initial_block_issuance >> shift;
    if (issuance < params_.tail_block_issuance) {
        issuance = params_.tail_block_issuance;
    }
    return issuance;
}

Amount MonetaryEngine::apply_issuance(Height height) {
    const Amount issuance = issuance_for_block(height);
    const Amount new_supply = checked_add(total_supply_, issuance);
    if (params_.max_supply != 0 && new_supply > params_.max_supply) {
        throw MonetaryError(MonetaryErrorCode::exceeds_max_supply,
                            "issuance exceeds maximum supply");
    }
    total_supply_ = new_supply;
    cumulative_issued_ = checked_add(cumulative_issued_, issuance);
    height_ = height;
    return issuance;
}

void MonetaryEngine::apply_fees(Amount base_fee, Amount priority_fee) {
    if (priority_fee > params_.priority_fee_cap) {
        throw MonetaryError(MonetaryErrorCode::invalid_fee,
                            "priority fee exceeds cap");
    }
    if (base_fee > total_supply_) {
        throw MonetaryError(MonetaryErrorCode::underflow,
                            "amount underflow");
    }
    // Base fee is burned: removed from supply.
    total_supply_ -= base_fee;
    burned_ += base_fee;
    height_ = 0; // fees do not advance issuance height
}

Amount MonetaryEngine::adjust_base_fee(Amount previous_base_fee,
                                       std::uint64_t utilization_ppm) const {
    if (previous_base_fee < params_.base_fee_min) {
        previous_base_fee = params_.base_fee_min;
    }
    if (previous_base_fee > params_.base_fee_max) {
        previous_base_fee = params_.base_fee_max;
    }
    if (utilization_ppm == params_.target_utilization_ppm) {
        return previous_base_fee;
    }
    // Signed adjustment in ppm: positive = utilization above target.
    const int64_t delta_ppm =
        static_cast<int64_t>(utilization_ppm) -
        static_cast<int64_t>(params_.target_utilization_ppm);
    // Scale by max change per block relative to the target.
    const int64_t scaled = delta_ppm *
                           static_cast<int64_t>(params_.max_base_fee_change_ppm) /
                           static_cast<int64_t>(params_.target_utilization_ppm);
    Amount result = previous_base_fee;
    if (scaled >= 0) {
        // new = previous * (1'000'000 + scaled) / 1'000'000.
        // previous <= base_fee_max and (1'000'000+scaled) <= ~1.125'000'000,
        // so the product fits in uint64_t.
        result = (previous_base_fee *
                  (1'000'000ULL + static_cast<Amount>(scaled))) /
                 1'000'000ULL;
    } else {
        const Amount reduction =
            (static_cast<Amount>(-scaled) * previous_base_fee) /
            1'000'000ULL;
        result = previous_base_fee > reduction
                     ? previous_base_fee - reduction
                     : 0;
    }
    if (result < params_.base_fee_min) {
        result = params_.base_fee_min;
    }
    if (result > params_.base_fee_max) {
        result = params_.base_fee_max;
    }
    return result;
}

void MonetaryEngine::bond(Amount amount) {
    if (amount == 0) {
        return;
    }
    const Amount liquid = total_supply_ - total_bonded_stake_;
    if (amount > liquid) {
        throw MonetaryError(MonetaryErrorCode::insufficient_liquid_supply,
                            "insufficient liquid supply to bond");
    }
    total_bonded_stake_ = checked_add(total_bonded_stake_, amount);
}

void MonetaryEngine::unbond(Amount amount) {
    if (amount == 0) {
        return;
    }
    if (amount > total_bonded_stake_) {
        throw MonetaryError(MonetaryErrorCode::underflow,
                            "unbond exceeds bonded stake");
    }
    total_bonded_stake_ = checked_sub(total_bonded_stake_, amount);
}

void MonetaryEngine::slash(Amount amount) {
    if (amount == 0) {
        return;
    }
    if (amount > total_bonded_stake_) {
        throw MonetaryError(MonetaryErrorCode::underflow,
                            "slash exceeds bonded stake");
    }
    total_bonded_stake_ = checked_sub(total_bonded_stake_, amount);
    burned_ = checked_add(burned_, amount);
    total_supply_ = checked_sub(total_supply_, amount);
}

SupplySnapshot MonetaryEngine::snapshot(Height height) const {
    return SupplySnapshot{
        height,
        total_supply_,
        burned_,
        total_bonded_stake_,
        total_supply_ - total_bonded_stake_,
        cumulative_issued_,
    };
}

bool MonetaryEngine::supply_conserved() const noexcept {
    // Invariant: total_supply + burned == genesis + cumulative_issued.
    // Checked addition avoids overflow; subtraction is avoided so that
    // burning fees from genesis supply (without prior issuance) does not
    // underflow.
    return checked_add(total_supply_, burned_) ==
           checked_add(genesis_supply_, cumulative_issued_);
}

} // namespace feinstat::monetary
