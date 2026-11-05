#include "feinstat/application/verification.hpp"

#include "feinstat/bridge/bridge_service.hpp"

#include "feinstat/ledger/ledger.hpp"
#include "feinstat/platform/financial_platform.hpp"

#include <exception>

namespace feinstat::application {

DatabaseVerificationReport verify_database(const std::string& database_path) {
    DatabaseVerificationReport report;

    try {
        auto ledger = ledger::Ledger::open(database_path);
        report.ledger_opened = true;
        report.chain_height = ledger.chain().height();
        report.block_count = ledger.chain().blocks().size();
        report.native_supply = ledger.chain().total_supply();
        report.pending_transactions = ledger.mempool().transactions().size();
        report.wallet_count = ledger.wallets().size();
        report.ledger_valid = ledger.chain().validate(&report.ledger_error);
    } catch (const std::exception& exception) {
        report.ledger_error = exception.what();
    } catch (...) {
        report.ledger_error = "unknown error while opening or validating Ledger";
    }

    // Do not try to recover/migrate the shared platform if the Ledger database
    // itself could not be opened. This keeps diagnostics clear and avoids
    // performing schema initialization after a failed Ledger open.
    if (!report.ledger_opened || !report.ledger_valid) return report;

    try {
        auto financial = platform::FinancialPlatform::open(database_path);
        report.platform_opened = true;
        report.platform_revision = financial.revision();
        report.platform_account_count = financial.accounts().size();
        report.settlement_journal_entries = financial.settlement_book().journal().size();

        const auto match_snapshot = financial.match().snapshot();
        report.match_order_count = match_snapshot.orders.size();
        report.match_trade_count = match_snapshot.trades.size();

        const auto flow_snapshot = financial.flow().snapshot();
        report.flow_pool_count = flow_snapshot.pools.size();
        report.flow_lp_position_count = flow_snapshot.lp_positions.size();
        report.flow_swap_count = flow_snapshot.swaps.size();

        const auto& bridge_state = financial.bridge();
        report.bridge_configured = !bridge_state.vault_address.empty();
        if (report.bridge_configured) {
            report.bridge_vault_address = bridge_state.vault_address;
            auto ledger_for_bridge = ledger::Ledger::open(database_path);
            bridge::BridgeService bridge_service(ledger_for_bridge, financial);
            bridge_service.validate_history();
            const auto bridge_status = bridge_service.status();
            report.bridge_vault_balance = bridge_status.vault_on_chain_balance;
            report.bridge_fst_liabilities = bridge_status.off_chain_fst_liabilities;
            report.bridge_pending_withdrawal_reserve = bridge_status.pending_withdrawal_reserve;
            report.bridge_required_collateral = bridge_status.required_collateral;
            report.bridge_collateral_shortfall = bridge_status.collateral_shortfall;
            report.bridge_fully_collateralized = bridge_status.fully_collateralized;
            report.bridge_pending_awaiting_reconciliation_count =
                bridge_status.pending_awaiting_reconciliation;
        }
        report.bridge_deposit_count = bridge_state.deposits.size();
        for (const auto& withdrawal : bridge_state.withdrawals) {
            if (withdrawal.status == platform::BridgeWithdrawalStatus::Pending) {
                ++report.bridge_pending_withdrawal_count;
            } else {
                ++report.bridge_confirmed_withdrawal_count;
            }
        }

        report.platform_valid = financial.validate(&report.platform_error);
    } catch (const std::exception& exception) {
        report.platform_error = exception.what();
    } catch (...) {
        report.platform_error = "unknown error while opening or validating shared platform";
    }

    return report;
}

} // namespace feinstat::application
