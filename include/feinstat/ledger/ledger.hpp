#pragma once

#include "feinstat/ledger/mempool.hpp"
#include "feinstat/consensus/fork_choice.hpp"
#include "feinstat/storage/sqlite_store.hpp"

#include <memory>
#include <string>
#include <vector>

namespace feinstat::ledger {

struct ChainAdoptionResult {
    bool adopted{false};
    bool same_chain{false};
    std::uint64_t previous_height{0};
    std::uint64_t resulting_height{0};
    std::uint64_t common_ancestor_height{0};
    std::size_t requeued_transactions{0};
    std::size_t dropped_detached_transactions{0};
};

class Ledger {
public:
    static Ledger initialize(const std::string& database_path,
                             const std::vector<GenesisAllocation>& allocations,
                             std::uint32_t difficulty = 2,
                             Amount block_reward = kDefaultBlockReward);
    static Ledger open(const std::string& database_path,
                       std::uint32_t difficulty = 2,
                       Amount block_reward = kDefaultBlockReward);

    Ledger(Ledger&&) noexcept = default;
    Ledger& operator=(Ledger&&) noexcept = default;
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    [[nodiscard]] Wallet create_wallet(const std::string& passphrase);
    void register_wallet(const Wallet& wallet, const std::string& passphrase);
    [[nodiscard]] Wallet unlock_wallet(const Address& address,
                                       const std::string& passphrase) const;
    [[nodiscard]] std::vector<std::pair<Address, PublicKey>> wallets() const;

    [[nodiscard]] Transaction create_transfer(const Wallet& sender,
                                               const Address& recipient,
                                               Amount amount,
                                               Amount fee) const;
    [[nodiscard]] bool submit(const Transaction& transaction,
                              std::string* error = nullptr);
    [[nodiscard]] bool can_submit(const Transaction& transaction,
                                  std::string* error = nullptr) const;
    [[nodiscard]] bool has_pending_transaction(const std::string& transaction_id) const;
    [[nodiscard]] std::optional<ConfirmedTransaction> find_confirmed_transaction(
        const std::string& transaction_id) const;
    [[nodiscard]] bool owns_wallet(const Wallet& wallet) const;
    [[nodiscard]] Block mine(const Address& miner);

    // Validate an entire contiguous peer batch against a staged chain, then
    // atomically commit the blocks and resulting durable mempool snapshot.
    void accept_blocks(const std::vector<Block>& blocks);

    // Apply a consensus-committed block batch and persist the corresponding
    // Raft snapshot in the same SQLite transaction. The Raft applied index and
    // Ledger effects therefore recover together after a process or host crash.
    [[nodiscard]] std::uint64_t commit_raft_application(
        const std::vector<Block>& blocks,
        const Bytes& raft_snapshot,
        std::uint64_t expected_raft_revision);
    // Atomically replace the local Ledger state with a validated Raft
    // application snapshot and persist its Raft metadata and snapshot bytes.
    [[nodiscard]] std::uint64_t install_raft_snapshot(
        const std::vector<Block>& complete_chain,
        const Bytes& raft_state,
        std::uint64_t expected_raft_revision,
        std::uint64_t snapshot_index,
        const Bytes& application_snapshot);

    // Validate a complete peer chain from the same genesis, apply the
    // deterministic fixed-difficulty fork-choice rule, and if preferred,
    // atomically replace the chain plus reconciled durable mempool.
    [[nodiscard]] ChainAdoptionResult consider_chain(
        const std::vector<Block>& complete_chain,
        const std::vector<std::pair<std::string, std::uint64_t>>& protected_transactions = {});

    [[nodiscard]] const Chain& chain() const noexcept { return chain_; }
    [[nodiscard]] const Mempool& mempool() const noexcept { return mempool_; }

private:
    Ledger(storage::SqliteStore store, Chain chain, Mempool mempool = {});

    storage::SqliteStore store_;
    Chain chain_;
    Mempool mempool_;
};

} // namespace feinstat::ledger
