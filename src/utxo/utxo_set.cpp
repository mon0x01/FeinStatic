#include "feinstat/utxo/utxo_set.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <stdexcept>

namespace feinstat::utxo {

bool UtxoSet::contains(const Outpoint& outpoint) const {
    return utxos_.find(outpoint) != utxos_.end();
}

const Output& UtxoSet::get(const Outpoint& outpoint) const {
    const auto it = utxos_.find(outpoint);
    if (it == utxos_.end()) {
        throw std::runtime_error("outpoint is spent or unknown");
    }
    return it->second;
}

Amount UtxoSet::balance(const PublicKey& spending_key) const {
    Amount total = 0;
    for (const auto& [outpoint, output] : utxos_) {
        if (output.spending_key == spending_key) {
            const auto added = [&]() -> std::optional<Amount> {
                if (output.amount > kMaxAmount - total) return std::nullopt;
                return total + output.amount;
            }();
            if (!added) {
                throw std::runtime_error("balance overflow");
            }
            total = *added;
        }
    }
    return total;
}

std::size_t UtxoSet::size() const noexcept {
    return utxos_.size();
}

bool UtxoSet::empty() const noexcept {
    return utxos_.empty();
}

std::vector<Outpoint> UtxoSet::outpoints() const {
    std::vector<Outpoint> result;
    result.reserve(utxos_.size());
    for (const auto& [outpoint, _] : utxos_) {
        result.push_back(outpoint);
    }
    return result;
}

UtxoSet UtxoSet::genesis(
    const std::vector<std::pair<PublicKey, Amount>>& allocations) {
    UtxoSet set;
    std::uint32_t index = 0;
    for (const auto& [spending_key, amount] : allocations) {
        if (amount > kMaxAmount) {
            throw std::runtime_error("genesis allocation exceeds maximum amount");
        }
        Outpoint outpoint{Hash{}, index++};
        set.utxos_.emplace(outpoint, Output{amount, spending_key});
    }
    return set;
}

Amount UtxoSet::apply(const ChainId& chain_id, const Transaction& transaction) {
    const auto result = validate_transaction(chain_id, transaction, *this);
    if (!result.valid) {
        throw std::runtime_error("invalid transaction: " + result.error);
    }

    // Consume inputs (single-consumption; duplicates already rejected).
    for (const auto& input : transaction.inputs) {
        utxos_.erase(input.outpoint);
    }

    // Produce outputs, keyed by the domain-separated transaction digest so
    // each output has a globally unique outpoint.
    const TransactionId tx_id = transaction_id(chain_id, transaction);
    std::uint32_t index = 0;
    for (const auto& output : transaction.outputs) {
        if (output.amount > kMaxAmount) {
            throw std::runtime_error("output amount exceeds maximum");
        }
        Outpoint outpoint{tx_id, index++};
        utxos_.emplace(outpoint, output);
    }

    return result.fee;
}

Hash UtxoSet::leaf_hash(const Outpoint& outpoint,
                        const Output& output) const {
    ByteWriter writer;
    writer.raw(outpoint.transaction_hash.data(), outpoint.transaction_hash.size());
    writer.u32(outpoint.output_index);
    writer.u64(output.amount);
    writer.raw(output.spending_key.data(), output.spending_key.size());
    return crypto::sha256(writer.data());
}

Hash UtxoSet::state_root() const {
    std::vector<Hash> leaves;
    leaves.reserve(utxos_.size());
    for (const auto& [outpoint, output] : utxos_) {
        leaves.push_back(leaf_hash(outpoint, output));
    }

    // Deterministic Merkle root: pair adjacent leaves, promote odd leaf.
    // Empty set hashes the empty byte string.
    if (leaves.empty()) {
        return crypto::sha256(Bytes{});
    }

    while (leaves.size() > 1) {
        std::vector<Hash> next;
        next.reserve((leaves.size() + 1U) / 2U);
        for (std::size_t i = 0; i < leaves.size(); i += 2) {
            if (i + 1 < leaves.size()) {
                ByteWriter writer;
                writer.raw(leaves[i].data(), leaves[i].size());
                writer.raw(leaves[i + 1].data(), leaves[i + 1].size());
                next.push_back(crypto::sha256(writer.data()));
            } else {
                next.push_back(leaves[i]);
            }
        }
        leaves = std::move(next);
    }
    return leaves[0];
}

} // namespace feinstat::utxo
