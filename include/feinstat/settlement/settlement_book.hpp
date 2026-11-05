#pragma once

#include "feinstat/common/types.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace feinstat::settlement {

struct BalanceState {
    Amount total{0};
    Amount reserved{0};

    [[nodiscard]] Amount available() const noexcept { return total - reserved; }
    friend bool operator==(const BalanceState&, const BalanceState&) = default;
};

struct BalanceRecord {
    std::string account_id;
    std::string asset_id;
    BalanceState state;
};

struct BalanceUpdate {
    std::string account_id;
    std::string asset_id;
    BalanceState state;
};

struct JournalEntry {
    std::uint64_t sequence{0};
    std::string operation;
    std::string account_id;
    std::string asset_id;
    Amount total_after{0};
    Amount reserved_after{0};
    std::string reference;
};

struct Snapshot {
    std::vector<std::string> accounts;
    std::vector<BalanceRecord> balances;
    std::vector<JournalEntry> journal;
    std::uint64_t next_journal_sequence{1};
};

// Canonical, shared balance state for the off-chain Match and Flow engines.
// Balances are integer asset atoms. Reserved funds remain part of total but
// cannot be spent by either engine until released or settled.
class SettlementBook {
public:
    void create_account(const std::string& account_id);
    [[nodiscard]] bool has_account(const std::string& account_id) const noexcept;
    [[nodiscard]] const std::vector<std::string> accounts() const;

    [[nodiscard]] BalanceState state(const std::string& account_id,
                                     const std::string& asset_id) const;
    [[nodiscard]] Amount available(const std::string& account_id,
                                   const std::string& asset_id) const;
    [[nodiscard]] Amount total(const std::string& account_id,
                               const std::string& asset_id) const;
    [[nodiscard]] Amount reserved(const std::string& account_id,
                                  const std::string& asset_id) const;

    // Explicit funding is intended for private test/demo accounts only. The
    // source string is recorded in the append-only journal.
    void credit(const std::string& account_id, const std::string& asset_id,
                Amount amount, const std::string& operation,
                const std::string& reference = {});
    void debit_available(const std::string& account_id, const std::string& asset_id,
                         Amount amount, const std::string& operation,
                         const std::string& reference = {});
    void reserve(const std::string& account_id, const std::string& asset_id,
                 Amount amount, const std::string& operation,
                 const std::string& reference = {});
    void release(const std::string& account_id, const std::string& asset_id,
                 Amount amount, const std::string& operation,
                 const std::string& reference = {});
    void transfer_available(const std::string& from_account, const std::string& to_account,
                            const std::string& asset_id, Amount amount,
                            const std::string& operation,
                            const std::string& reference = {});

    // Used by deterministic engine preflight/commit. Rejects impossible
    // reserved > total states and journals the resulting state.
    void replace_state(const std::string& account_id, const std::string& asset_id,
                       BalanceState new_state, const std::string& operation,
                       const std::string& reference = {});
    // Applies multiple resulting balances as one in-memory journal transaction.
    // All allocations/validation happen before any balance or journal mutation;
    // on failure the book is unchanged.
    void replace_states(const std::vector<BalanceUpdate>& updates,
                        const std::string& operation,
                        const std::string& reference = {});

    [[nodiscard]] const std::vector<JournalEntry>& journal() const noexcept { return journal_; }
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] static SettlementBook from_snapshot(const Snapshot& snapshot);
    [[nodiscard]] bool validate(std::string* error = nullptr) const;

    [[nodiscard]] static bool valid_asset_id(const std::string& asset_id) noexcept;

private:
    using AccountBalances = std::map<std::string, BalanceState>;
    void require_account(const std::string& account_id) const;
    void append_journal(const std::string& account_id, const std::string& asset_id,
                        BalanceState state, const std::string& operation,
                        const std::string& reference);

    std::map<std::string, AccountBalances> accounts_;
    std::vector<JournalEntry> journal_;
    std::uint64_t next_journal_sequence_{1};
};

} // namespace feinstat::settlement
