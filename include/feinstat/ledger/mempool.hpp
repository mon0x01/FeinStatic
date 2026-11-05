#pragma once

#include "feinstat/ledger/chain.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace feinstat::ledger {

// Limits are intentionally conservative for a single-process prototype. They
// bound both memory use and the amount of work admission performs per request.
inline constexpr std::size_t kMaximumMempoolTransactions = 10'000;
inline constexpr std::size_t kMaximumMempoolSerializedBytes = 32U * 1024U * 1024U;

class Mempool {
public:
    [[nodiscard]] bool add(const Transaction& transaction,
                           const Chain& chain,
                           std::string* error = nullptr);
    void clear() noexcept;
    [[nodiscard]] const std::vector<Transaction>& transactions() const noexcept { return transactions_; }
    [[nodiscard]] std::size_t serialized_bytes() const noexcept { return serialized_bytes_; }
    [[nodiscard]] std::uint64_t next_nonce(const Address& address,
                                           std::uint64_t chain_nonce) const noexcept;
    [[nodiscard]] bool contains(const std::string& transaction_id) const;

private:
    std::vector<Transaction> transactions_;
    std::size_t serialized_bytes_{0};
};

} // namespace feinstat::ledger
