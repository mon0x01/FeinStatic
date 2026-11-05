#include "feinstat/settlement/settlement_book.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <set>
#include <type_traits>
#include <utility>

namespace feinstat::settlement {
namespace {

Amount checked_add(Amount left, Amount right, const char* message) {
    if (left > UINT64_MAX - right) throw std::overflow_error(message);
    return left + right;
}

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

} // namespace

bool SettlementBook::valid_asset_id(const std::string& asset_id) noexcept {
    if (asset_id.empty() || asset_id.size() > 16) return false;
    for (const char raw : asset_id) {
        const auto c = static_cast<unsigned char>(raw);
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    }
    return true;
}

void SettlementBook::create_account(const std::string& account_id) {
    if (account_id.empty() || account_id.size() > 128) {
        throw std::invalid_argument("settlement account ID must contain 1 to 128 characters");
    }
    const auto [it, inserted] = accounts_.try_emplace(account_id);
    (void)it;
    if (!inserted) throw std::invalid_argument("settlement account already exists");
}

bool SettlementBook::has_account(const std::string& account_id) const noexcept {
    return accounts_.contains(account_id);
}

const std::vector<std::string> SettlementBook::accounts() const {
    std::vector<std::string> result;
    result.reserve(accounts_.size());
    for (const auto& [account, balances] : accounts_) {
        (void)balances;
        result.push_back(account);
    }
    return result;
}

void SettlementBook::require_account(const std::string& account_id) const {
    if (!accounts_.contains(account_id)) {
        throw std::invalid_argument("unknown settlement account: " + account_id);
    }
}

BalanceState SettlementBook::state(const std::string& account_id,
                                   const std::string& asset_id) const {
    require_account(account_id);
    if (!valid_asset_id(asset_id)) throw std::invalid_argument("invalid asset identifier");
    const auto& balances = accounts_.at(account_id);
    const auto it = balances.find(asset_id);
    return it == balances.end() ? BalanceState{} : it->second;
}

Amount SettlementBook::available(const std::string& account_id,
                                 const std::string& asset_id) const {
    return state(account_id, asset_id).available();
}

Amount SettlementBook::total(const std::string& account_id,
                             const std::string& asset_id) const {
    return state(account_id, asset_id).total;
}

Amount SettlementBook::reserved(const std::string& account_id,
                                const std::string& asset_id) const {
    return state(account_id, asset_id).reserved;
}

void SettlementBook::append_journal(const std::string& account_id,
                                    const std::string& asset_id,
                                    BalanceState new_state,
                                    const std::string& operation,
                                    const std::string& reference) {
    if (operation.empty() || operation.size() > 64 || reference.size() > 256) {
        throw std::invalid_argument("invalid settlement journal metadata");
    }
    if (next_journal_sequence_ == 0 || next_journal_sequence_ == UINT64_MAX) {
        throw std::overflow_error("settlement journal sequence exhausted");
    }
    journal_.push_back(JournalEntry{next_journal_sequence_, operation, account_id,
                                    asset_id, new_state.total, new_state.reserved, reference});
    ++next_journal_sequence_;
}

void SettlementBook::credit(const std::string& account_id, const std::string& asset_id,
                            Amount amount, const std::string& operation,
                            const std::string& reference) {
    require_account(account_id);
    if (!valid_asset_id(asset_id) || amount == 0) throw std::invalid_argument("invalid settlement credit");
    const BalanceState before = state(account_id, asset_id);
    const BalanceState after{checked_add(before.total, amount, "settlement balance overflow"), before.reserved};
    auto& balances = accounts_.at(account_id);
    const auto [it, inserted] = balances.try_emplace(asset_id, BalanceState{});
    try {
        append_journal(account_id, asset_id, after, operation, reference);
    } catch (...) {
        if (inserted) balances.erase(it);
        throw;
    }
    it->second = after;
}

void SettlementBook::debit_available(const std::string& account_id, const std::string& asset_id,
                                     Amount amount, const std::string& operation,
                                     const std::string& reference) {
    require_account(account_id);
    if (!valid_asset_id(asset_id) || amount == 0) throw std::invalid_argument("invalid settlement debit");
    const BalanceState before = state(account_id, asset_id);
    if (before.available() < amount) throw std::runtime_error("insufficient available settlement balance");
    const BalanceState after{before.total - amount, before.reserved};
    auto& balances = accounts_.at(account_id);
    const auto [it, inserted] = balances.try_emplace(asset_id, BalanceState{});
    try {
        append_journal(account_id, asset_id, after, operation, reference);
    } catch (...) {
        if (inserted) balances.erase(it);
        throw;
    }
    it->second = after;
}

void SettlementBook::reserve(const std::string& account_id, const std::string& asset_id,
                             Amount amount, const std::string& operation,
                             const std::string& reference) {
    require_account(account_id);
    if (!valid_asset_id(asset_id) || amount == 0) throw std::invalid_argument("invalid settlement reservation");
    const BalanceState before = state(account_id, asset_id);
    if (before.available() < amount) throw std::runtime_error("insufficient available balance to reserve");
    const BalanceState after{before.total, checked_add(before.reserved, amount, "reservation overflow")};
    auto& balances = accounts_.at(account_id);
    const auto [it, inserted] = balances.try_emplace(asset_id, BalanceState{});
    try {
        append_journal(account_id, asset_id, after, operation, reference);
    } catch (...) {
        if (inserted) balances.erase(it);
        throw;
    }
    it->second = after;
}

void SettlementBook::release(const std::string& account_id, const std::string& asset_id,
                             Amount amount, const std::string& operation,
                             const std::string& reference) {
    require_account(account_id);
    if (!valid_asset_id(asset_id) || amount == 0) throw std::invalid_argument("invalid settlement release");
    const BalanceState before = state(account_id, asset_id);
    if (before.reserved < amount) throw std::logic_error("settlement reservation underflow");
    const BalanceState after{before.total, before.reserved - amount};
    auto& balances = accounts_.at(account_id);
    const auto [it, inserted] = balances.try_emplace(asset_id, BalanceState{});
    try {
        append_journal(account_id, asset_id, after, operation, reference);
    } catch (...) {
        if (inserted) balances.erase(it);
        throw;
    }
    it->second = after;
}

void SettlementBook::transfer_available(const std::string& from_account,
                                         const std::string& to_account,
                                         const std::string& asset_id, Amount amount,
                                         const std::string& operation,
                                         const std::string& reference) {
    if (from_account == to_account) throw std::invalid_argument("settlement transfer requires distinct accounts");
    require_account(from_account);
    require_account(to_account);
    if (!valid_asset_id(asset_id) || amount == 0) throw std::invalid_argument("invalid settlement transfer");
    const BalanceState source = state(from_account, asset_id);
    const BalanceState destination = state(to_account, asset_id);
    if (source.available() < amount) throw std::runtime_error("insufficient available balance for settlement transfer");
    const BalanceState source_after{source.total - amount, source.reserved};
    const BalanceState destination_after{checked_add(destination.total, amount, "destination balance overflow"),
                                         destination.reserved};
    if (operation.empty() || operation.size() > 64 || reference.size() > 256 ||
        next_journal_sequence_ > UINT64_MAX - 2) {
        throw std::invalid_argument("invalid settlement transfer metadata or exhausted journal");
    }

    auto& source_balances = accounts_.at(from_account);
    auto& destination_balances = accounts_.at(to_account);
    const auto [source_it, source_inserted] = source_balances.try_emplace(asset_id, BalanceState{});
    bool destination_inserted = false;
    auto destination_it = destination_balances.end();
    try {
        const auto insertion = destination_balances.try_emplace(asset_id, BalanceState{});
        destination_it = insertion.first;
        destination_inserted = insertion.second;
        JournalEntry source_entry{next_journal_sequence_, operation, from_account, asset_id,
                                  source_after.total, source_after.reserved, reference};
        JournalEntry destination_entry{next_journal_sequence_ + 1, operation, to_account, asset_id,
                                       destination_after.total, destination_after.reserved, reference};
        journal_.reserve(journal_.size() + 2);
        journal_.push_back(std::move(source_entry));
        journal_.push_back(std::move(destination_entry));
    } catch (...) {
        if (source_inserted) source_balances.erase(source_it);
        if (destination_inserted && destination_it != destination_balances.end()) destination_balances.erase(destination_it);
        throw;
    }
    next_journal_sequence_ += 2;
    source_it->second = source_after;
    destination_it->second = destination_after;
}

void SettlementBook::replace_state(const std::string& account_id, const std::string& asset_id,
                                   BalanceState new_state, const std::string& operation,
                                   const std::string& reference) {
    require_account(account_id);
    if (!valid_asset_id(asset_id) || new_state.reserved > new_state.total) {
        throw std::invalid_argument("invalid settlement balance state");
    }
    auto& balances = accounts_.at(account_id);
    const auto [it, inserted] = balances.try_emplace(asset_id, BalanceState{});
    try {
        append_journal(account_id, asset_id, new_state, operation, reference);
    } catch (...) {
        if (inserted) balances.erase(it);
        throw;
    }
    it->second = new_state;
}

void SettlementBook::replace_states(const std::vector<BalanceUpdate>& updates,
                                    const std::string& operation,
                                    const std::string& reference) {
    if (updates.empty()) return;
    if (operation.empty() || operation.size() > 64 || reference.size() > 256) {
        throw std::invalid_argument("invalid settlement batch journal metadata");
    }
    if (updates.size() > UINT64_MAX - next_journal_sequence_) {
        throw std::overflow_error("settlement journal sequence exhausted");
    }
    if (updates.size() > journal_.max_size() - journal_.size()) {
        throw std::length_error("settlement journal capacity exhausted");
    }

    using BalanceMap = AccountBalances;
    using BalanceIterator = BalanceMap::iterator;
    struct PreparedBalance {
        BalanceIterator iterator;
        bool inserted;
    };

    std::set<std::pair<std::string, std::string>> unique_keys;
    std::vector<JournalEntry> prepared_entries;
    std::vector<PreparedBalance> prepared_balances;
    prepared_entries.reserve(updates.size());
    prepared_balances.reserve(updates.size());

    // Validate and construct all journal payloads before touching canonical
    // maps. Duplicate keys would make the resulting-state semantics ambiguous.
    for (std::size_t i = 0; i < updates.size(); ++i) {
        const auto& update = updates[i];
        require_account(update.account_id);
        if (!valid_asset_id(update.asset_id) || update.state.reserved > update.state.total) {
            throw std::invalid_argument("invalid settlement balance in batch");
        }
        if (!unique_keys.emplace(update.account_id, update.asset_id).second) {
            throw std::invalid_argument("duplicate account/asset in settlement batch");
        }
        prepared_entries.push_back(JournalEntry{
            next_journal_sequence_ + static_cast<std::uint64_t>(i), operation,
            update.account_id, update.asset_id, update.state.total, update.state.reserved, reference});
    }

    static_assert(std::is_nothrow_move_constructible_v<JournalEntry>);
    const std::size_t required_journal_size = journal_.size() + updates.size();
    if (required_journal_size > journal_.capacity()) {
        const std::size_t maximum_capacity = journal_.max_size();
        const std::size_t current_capacity = journal_.capacity();
        std::size_t next_capacity = current_capacity == 0 ? 8
            : (current_capacity > maximum_capacity / 2 ? maximum_capacity : current_capacity * 2);
        if (next_capacity < required_journal_size) next_capacity = required_journal_size;
        journal_.reserve(next_capacity);
    }

    // Pre-create any previously absent asset slots. If a map allocation fails,
    // erase only slots inserted by this batch and leave all existing states and
    // journal entries untouched.
    try {
        for (const auto& update : updates) {
            auto& balances = accounts_.at(update.account_id);
            const auto [it, inserted] = balances.try_emplace(update.asset_id, BalanceState{});
            prepared_balances.push_back(PreparedBalance{it, inserted});
        }
    } catch (...) {
        for (std::size_t i = 0; i < prepared_balances.size(); ++i) {
            if (prepared_balances[i].inserted) {
                const auto& update = updates[i];
                accounts_.at(update.account_id).erase(prepared_balances[i].iterator);
            }
        }
        throw;
    }

    // Capacity was reserved and JournalEntry has a noexcept move constructor;
    // from here through the balance assignments there are no throwing steps.
    for (auto& entry : prepared_entries) journal_.push_back(std::move(entry));
    for (std::size_t i = 0; i < updates.size(); ++i) {
        prepared_balances[i].iterator->second = updates[i].state;
    }
    next_journal_sequence_ += static_cast<std::uint64_t>(updates.size());
}

Snapshot SettlementBook::snapshot() const {
    Snapshot result;
    result.accounts.reserve(accounts_.size());
    for (const auto& [account, balances] : accounts_) {
        result.accounts.push_back(account);
        for (const auto& [asset, balance] : balances) {
            result.balances.push_back(BalanceRecord{account, asset, balance});
        }
    }
    result.journal = journal_;
    result.next_journal_sequence = next_journal_sequence_;
    return result;
}

SettlementBook SettlementBook::from_snapshot(const Snapshot& snapshot_value) {
    if (snapshot_value.accounts.size() > 100'000 || snapshot_value.balances.size() > 1'000'000 ||
        snapshot_value.journal.size() > 5'000'000) {
        throw std::runtime_error("settlement snapshot exceeds configured limits");
    }
    SettlementBook result;
    for (const auto& account : snapshot_value.accounts) {
        result.create_account(account);
    }
    for (const auto& record : snapshot_value.balances) {
        if (!result.has_account(record.account_id) || !valid_asset_id(record.asset_id) ||
            record.state.reserved > record.state.total) {
            throw std::runtime_error("invalid balance record in settlement snapshot");
        }
        auto& balances = result.accounts_.at(record.account_id);
        if (!balances.emplace(record.asset_id, record.state).second) {
            throw std::runtime_error("duplicate balance record in settlement snapshot");
        }
    }
    std::uint64_t expected = 1;
    for (const auto& entry : snapshot_value.journal) {
        if (entry.sequence != expected || !result.has_account(entry.account_id) ||
            !valid_asset_id(entry.asset_id) || entry.operation.empty() ||
            entry.operation.size() > 64 || entry.reference.size() > 256 ||
            entry.reserved_after > entry.total_after) {
            throw std::runtime_error("invalid settlement journal in snapshot");
        }
        ++expected;
    }
    if (snapshot_value.next_journal_sequence != expected) {
        throw std::runtime_error("settlement journal sequence metadata is inconsistent");
    }
    result.journal_ = snapshot_value.journal;
    result.next_journal_sequence_ = snapshot_value.next_journal_sequence;
    std::string error;
    if (!result.validate(&error)) throw std::runtime_error("settlement snapshot validation failed: " + error);
    return result;
}

bool SettlementBook::validate(std::string* error) const {
    for (const auto& [account, balances] : accounts_) {
        if (account.empty() || account.size() > 128) {
            set_error(error, "invalid settlement account identifier");
            return false;
        }
        for (const auto& [asset, balance] : balances) {
            if (!valid_asset_id(asset) || balance.reserved > balance.total) {
                set_error(error, "invalid asset balance or reservation");
                return false;
            }
        }
    }
    std::uint64_t expected = 1;
    std::map<std::pair<std::string, std::string>, BalanceState> latest_states;
    for (const auto& entry : journal_) {
        if (entry.sequence != expected++ || !accounts_.contains(entry.account_id) ||
            !valid_asset_id(entry.asset_id) || entry.reserved_after > entry.total_after ||
            entry.operation.empty() || entry.operation.size() > 64 || entry.reference.size() > 256) {
            set_error(error, "settlement journal is inconsistent");
            return false;
        }
        latest_states[{entry.account_id, entry.asset_id}] = BalanceState{entry.total_after, entry.reserved_after};
    }
    for (const auto& [account_id, balances] : accounts_) {
        for (const auto& [asset_id, current] : balances) {
            const auto latest = latest_states.find({account_id, asset_id});
            if (latest == latest_states.end() || latest->second != current) {
                set_error(error, "settlement balance does not match its latest journal entry");
                return false;
            }
        }
    }
    if (next_journal_sequence_ != expected) {
        set_error(error, "settlement journal sequence is inconsistent");
        return false;
    }
    return true;
}

} // namespace feinstat::settlement
