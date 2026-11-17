#pragma once

#include "feinstat/monetary/types.hpp"

#include <cstddef>

namespace feinstat::monetary {

// Deterministic monetary and fee engine. All transitions are pure functions
// of the prior state, the block height, and the supplied amounts (no wall
// clock, no floating point, no unordered iteration). Supply conservation is
// an invariant: total_supply == genesis + cumulative_issued - burned at all
// times.
class MonetaryEngine {
public:
    explicit MonetaryEngine(MonetaryParams params);

    // The genesis state: supply == genesis_supply, nothing issued/burned.
    [[nodiscard]] SupplySnapshot genesis() const;

    // Per-block issuance at the given height: declines geometrically toward
    // the permanent tail emission. Deterministic in height.
    [[nodiscard]] Amount issuance_for_block(Height height) const;

    // Apply the block's issuance (minted to validator rewards). Updates
    // total_supply and cumulative_issued. Throws on supply-cap overflow.
    Amount apply_issuance(Height height);

    // Apply a block's fees: base_fee is burned (removed from supply),
    // priority_fee is payable to the block-producing validator. Throws on
    // underflow or if the priority fee exceeds its cap.
    void apply_fees(Amount base_fee, Amount priority_fee);

    // Congestion-adjusted base fee from the previous base fee and the
    // current block's utilization (parts per million of block capacity).
    // Deterministic; bounded by max_base_fee_change_ppm and [min, max].
    [[nodiscard]] Amount adjust_base_fee(Amount previous_base_fee,
                                         std::uint64_t utilization_ppm) const;

    // Move `amount` from liquid supply into bonded stake (staking/bonding).
    // Throws if insufficient liquid supply.
    void bond(Amount amount);

    // Move `amount` from bonded stake back to liquid supply (unbonding).
    // Throws if insufficient bonded stake.
    void unbond(Amount amount);

    // Slash `amount` from bonded stake; the slashed amount is burned
    // (removed from supply). Throws if insufficient bonded stake.
    void slash(Amount amount);

    [[nodiscard]] SupplySnapshot snapshot(Height height) const;
    [[nodiscard]] const MonetaryParams& params() const noexcept {
        return params_;
    }

    // Supply conservation invariant: total_supply == genesis +
    // cumulative_issued - burned. Always true by construction; exposed for
    // independent verification and tests.
    [[nodiscard]] bool supply_conserved() const noexcept;

private:
    MonetaryParams params_;
    Amount genesis_supply_{0};
    Amount total_supply_{0};
    Amount burned_{0};
    Amount total_bonded_stake_{0};
    Amount cumulative_issued_{0};
    Height height_{0};
};

} // namespace feinstat::monetary
