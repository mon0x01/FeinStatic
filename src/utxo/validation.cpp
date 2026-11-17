#include "feinstat/utxo/validation.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace feinstat::utxo {

namespace {

std::optional<Amount> checked_add(Amount a, Amount b) {
    if (b > kMaxAmount - a) {
        return std::nullopt;
    }
    return a + b;
}

} // namespace

std::optional<Amount> sum_outputs(const Transaction& transaction) {
    Amount total = 0;
    for (const auto& output : transaction.outputs) {
        if (output.amount > kMaxAmount) {
            return std::nullopt;
        }
        const auto added = checked_add(total, output.amount);
        if (!added) {
            return std::nullopt;
        }
        total = *added;
    }
    return total;
}

std::optional<Amount> sum_inputs(const Transaction& transaction,
                                 const UtxoView& view) {
    Amount total = 0;
    for (const auto& input : transaction.inputs) {
        if (!view.contains(input.outpoint)) {
            return std::nullopt;
        }
        const Amount amount = view.get(input.outpoint).amount;
        const auto added = checked_add(total, amount);
        if (!added) {
            return std::nullopt;
        }
        total = *added;
    }
    return total;
}

ValidationResult validate_transaction(const ChainId& chain_id,
                                      const Transaction& transaction,
                                      const UtxoView& view) {
    ValidationResult result;

    if (transaction.inputs.empty()) {
        result.error = "transaction must consume at least one input";
        return result;
    }
    if (transaction.outputs.empty()) {
        result.error = "transaction must produce at least one output";
        return result;
    }
    if (transaction.inputs.size() > kMaxInputsPerTransaction) {
        result.error = "too many inputs";
        return result;
    }
    if (transaction.outputs.size() > kMaxOutputsPerTransaction) {
        result.error = "too many outputs";
        return result;
    }

    // No duplicate outpoint consumption within the transaction.
    {
        std::vector<Outpoint> seen;
        seen.reserve(transaction.inputs.size());
        for (const auto& input : transaction.inputs) {
            for (const auto& prior : seen) {
                if (input.outpoint == prior) {
                    result.error = "duplicate outpoint consumed in transaction";
                    return result;
                }
            }
            seen.push_back(input.outpoint);
        }
    }

    const auto inputs_total = sum_inputs(transaction, view);
    if (!inputs_total) {
        result.error = "input references a spent or unknown outpoint, or amount overflows";
        return result;
    }

    const auto outputs_total = sum_outputs(transaction);
    if (!outputs_total) {
        result.error = "output amount overflows";
        return result;
    }

    if (*outputs_total > *inputs_total) {
        result.error = "outputs exceed inputs; value not conserved";
        return result;
    }

    // Witness signatures: each input's output spending key must sign the
    // chain-bound domain-separated digest.
    const TransactionId digest = transaction_digest(chain_id, transaction);
    const Bytes digest_bytes(digest.begin(), digest.end());
    for (const auto& input : transaction.inputs) {
        if (!view.contains(input.outpoint)) {
            result.error = "input references a spent or unknown outpoint";
            return result;
        }
        const PublicKey spending_key = view.get(input.outpoint).spending_key;
        if (!crypto::verify_ed25519(spending_key, digest_bytes, input.witness)) {
            result.error = "invalid witness signature for input";
            return result;
        }
    }

    result.valid = true;
    result.fee = *inputs_total - *outputs_total;
    return result;
}

} // namespace feinstat::utxo
