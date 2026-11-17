#pragma once

#include "feinstat/common/types.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace feinstat::monetary {

using Height = std::uint64_t;

// Testnet-labelled parameters (NOT production policy until justified by
// simulation, per ADR-0005 / D-015). All values use exact integer atoms.
struct MonetaryParams {
    // Genesis native-FST supply (atoms).
    Amount genesis_supply{1'000'000'000ULL * kAtomsPerCoin};
    // Issuance per block at genesis (atoms).
    Amount initial_block_issuance{2ULL * kAtomsPerCoin};
    // Permanent per-block tail issuance (atoms), approached asymptotically.
    Amount tail_block_issuance{1ULL * kAtomsPerCoin};
    // Blocks over which per-block issuance halves (geometric decline).
    Height issuance_halving_interval{100'000};
    // Absolute supply cap (overflow guard); 0 means no cap.
    Amount max_supply{0};
    // Minimum / maximum congestion-adjusted base fee (atoms).
    Amount base_fee_min{1'000ULL};
    Amount base_fee_max{1'000'000ULL};
    // Target block utilization (parts per million) for congestion adjustment.
    std::uint64_t target_utilization_ppm{500'000}; // 50%
    // Maximum base-fee change per block (parts per million).
    std::uint64_t max_base_fee_change_ppm{125'000}; // 12.5%
    // Priority fee cap (atoms) to bound MEV extraction.
    Amount priority_fee_cap{1'000'000ULL};
};

// Snapshot of supply accounting at a given height. Every field is derived
// deterministically from genesis + issuance + fees + slashing so the full
// supply history is independently verifiable.
struct SupplySnapshot {
    Height height{0};
    // Total non-burned supply (liquid + bonded).
    Amount total_supply{0};
    // Cumulative amount burned (base fees + slashed stake).
    Amount burned{0};
    // Total stake currently bonded to validators.
    Amount total_bonded_stake{0};
    // Liquid circulating supply = total_supply - total_bonded_stake.
    Amount liquid_supply{0};
    // Cumulative amount issued since genesis.
    Amount cumulative_issued{0};
};

// Result of computing fees for a block.
struct FeeResult {
    Amount base_fee{0};        // burned
    Amount priority_fee{0};    // to block-producing validator
    Amount total_fee{0};       // base_fee + priority_fee
    Amount burned_amount{0};   // == base_fee
    Amount validator_reward{0}; // priority_fee (+ issuance reward applied separately)
};

// Monetary error category.
enum class MonetaryErrorCode {
    ok,
    overflow,
    underflow,
    exceeds_max_supply,
    insufficient_liquid_supply,
    invalid_fee,
};

class MonetaryError : public std::runtime_error {
public:
    explicit MonetaryError(MonetaryErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] MonetaryErrorCode code() const noexcept { return code_; }
private:
    MonetaryErrorCode code_;
};

} // namespace feinstat::monetary
