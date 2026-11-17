#pragma once

#include "feinstat/utxo/types.hpp"
#include "feinstat/utxo/validation.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::utxo {

// Bounded mempool admitting valid transactions and preventing double-spends
// within the pool. Inputs must reference outpoints unspent in the underlying
// chain UTXO set and not already consumed by another pooled transaction.
// Outputs created by pooled transactions become spendable only after
// confirmation in a block (cross-pool atomicity is deferred).
class Mempool {
public:
    Mempool(std::size_t max_transaction_count,
            std::size_t max_total_bytes);

    // Attempt to admit a transaction. Validates against the chain UTXO view
    // combined with mempool-spent outpoints. On success the transaction is
    // stored; on failure returns false with a human-readable error.
    bool add(const ChainId& chain_id, const UtxoView& chain_view,
             const Transaction& transaction, std::string* error = nullptr);

    // Remove a transaction by identifier (e.g. after block confirmation).
    void remove(const TransactionId& transaction_id);

    // Inform the pool that an outpoint was spent by a confirmed block;
    // pooled transactions consuming it are removed.
    void remove_spent(const Outpoint& outpoint);

    // All admitted transactions in deterministic order (sorted by id).
    [[nodiscard]] std::vector<Transaction> transactions() const;

    [[nodiscard]] bool contains(const TransactionId& transaction_id) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t total_bytes() const noexcept;
    [[nodiscard]] std::size_t max_transaction_count() const noexcept;
    [[nodiscard]] std::size_t max_total_bytes() const noexcept;

private:
    struct Entry {
        Transaction transaction;
        std::size_t bytes{0};
        std::uint64_t sequence{0};
    };

    std::size_t max_transaction_count_;
    std::size_t max_total_bytes_;
    std::map<TransactionId, Entry> entries_;           // keyed by id
    std::map<Outpoint, TransactionId> spent_by_;       // outpoint -> spending tx id
    std::vector<TransactionId> insertion_order_;       // FIFO eviction order
    std::size_t total_bytes_{0};
    std::uint64_t next_sequence_{0};

    [[nodiscard]] bool admits_outpoint(const Outpoint& outpoint) const;
    void evict_to_limits();
};

} // namespace feinstat::utxo
