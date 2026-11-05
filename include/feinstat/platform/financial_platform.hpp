#pragma once

#include "feinstat/exchange/matching_engine.hpp"
#include "feinstat/flow/amm.hpp"
#include "feinstat/ledger/transaction.hpp"
#include "feinstat/settlement/settlement_book.hpp"
#include "feinstat/storage/sqlite_store.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace feinstat::bridge { class BridgeService; }

namespace feinstat::platform {

enum class BridgeWithdrawalStatus : std::uint8_t { Pending = 0, Confirmed = 1 };

struct BridgeDepositRecord {
    std::string transaction_id;
    std::string account_id;
    Amount amount{0};
    std::uint64_t block_height{0};
};

struct BridgeWithdrawalRecord {
    std::string transaction_id;
    std::string account_id;
    Amount amount{0};
    Amount fee{0};
    Bytes signed_transaction;
    BridgeWithdrawalStatus status{BridgeWithdrawalStatus::Pending};
    std::uint64_t block_height{0};
};

struct BridgeSnapshot {
    std::string vault_address;
    std::vector<BridgeDepositRecord> deposits;
    std::vector<BridgeWithdrawalRecord> withdrawals;
};

struct AccountBalance {
    Amount total{0};
    Amount reserved{0};
    [[nodiscard]] Amount available() const noexcept { return total - reserved; }
};

// Durable coordinator for the off-chain Match and Flow engines. Both engines
// share one settlement book; every mutating operation is applied to a candidate
// state and then committed as one revisioned SQLite snapshot.
class FinancialPlatform {
public:
    static constexpr const char* kBaseAsset = "FST";
    static constexpr const char* kQuoteAsset = "USD";
    static constexpr Amount kBaseLotSize = kAtomsPerCoin;

    static FinancialPlatform open(const std::string& ledger_database_path);

    FinancialPlatform(FinancialPlatform&&) noexcept = default;
    FinancialPlatform& operator=(FinancialPlatform&&) noexcept = default;
    FinancialPlatform(const FinancialPlatform&) = delete;
    FinancialPlatform& operator=(const FinancialPlatform&) = delete;

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::vector<std::string> accounts() const { return state_.settlement->accounts(); }
    [[nodiscard]] AccountBalance balance(const std::string& account_id,
                                         const std::string& asset_id) const;
    [[nodiscard]] const settlement::SettlementBook& settlement_book() const noexcept { return *state_.settlement; }
    [[nodiscard]] const exchange::MatchingEngine& match() const noexcept { return state_.match; }
    [[nodiscard]] const flow::AmmEngine& flow() const noexcept { return state_.flow; }

    void create_account(const std::string& account_id);
    // Creates synthetic off-chain test/demo balances. This is deliberately not
    // a Ledger deposit and must never be represented as on-chain issuance.
    void fund_test_asset(const std::string& account_id, const std::string& asset_id,
                         Amount amount, const std::string& reason);
    void transfer_asset(const std::string& from_account, const std::string& to_account,
                        const std::string& asset_id, Amount amount);

    [[nodiscard]] const BridgeSnapshot& bridge() const noexcept { return state_.bridge; }
    // Transaction IDs and confirmation heights that must remain canonical
    // across Ledger reorganization because Settlement records depend on them.
    [[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>> bridge_transaction_references() const;
    [[nodiscard]] Amount total_fst_liabilities() const;

    [[nodiscard]] exchange::OrderId submit_limit(const std::string& account_id,
                                                  exchange::Side side,
                                                  exchange::Price price,
                                                  exchange::Quantity quantity);
    [[nodiscard]] exchange::OrderId submit_market_buy(const std::string& account_id, Amount quote_budget);
    [[nodiscard]] exchange::OrderId submit_market_sell(const std::string& account_id,
                                                        exchange::Quantity quantity);
    [[nodiscard]] bool cancel_order(const std::string& account_id, exchange::OrderId order_id);

    [[nodiscard]] std::string create_pool(const std::string& asset_x,
                                          const std::string& asset_y,
                                          std::uint32_t fee_bps = flow::kDefaultFeeBps);
    [[nodiscard]] flow::LiquidityResult add_liquidity(const std::string& account_id,
                                                       const std::string& pool_id,
                                                       Amount max_amount_x,
                                                       Amount max_amount_y,
                                                       Amount minimum_shares = 1);
    [[nodiscard]] flow::LiquidityResult remove_liquidity(const std::string& account_id,
                                                          const std::string& pool_id,
                                                          Amount shares);
    [[nodiscard]] flow::SwapQuote quote(const std::string& pool_id,
                                        const std::string& asset_in,
                                        Amount amount_in) const;
    [[nodiscard]] flow::SwapRecord swap(const std::string& account_id,
                                        const std::string& pool_id,
                                        const std::string& asset_in,
                                        Amount amount_in,
                                        Amount minimum_amount_out);

    [[nodiscard]] bool validate(std::string* error = nullptr) const;

private:
    friend class ::feinstat::bridge::BridgeService;

    void configure_bridge_vault(const std::string& vault_address);
    [[nodiscard]] bool apply_confirmed_bridge_deposit(const std::string& transaction_id,
                                                       const std::string& account_id,
                                                       Amount amount,
                                                       std::uint64_t block_height);
    void prepare_bridge_withdrawal(const ledger::Transaction& transaction);
    [[nodiscard]] bool confirm_bridge_withdrawal(const std::string& transaction_id,
                                                  std::uint64_t block_height);

    struct RuntimeState {
        explicit RuntimeState(std::shared_ptr<settlement::SettlementBook> book);
        [[nodiscard]] RuntimeState clone() const;
        std::shared_ptr<settlement::SettlementBook> settlement;
        exchange::MatchingEngine match;
        flow::AmmEngine flow;
        BridgeSnapshot bridge;
    };

    FinancialPlatform(storage::SqliteStore store, RuntimeState state, std::uint64_t revision);
    void persist_and_commit(RuntimeState&& candidate);
    [[nodiscard]] static bool validate_state(const RuntimeState& state, std::string* error);
    [[nodiscard]] static Bytes serialize_state(const RuntimeState& state);
    [[nodiscard]] static RuntimeState deserialize_state(const Bytes& bytes);

    storage::SqliteStore store_;
    RuntimeState state_;
    std::uint64_t revision_{0};
};

} // namespace feinstat::platform
