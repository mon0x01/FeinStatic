#include "feinstat/ledger/mempool.hpp"

#include "feinstat/crypto/crypto.hpp"

namespace feinstat::ledger {

bool Mempool::add(const Transaction& transaction,
                  const Chain& chain,
                  std::string* error) {
    const auto hash = transaction.id();
    const std::string id = crypto::hex(hash.data(), hash.size());
    if (contains(id) || chain.transaction_ids().contains(id)) {
        if (error != nullptr) *error = "duplicate or replayed transaction";
        return false;
    }
    if (transactions_.size() >= kMaximumMempoolTransactions) {
        if (error != nullptr) *error = "mempool transaction count limit reached";
        return false;
    }
    const std::size_t transaction_bytes = transaction.serialize().size();
    if (transaction_bytes > kMaximumMempoolSerializedBytes - serialized_bytes_) {
        if (error != nullptr) *error = "mempool serialized-byte limit reached";
        return false;
    }

    std::vector<Transaction> candidate = transactions_;
    candidate.push_back(transaction);
    if (!chain.validate_transaction_batch(candidate, error)) return false;
    transactions_.push_back(transaction);
    serialized_bytes_ += transaction_bytes;
    return true;
}

void Mempool::clear() noexcept {
    transactions_.clear();
    serialized_bytes_ = 0;
}

std::uint64_t Mempool::next_nonce(const Address& address,
                                  std::uint64_t chain_nonce) const noexcept {
    std::uint64_t result = chain_nonce;
    for (const auto& transaction : transactions_) {
        if (transaction.sender == address && transaction.nonce > result) result = transaction.nonce;
    }
    return result;
}

bool Mempool::contains(const std::string& transaction_id) const {
    for (const auto& transaction : transactions_) {
        const auto hash = transaction.id();
        if (crypto::hex(hash.data(), hash.size()) == transaction_id) return true;
    }
    return false;
}

} // namespace feinstat::ledger
