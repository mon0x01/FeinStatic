#include "feinstat/ledger/chain.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace feinstat::ledger {
namespace {

void set_error(std::string* output, const std::string& message) {
    if (output != nullptr) *output = message;
}

bool checked_add(Amount left, Amount right, Amount& result) {
    if (left > UINT64_MAX - right) return false;
    result = left + right;
    return true;
}

} // namespace

Chain::Chain(const std::vector<GenesisAllocation>& allocations,
             std::uint32_t difficulty,
             Amount block_reward)
    : difficulty_(difficulty), block_reward_(block_reward) {
    if (difficulty > 8) throw std::invalid_argument("difficulty must be between 0 and 8 hex zeroes");
    if (block_reward == 0) throw std::invalid_argument("block reward must be positive");
    if (allocations.empty()) throw std::invalid_argument("genesis requires at least one allocation");

    for (const auto& allocation : allocations) {
        if (!crypto::is_valid_address(allocation.address)) throw std::invalid_argument("invalid genesis address");
        if (allocation.amount == 0) throw std::invalid_argument("genesis allocation must be positive");
        if (accounts_.contains(allocation.address)) throw std::invalid_argument("duplicate genesis address");
        Amount updated_supply = 0;
        if (!checked_add(total_supply_, allocation.amount, updated_supply)) {
            throw std::overflow_error("genesis supply overflows amount type");
        }
        total_supply_ = updated_supply;
        accounts_.emplace(allocation.address, AccountState{allocation.amount, 0});
    }
    blocks_.push_back(Block::make_genesis(allocations));
}

Chain Chain::from_blocks(const std::vector<Block>& blocks,
                         std::uint32_t difficulty,
                         Amount block_reward) {
    if (blocks.empty()) throw std::invalid_argument("database contains no genesis block");
    const Block& genesis = blocks.front();
    if (genesis.height != 0 || genesis.timestamp != 0 || genesis.difficulty != 0 ||
        genesis.reward != 0 || !genesis.miner.empty() || !genesis.transactions.empty() ||
        genesis.previous_hash != Hash{} || genesis.hash != genesis.calculate_hash()) {
        throw std::runtime_error("invalid genesis block");
    }

    Chain result(genesis.genesis_allocations, difficulty, block_reward);
    if (result.blocks_.front().serialize() != genesis.serialize()) {
        throw std::runtime_error("genesis block does not match canonical genesis state");
    }
    for (std::size_t i = 1; i < blocks.size(); ++i) result.append(blocks[i]);
    return result;
}

bool Chain::apply_transactions(Accounts& accounts,
                               const std::vector<Transaction>& transactions,
                               Amount& fees,
                               std::string* error) {
    fees = 0;
    for (const auto& transaction : transactions) {
        if (!transaction.verify_signature()) {
            set_error(error, "invalid transaction signature or transaction fields");
            return false;
        }
        if (transaction.amount > UINT64_MAX - transaction.fee) {
            set_error(error, "transaction amount plus fee overflows");
            return false;
        }
        const Amount debit = transaction.amount + transaction.fee;
        auto sender_it = accounts.find(transaction.sender);
        if (sender_it == accounts.end()) {
            set_error(error, "sender account does not exist or has no prior balance");
            return false;
        }
        if (sender_it->second.nonce == UINT64_MAX || transaction.nonce != sender_it->second.nonce + 1) {
            set_error(error, "incorrect sender nonce");
            return false;
        }
        if (sender_it->second.balance < debit) {
            set_error(error, "insufficient balance");
            return false;
        }

        auto recipient_it = accounts.find(transaction.recipient);
        const Amount current_recipient_balance = recipient_it == accounts.end()
            ? 0
            : recipient_it->second.balance;
        const Amount recipient_base = transaction.recipient == transaction.sender
            ? current_recipient_balance - debit
            : current_recipient_balance;
        Amount recipient_balance = 0;
        if (!checked_add(recipient_base, transaction.amount, recipient_balance)) {
            set_error(error, "recipient balance overflow");
            return false;
        }
        Amount updated_fees = 0;
        if (!checked_add(fees, transaction.fee, updated_fees)) {
            set_error(error, "block transaction fees overflow");
            return false;
        }

        // All validation for this transaction is complete before modifying the local state copy.
        sender_it->second.balance -= debit;
        sender_it->second.nonce = transaction.nonce;
        accounts[transaction.recipient].balance = recipient_balance;
        fees = updated_fees;
    }
    return true;
}

bool Chain::validate_transaction_batch(const std::vector<Transaction>& transactions,
                                       std::string* error) const {
    Accounts projected = accounts_;
    Amount ignored_fees = 0;
    std::unordered_set<std::string> seen;
    for (const auto& transaction : transactions) {
        const auto id_hash = transaction.id();
        const std::string id = crypto::hex(id_hash.data(), id_hash.size());
        if (transaction_ids_.contains(id) || !seen.insert(id).second) {
            set_error(error, "duplicate or replayed transaction");
            return false;
        }
    }
    return apply_transactions(projected, transactions, ignored_fees, error);
}

bool Chain::apply_block_to_state(const Block& block,
                                 Accounts& accounts,
                                 Amount& supply,
                                 std::string* error) const {
    Amount fees = 0;
    if (!apply_transactions(accounts, block.transactions, fees, error)) return false;

    Amount miner_credit = 0;
    if (!checked_add(block.reward, fees, miner_credit)) {
        set_error(error, "miner reward plus fees overflow");
        return false;
    }
    const auto miner_it = accounts.find(block.miner);
    const Amount miner_balance = miner_it == accounts.end() ? 0 : miner_it->second.balance;
    Amount updated_miner_balance = 0;
    if (!checked_add(miner_balance, miner_credit, updated_miner_balance)) {
        set_error(error, "miner balance overflow");
        return false;
    }
    Amount updated_supply = 0;
    if (!checked_add(supply, block.reward, updated_supply)) {
        set_error(error, "total supply overflow");
        return false;
    }
    accounts[block.miner].balance = updated_miner_balance;
    supply = updated_supply;
    return true;
}

bool Chain::can_append(const Block& block, std::string* error) const {
    if (block.height != blocks_.size()) {
        set_error(error, "block height does not extend current chain");
        return false;
    }
    if (block.previous_hash != blocks_.back().hash) {
        set_error(error, "previous block hash mismatch");
        return false;
    }
    if (block.timestamp <= blocks_.back().timestamp) {
        set_error(error, "block timestamp must increase");
        return false;
    }
    if (block.difficulty != difficulty_) {
        set_error(error, "block difficulty does not match chain configuration");
        return false;
    }
    if (!crypto::is_valid_address(block.miner)) {
        set_error(error, "invalid miner address");
        return false;
    }
    if (block.reward != block_reward_) {
        set_error(error, "incorrect block reward");
        return false;
    }
    if (!block.genesis_allocations.empty()) {
        set_error(error, "non-genesis block cannot contain genesis allocations");
        return false;
    }
    if (!block.has_valid_proof_of_work()) {
        set_error(error, "invalid block hash or proof of work");
        return false;
    }

    std::unordered_set<std::string> seen;
    for (const auto& transaction : block.transactions) {
        const auto id_hash = transaction.id();
        const std::string id = crypto::hex(id_hash.data(), id_hash.size());
        if (transaction_ids_.contains(id) || !seen.insert(id).second) {
            set_error(error, "block contains duplicate or replayed transaction");
            return false;
        }
    }

    Accounts projected = accounts_;
    Amount projected_supply = total_supply_;
    return apply_block_to_state(block, projected, projected_supply, error);
}

void Chain::append(const Block& block) {
    std::string error;
    if (!can_append(block, &error)) throw std::runtime_error("cannot append block: " + error);

    Accounts updated_accounts = accounts_;
    Amount updated_supply = total_supply_;
    if (!apply_block_to_state(block, updated_accounts, updated_supply, &error)) {
        throw std::runtime_error("block state transition failed: " + error);
    }

    // Stage every allocation before committing any logical state. In particular,
    // a failed set/vector allocation must not leave balances and the block list apart.
    auto updated_transaction_ids = transaction_ids_;
    for (const auto& transaction : block.transactions) {
        const auto id_hash = transaction.id();
        updated_transaction_ids.insert(crypto::hex(id_hash.data(), id_hash.size()));
    }
    Block staged_block = block;
    blocks_.reserve(blocks_.size() + 1);

    accounts_.swap(updated_accounts);
    transaction_ids_.swap(updated_transaction_ids);
    total_supply_ = updated_supply;
    blocks_.push_back(std::move(staged_block));
}

Block Chain::make_next_block(const std::vector<Transaction>& transactions,
                             const Address& miner) const {
    if (!crypto::is_valid_address(miner)) throw std::invalid_argument("invalid miner address");
    Block block;
    block.height = static_cast<std::uint64_t>(blocks_.size());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now < 0) throw std::runtime_error("system clock is before Unix epoch");
    block.timestamp = static_cast<std::uint64_t>(now);
    if (block.timestamp <= blocks_.back().timestamp) {
        if (blocks_.back().timestamp == UINT64_MAX) throw std::overflow_error("block timestamp space exhausted");
        block.timestamp = blocks_.back().timestamp + 1;
    }
    block.previous_hash = blocks_.back().hash;
    block.difficulty = difficulty_;
    block.miner = miner;
    block.reward = block_reward_;
    block.transactions = transactions;
    block.mine(difficulty_);
    return block;
}

std::optional<ConfirmedTransaction> Chain::find_transaction(const std::string& transaction_id) const {
    if (transaction_id.size() != 64) return std::nullopt;
    for (const auto& block : blocks_) {
        if (block.height == 0) continue;
        for (const auto& transaction : block.transactions) {
            const auto id_hash = transaction.id();
            const std::string id = crypto::hex(id_hash.data(), id_hash.size());
            if (id == transaction_id) return ConfirmedTransaction{transaction, block.height};
        }
    }
    return std::nullopt;
}

Amount Chain::balance_of(const Address& address) const noexcept {
    const auto it = accounts_.find(address);
    return it == accounts_.end() ? 0 : it->second.balance;
}

std::uint64_t Chain::nonce_of(const Address& address) const noexcept {
    const auto it = accounts_.find(address);
    return it == accounts_.end() ? 0 : it->second.nonce;
}

std::uint64_t Chain::height() const noexcept {
    return blocks_.empty() ? 0 : static_cast<std::uint64_t>(blocks_.size() - 1);
}

bool Chain::validate(std::string* error) const {
    try {
        const Chain replayed = Chain::from_blocks(blocks_, difficulty_, block_reward_);
        if (replayed.accounts_ != accounts_ || replayed.total_supply_ != total_supply_ ||
            replayed.transaction_ids_ != transaction_ids_) {
            set_error(error, "in-memory state differs from replayed chain state");
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    }
}

} // namespace feinstat::ledger
