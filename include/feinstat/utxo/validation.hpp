#pragma once

#include "feinstat/utxo/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::utxo {

// Read-only view of spendable outputs used by transaction validation.
// Implementations are deterministic and side-effect free.
class UtxoView {
public:
    virtual ~UtxoView() = default;

    // True if the outpoint exists and is unspent in this view.
    [[nodiscard]] virtual bool contains(const Outpoint& outpoint) const = 0;
    // The output referenced by an unspent outpoint. Undefined if !contains.
    [[nodiscard]] virtual const Output& get(const Outpoint& outpoint) const = 0;
};

// Result of validating and applying a transaction against a UTXO view.
struct ValidationResult {
    bool valid{false};
    std::string error;
    Amount fee{0}; // sum(inputs) - sum(outputs); only meaningful when valid
};

// Syntactic and semantic validation of a transaction against a UTXO view.
// Checks: non-empty inputs/outputs, no duplicate outpoints, input existence,
// single-consumption (no double-spend within the batch), ed25519 witness
// signatures over the domain-separated chain-bound digest, and value
// conservation with checked arithmetic. Does not mutate the view.
[[nodiscard]] ValidationResult validate_transaction(const ChainId& chain_id,
                                                    const Transaction& transaction,
                                                    const UtxoView& view);

// Total input amount with overflow detection.
[[nodiscard]] std::optional<Amount> sum_inputs(const Transaction& transaction,
                                               const UtxoView& view);

// Total output amount with overflow detection.
[[nodiscard]] std::optional<Amount> sum_outputs(const Transaction& transaction);

} // namespace feinstat::utxo
