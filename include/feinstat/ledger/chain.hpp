#pragma once

#include "feinstat/ledger/block.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace feinstat::ledger {

struct ConfirmedTransaction {
    Transaction transaction;
    std::uint64_t block_height{0};
};

struct AccountState {
    Amount balance{0};
    std::uint64_t nonce{0};
    bool operator==(const AccountState&) const = default;
};

class Chain {
public:
    Chain(const std::vector<GenesisAllocation>& allocations,
          std::uint32_t difficulty,
          Amount block_reward);

    static Chain from_blocks(const std::vector<Block>& blocks,
                             std::uint32_t difficulty,
                             Amount block_reward);

    [[nodiscard]] bool validate(std::string* error = nullptr) const;
    [[nodiscard]] bool validate_transaction_batch(const std::vector<Transaction>& txs,
                                                   std::string* error = nullptr) const;
    [[nodiscard]] bool can_append(const Block& block, std::string* error = nullptr) const;
    void append(const Block& block);

    [[nodiscard]] Block make_next_block(const std::vector<Transaction>& transactions,
                                        const Address& miner) const;
    [[nodiscard]] Amount balance_of(const Address& address) const noexcept;
    [[nodiscard]] std::uint64_t nonce_of(const Address& address) const noexcept;
    [[nodiscard]] std::optional<ConfirmedTransaction> find_transaction(const std::string& transaction_id) const;
    [[nodiscard]] Amount total_supply() const noexcept { return total_supply_; }
    [[nodiscard]] std::uint64_t height() const noexcept;
    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return blocks_; }
    [[nodiscard]] const std::set<std::string>& transaction_ids() const noexcept { return transaction_ids_; }
    [[nodiscard]] std::uint32_t difficulty() const noexcept { return difficulty_; }
    [[nodiscard]] Amount block_reward() const noexcept { return block_reward_; }

private:
    using Accounts = std::map<Address, AccountState>;

    [[nodiscard]] static bool apply_transactions(Accounts& accounts,
                                                 const std::vector<Transaction>& txs,
                                                 Amount& fees,
                                                 std::string* error);
    [[nodiscard]] bool apply_block_to_state(const Block& block,
                                            Accounts& accounts,
                                            Amount& supply,
                                            std::string* error) const;

    std::vector<Block> blocks_;
    Accounts accounts_;
    std::set<std::string> transaction_ids_;
    Amount total_supply_{0};
    std::uint32_t difficulty_{0};
    Amount block_reward_{0};
};

} // namespace feinstat::ledger
