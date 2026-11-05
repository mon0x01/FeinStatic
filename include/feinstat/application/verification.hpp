#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace feinstat::application {

// Summary of a non-interactive integrity check against an existing FeinStatic
// database. This performs recovery and invariant validation; it does not
// create accounts, credit balances, or mutate financial state.
struct DatabaseVerificationReport {
    bool ledger_opened{false};
    bool ledger_valid{false};
    std::string ledger_error;
    std::uint64_t chain_height{0};
    std::uint64_t block_count{0};
    std::uint64_t native_supply{0};
    std::size_t pending_transactions{0};
    std::size_t wallet_count{0};

    bool platform_opened{false};
    bool platform_valid{false};
    std::string platform_error;
    std::uint64_t platform_revision{0};
    std::size_t platform_account_count{0};
    std::size_t settlement_journal_entries{0};
    std::size_t match_order_count{0};
    std::size_t match_trade_count{0};
    std::size_t flow_pool_count{0};
    std::size_t flow_lp_position_count{0};
    std::size_t flow_swap_count{0};

    bool bridge_configured{false};
    std::string bridge_vault_address;
    std::uint64_t bridge_vault_balance{0};
    std::uint64_t bridge_fst_liabilities{0};
    std::uint64_t bridge_pending_withdrawal_reserve{0};
    std::uint64_t bridge_required_collateral{0};
    std::uint64_t bridge_collateral_shortfall{0};
    bool bridge_fully_collateralized{false};
    std::size_t bridge_deposit_count{0};
    std::size_t bridge_pending_withdrawal_count{0};
    std::size_t bridge_pending_awaiting_reconciliation_count{0};
    std::size_t bridge_confirmed_withdrawal_count{0};

    [[nodiscard]] bool success() const noexcept {
        return ledger_opened && ledger_valid && platform_opened && platform_valid;
    }
};

// Opens an existing database, validates the recovered Ledger chain and the
// recovered shared Settlement/Match/Flow state, and returns a machine-readable
// summary. An invalid component is represented in the report; it does not
// throw away the diagnostics. A missing/unopenable component is reported too.
[[nodiscard]] DatabaseVerificationReport verify_database(const std::string& database_path);

} // namespace feinstat::application
