#pragma once

#include "feinstat/ledger/ledger.hpp"
#include "feinstat/platform/financial_platform.hpp"

#include <cstddef>
#include <string>

namespace feinstat::bridge {

struct Status {
    bool configured{false};
    std::string vault_address;
    Amount vault_on_chain_balance{0};
    Amount off_chain_fst_liabilities{0};
    Amount pending_withdrawal_reserve{0};
    Amount untracked_vault_mempool_reserve{0};
    Amount required_collateral{0};
    Amount collateral_surplus{0};
    Amount collateral_shortfall{0};
    bool fully_collateralized{false};
    std::size_t deposits{0};
    std::size_t pending_withdrawals{0};
    std::size_t pending_awaiting_reconciliation{0};
    std::size_t untracked_vault_mempool_transactions{0};
    std::size_t confirmed_withdrawals{0};
};

struct DepositClaimResult {
    std::string transaction_id;
    std::string account_id;
    Amount amount{0};
    std::uint64_t block_height{0};
    bool newly_credited{false};
};

struct WithdrawalRequestResult {
    std::string transaction_id;
    Amount amount{0};
    Amount fee{0};
    bool submitted_to_mempool{false};
    std::string submission_error;
};

struct RetryResult {
    std::size_t submitted{0};
    std::size_t already_pending{0};
    std::size_t already_confirmed{0};
    std::vector<std::string> errors;
};

// Coordinates the single-node native-FST bridge boundary between confirmed
// Ledger transactions and the persistent off-chain settlement snapshot.
// Deposits are idempotently credited by confirmed transaction ID. Withdrawals
// debit settlement and persist the exact signed transaction before mempool
// submission, making interrupted submission recoverable without re-signing.
class BridgeService {
public:
    BridgeService(ledger::Ledger& ledger, platform::FinancialPlatform& financial)
        : ledger_(ledger), financial_(financial) {}

    void configure_vault(const std::string& vault_address);
    [[nodiscard]] Status status() const;
    // Cross-checks durable bridge history against the recovered Ledger chain.
    void validate_history() const;
    [[nodiscard]] DepositClaimResult claim_deposit(const std::string& transaction_id);
    [[nodiscard]] WithdrawalRequestResult request_withdrawal(
        const std::string& account_id, Amount amount, Amount fee,
        const ledger::Wallet& vault_wallet);
    [[nodiscard]] RetryResult retry_pending_withdrawals();
    [[nodiscard]] std::size_t reconcile_confirmed_withdrawals();

private:
    [[nodiscard]] bool is_registered_wallet(const std::string& address) const;
    void verify_confirmed_withdrawal(const platform::BridgeWithdrawalRecord& record,
                                     const ledger::ConfirmedTransaction& confirmed) const;

    ledger::Ledger& ledger_;
    platform::FinancialPlatform& financial_;
};

} // namespace feinstat::bridge
