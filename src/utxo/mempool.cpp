#include "feinstat/utxo/mempool.hpp"

#include <algorithm>

namespace feinstat::utxo {

namespace {

// View adapter: a chain UTXO view with mempool-spent outpoints treated as
// spent, so pooled transactions cannot double-spend each other's inputs.
class MempoolAdaptingView : public UtxoView {
public:
    MempoolAdaptingView(const UtxoView& chain_view,
                        const std::map<Outpoint, TransactionId>& spent_by)
        : chain_view_(chain_view), spent_by_(spent_by) {}

    [[nodiscard]] bool contains(const Outpoint& outpoint) const override {
        return spent_by_.find(outpoint) == spent_by_.end() &&
               chain_view_.contains(outpoint);
    }

    [[nodiscard]] const Output& get(const Outpoint& outpoint) const override {
        return chain_view_.get(outpoint);
    }

private:
    const UtxoView& chain_view_;
    const std::map<Outpoint, TransactionId>& spent_by_;
};

std::size_t transaction_serialized_bytes(const Transaction& transaction) {
    return encode_transaction(transaction).size();
}

} // namespace

Mempool::Mempool(std::size_t max_transaction_count,
                 std::size_t max_total_bytes)
    : max_transaction_count_(max_transaction_count),
      max_total_bytes_(max_total_bytes) {}

bool Mempool::add(const ChainId& chain_id, const UtxoView& chain_view,
                  const Transaction& transaction, std::string* error) {
    const TransactionId tx_id = transaction_id(chain_id, transaction);

    if (entries_.find(tx_id) != entries_.end()) {
        if (error) *error = "transaction already in mempool";
        return false;
    }

    const std::size_t tx_bytes = transaction_serialized_bytes(transaction);
    if (tx_bytes > kMaxTransactionSizeBytes) {
        if (error) *error = "transaction exceeds maximum size";
        return false;
    }

    MempoolAdaptingView view(chain_view, spent_by_);
    const auto result = validate_transaction(chain_id, transaction, view);
    if (!result.valid) {
        if (error) *error = result.error;
        return false;
    }

    // Record spent outpoints and store the transaction.
    for (const auto& input : transaction.inputs) {
        spent_by_.emplace(input.outpoint, tx_id);
    }
    Entry entry{transaction, tx_bytes, next_sequence_++};
    entries_.emplace(tx_id, std::move(entry));
    insertion_order_.push_back(tx_id);
    total_bytes_ += tx_bytes;

    evict_to_limits();
    return true;
}

void Mempool::remove(const TransactionId& transaction_id) {
    const auto it = entries_.find(transaction_id);
    if (it == entries_.end()) {
        return;
    }
    // Copy everything needed before erasing the map node, so no reference
    // outlives the erase.
    Entry entry = it->second;
    entries_.erase(it);
    total_bytes_ -= entry.bytes;
    for (const auto& input : entry.transaction.inputs) {
        spent_by_.erase(input.outpoint);
    }
    insertion_order_.erase(
        std::remove(insertion_order_.begin(), insertion_order_.end(),
                    transaction_id),
        insertion_order_.end());
}

void Mempool::remove_spent(const Outpoint& outpoint) {
    const auto it = spent_by_.find(outpoint);
    if (it == spent_by_.end()) {
        return;
    }
    // Copy the identifier before erasing the node it references.
    const TransactionId transaction_id = it->second;
    remove(transaction_id);
}

std::vector<Transaction> Mempool::transactions() const {
    // Deterministic order: sorted by transaction id.
    std::vector<Transaction> result;
    result.reserve(entries_.size());
    for (const auto& [id, entry] : entries_) {
        (void)id;
        result.push_back(entry.transaction);
    }
    return result;
}

bool Mempool::contains(const TransactionId& transaction_id) const {
    return entries_.find(transaction_id) != entries_.end();
}

std::size_t Mempool::size() const noexcept {
    return entries_.size();
}

std::size_t Mempool::total_bytes() const noexcept {
    return total_bytes_;
}

std::size_t Mempool::max_transaction_count() const noexcept {
    return max_transaction_count_;
}

std::size_t Mempool::max_total_bytes() const noexcept {
    return max_total_bytes_;
}

bool Mempool::admits_outpoint(const Outpoint& outpoint) const {
    return spent_by_.find(outpoint) == spent_by_.end();
}

void Mempool::evict_to_limits() {
    while (entries_.size() > max_transaction_count_ ||
           total_bytes_ > max_total_bytes_) {
        if (insertion_order_.empty()) {
            break;
        }
        const TransactionId oldest = insertion_order_.front();
        insertion_order_.erase(insertion_order_.begin());
        remove(oldest);
    }
}

} // namespace feinstat::utxo
