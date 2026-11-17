#pragma once

#include "feinstat/utxo/types.hpp"
#include "feinstat/utxo/validation.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::utxo {

// Deterministic UTXO set: the authoritative financial state representation.
// Outpoints are stored in a sorted map so iteration and state commitments are
// reproducible on every correct node given identical transactions and prior
// state.
class UtxoSet : public UtxoView {
public:
    UtxoSet() = default;

    // UtxoView
    [[nodiscard]] bool contains(const Outpoint& outpoint) const override;
    [[nodiscard]] const Output& get(const Outpoint& outpoint) const override;

    // Total amount spendable by the given spending key (balance view used by
    // wallet/account abstractions on top of the UTXO ledger).
    [[nodiscard]] Amount balance(const PublicKey& spending_key) const;

    // Number of unspent outputs.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    // All unspent outpoints in deterministic order.
    [[nodiscard]] std::vector<Outpoint> outpoints() const;

    // Initialize from a genesis allocation map: spending key -> amount.
    static UtxoSet genesis(
        const std::vector<std::pair<PublicKey, Amount>>& allocations);

    // Validate and apply a transaction, mutating the set. Consumed outpoints
    // are removed; new outputs are inserted. Returns the transaction fee.
    // Throws std::runtime_error if the transaction is invalid.
    Amount apply(const ChainId& chain_id, const Transaction& transaction);

    // Reproducible state commitment (Merkle root) over all unspent outputs.
    [[nodiscard]] Hash state_root() const;

private:
    std::map<Outpoint, Output> utxos_;

    [[nodiscard]] Hash leaf_hash(const Outpoint& outpoint,
                                 const Output& output) const;
};

} // namespace feinstat::utxo
