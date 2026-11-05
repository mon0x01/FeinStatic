#include "feinstat/bridge/bridge_service.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <limits>
#include <set>
#include <stdexcept>

namespace feinstat::bridge {
namespace {

Amount checked_add(Amount left, Amount right, const char* message) {
    if (left > UINT64_MAX - right) throw std::overflow_error(message);
    return left + right;
}

bool valid_transaction_id(const std::string& id) {
    if (id.size() != 64) return false;
    for (const char raw_character : id) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) return false;
    }
    return true;
}

std::string transaction_id(const ledger::Transaction& transaction) {
    const auto hash = transaction.id();
    return crypto::hex(hash.data(), hash.size());
}

} // namespace

bool BridgeService::is_registered_wallet(const std::string& address) const {
    const auto wallets = ledger_.wallets();
    for (const auto& [stored_address, public_key] : wallets) {
        (void)public_key;
        if (stored_address == address) return true;
    }
    return false;
}

void BridgeService::configure_vault(const std::string& vault_address) {
    validate_history();
    if (!is_registered_wallet(vault_address)) {
        throw std::invalid_argument("bridge vault must be an existing encrypted Ledger wallet");
    }
    if (financial_.bridge().vault_address == vault_address) return;
    for (const auto& transaction : ledger_.mempool().transactions()) {
        if (transaction.sender == vault_address) {
            throw std::logic_error("resolve all existing outgoing vault mempool transactions before configuring this wallet");
        }
    }
    financial_.configure_bridge_vault(vault_address);
}

void BridgeService::validate_history() const {
    const auto& bridge = financial_.bridge();
    if (bridge.vault_address.empty()) {
        if (!bridge.deposits.empty() || !bridge.withdrawals.empty()) {
            throw std::runtime_error("bridge history exists without a configured vault");
        }
        return;
    }

    for (const auto& deposit : bridge.deposits) {
        const auto confirmed = ledger_.find_confirmed_transaction(deposit.transaction_id);
        if (!confirmed) {
            throw std::runtime_error("bridge deposit history references a transaction absent from the Ledger chain: " +
                                     deposit.transaction_id);
        }
        const auto& transaction = confirmed->transaction;
        if (!transaction.verify_signature() || transaction.asset != kNativeAsset ||
            transaction.sender != deposit.account_id || transaction.recipient != bridge.vault_address ||
            transaction.amount != deposit.amount || confirmed->block_height != deposit.block_height) {
            throw std::runtime_error("bridge deposit history does not match its confirmed Ledger transaction: " +
                                     deposit.transaction_id);
        }
    }

    for (const auto& withdrawal : bridge.withdrawals) {
        const auto confirmed = ledger_.find_confirmed_transaction(withdrawal.transaction_id);
        if (withdrawal.status == platform::BridgeWithdrawalStatus::Confirmed && !confirmed) {
            throw std::runtime_error("confirmed bridge withdrawal is absent from the Ledger chain: " +
                                     withdrawal.transaction_id);
        }
        if (!confirmed) continue; // A genuinely pending signed request is recoverable.
        verify_confirmed_withdrawal(withdrawal, *confirmed);
        if (withdrawal.status == platform::BridgeWithdrawalStatus::Confirmed &&
            withdrawal.block_height != confirmed->block_height) {
            throw std::runtime_error("bridge withdrawal confirmation height does not match the Ledger chain: " +
                                     withdrawal.transaction_id);
        }
    }
}

Status BridgeService::status() const {
    validate_history();
    Status result;
    const auto& bridge = financial_.bridge();
    if (bridge.vault_address.empty()) return result;

    result.configured = true;
    result.vault_address = bridge.vault_address;
    result.vault_on_chain_balance = ledger_.chain().balance_of(bridge.vault_address);
    result.off_chain_fst_liabilities = financial_.total_fst_liabilities();
    result.deposits = bridge.deposits.size();
    std::set<std::string> bridge_withdrawal_ids;
    for (const auto& withdrawal : bridge.withdrawals) {
        bridge_withdrawal_ids.insert(withdrawal.transaction_id);
        if (withdrawal.status == platform::BridgeWithdrawalStatus::Pending) {
            ++result.pending_withdrawals;
            const auto confirmed = ledger_.find_confirmed_transaction(withdrawal.transaction_id);
            if (confirmed) {
                // The chain append may have committed immediately before a
                // process interruption prevented the bridge snapshot update.
                // Treat the signed payout as spent collateral, and expose the
                // stale metadata separately until reconciliation is persisted.
                verify_confirmed_withdrawal(withdrawal, *confirmed);
                ++result.pending_awaiting_reconciliation;
                continue;
            }
            result.pending_withdrawal_reserve = checked_add(
                result.pending_withdrawal_reserve,
                checked_add(withdrawal.amount, withdrawal.fee,
                            "pending bridge withdrawal reserve overflows"),
                "aggregate pending bridge withdrawal reserve overflows");
        } else {
            ++result.confirmed_withdrawals;
        }
    }

    // The dedicated vault should have no unrelated outgoing mempool activity.
    // Such transactions may predate vault configuration or come from a caller
    // outside the terminal workflow. Count them as encumbered collateral too.
    for (const auto& transaction : ledger_.mempool().transactions()) {
        if (transaction.sender != bridge.vault_address) continue;
        const auto id_hash = transaction.id();
        const std::string id = crypto::hex(id_hash.data(), id_hash.size());
        if (bridge_withdrawal_ids.contains(id)) continue;
        ++result.untracked_vault_mempool_transactions;
        result.untracked_vault_mempool_reserve = checked_add(
            result.untracked_vault_mempool_reserve,
            checked_add(transaction.amount, transaction.fee,
                        "untracked vault mempool reserve overflows"),
            "aggregate untracked vault mempool reserve overflows");
    }

    const Amount all_pending_reserves = checked_add(
        result.pending_withdrawal_reserve, result.untracked_vault_mempool_reserve,
        "aggregate pending bridge reserve overflows");
    result.required_collateral = checked_add(
        result.off_chain_fst_liabilities, all_pending_reserves,
        "effective bridge collateral requirement overflows");
    result.fully_collateralized = result.vault_on_chain_balance >= result.required_collateral;
    if (result.fully_collateralized) {
        result.collateral_surplus = result.vault_on_chain_balance - result.required_collateral;
    } else {
        result.collateral_shortfall = result.required_collateral - result.vault_on_chain_balance;
    }
    return result;
}

DepositClaimResult BridgeService::claim_deposit(const std::string& id) {
    validate_history();
    if (financial_.bridge().vault_address.empty()) throw std::logic_error("configure a bridge vault first");
    if (!valid_transaction_id(id)) throw std::invalid_argument("transaction ID must be 64 lowercase hexadecimal characters");
    const auto confirmed = ledger_.find_confirmed_transaction(id);
    if (!confirmed) throw std::runtime_error("deposit is not confirmed in the local Ledger chain");

    const auto& transaction = confirmed->transaction;
    const auto& vault = financial_.bridge().vault_address;
    if (!transaction.verify_signature() || transaction.asset != kNativeAsset ||
        transaction.recipient != vault || transaction.sender == vault || transaction.amount == 0) {
        throw std::runtime_error("confirmed transaction is not a valid native-FST deposit to this bridge vault");
    }
    if (!is_registered_wallet(transaction.sender)) {
        throw std::runtime_error("deposit sender is not a registered Ledger wallet; refusing to create an unowned settlement account");
    }

    const bool credited = financial_.apply_confirmed_bridge_deposit(
        id, transaction.sender, transaction.amount, confirmed->block_height);
    return {id, transaction.sender, transaction.amount, confirmed->block_height, credited};
}

void BridgeService::verify_confirmed_withdrawal(
    const platform::BridgeWithdrawalRecord& record,
    const ledger::ConfirmedTransaction& confirmed) const {
    const auto& transaction = confirmed.transaction;
    if (!transaction.verify_signature() || transaction_id(transaction) != record.transaction_id ||
        transaction.sender != financial_.bridge().vault_address ||
        transaction.recipient != record.account_id || transaction.asset != kNativeAsset ||
        transaction.amount != record.amount || transaction.fee != record.fee) {
        throw std::runtime_error("confirmed Ledger transaction does not match the persisted bridge withdrawal request");
    }
}

WithdrawalRequestResult BridgeService::request_withdrawal(
    const std::string& account_id, Amount amount, Amount fee,
    const ledger::Wallet& vault_wallet) {
    validate_history();
    const auto& bridge = financial_.bridge();
    if (bridge.vault_address.empty()) throw std::logic_error("configure a bridge vault first");
    if (amount == 0) throw std::invalid_argument("withdrawal amount must be positive");
    if (amount > UINT64_MAX - fee) throw std::overflow_error("withdrawal amount plus fee overflows");
    if (vault_wallet.address() != bridge.vault_address || !ledger_.owns_wallet(vault_wallet)) {
        throw std::invalid_argument("unlocked wallet does not match the configured bridge vault");
    }
    if (!is_registered_wallet(account_id) || account_id == bridge.vault_address) {
        throw std::invalid_argument("withdrawal recipient must be a registered non-vault Ledger wallet");
    }
    // A pending signed withdrawal owns the next vault nonce. Resolve it first
    // so a retry after restart cannot collide with a later withdrawal nonce.
    for (const auto& existing : bridge.withdrawals) {
        if (existing.status == platform::BridgeWithdrawalStatus::Pending) {
            throw std::logic_error("a bridge withdrawal is already pending; retry it and mine it before creating another");
        }
    }
    for (const auto& pending : ledger_.mempool().transactions()) {
        if (pending.sender == bridge.vault_address) {
            throw std::logic_error("vault has an unrelated outgoing mempool transaction; mine or resolve it before withdrawing");
        }
    }

    const auto account = financial_.balance(account_id, platform::FinancialPlatform::kBaseAsset);
    if (account.available() < amount) throw std::runtime_error("insufficient available off-chain FST balance");

    const Amount liabilities = financial_.total_fst_liabilities();
    const Amount required_vault_balance = checked_add(liabilities, fee,
        "required bridge collateral plus fee overflows");
    const Amount vault_balance = ledger_.chain().balance_of(bridge.vault_address);
    if (vault_balance < required_vault_balance) {
        throw std::runtime_error("bridge vault is undercollateralized: on-chain vault balance must cover all off-chain FST account balances, AMM reserves, and the withdrawal fee");
    }

    ledger::Transaction transaction = ledger_.create_transfer(vault_wallet, account_id, amount, fee);
    std::string validation_error;
    if (!ledger_.can_submit(transaction, &validation_error)) {
        throw std::runtime_error("withdrawal transaction preflight failed: " + validation_error);
    }

    // Persist the debit and exact signed transaction before broadcasting. If
    // the process stops here, retry_pending_withdrawals can safely rebroadcast
    // the same transaction; it cannot create a second withdrawal.
    financial_.prepare_bridge_withdrawal(transaction);
    const std::string id = transaction_id(transaction);
    std::string submission_error;
    const bool submitted = ledger_.submit(transaction, &submission_error);
    return {id, amount, fee, submitted, submitted ? std::string{} : submission_error};
}

RetryResult BridgeService::retry_pending_withdrawals() {
    validate_history();
    RetryResult result;
    const auto records = financial_.bridge().withdrawals;
    for (const auto& record : records) {
        if (record.status != platform::BridgeWithdrawalStatus::Pending) continue;
        const auto confirmed = ledger_.find_confirmed_transaction(record.transaction_id);
        if (confirmed) {
            verify_confirmed_withdrawal(record, *confirmed);
            if (financial_.confirm_bridge_withdrawal(record.transaction_id, confirmed->block_height)) {
                ++result.already_confirmed;
            }
            continue;
        }
        // The Ledger mempool is durable. After restart, this is normally
        // already present and must not be submitted a second time. If an
        // earlier interrupted attempt committed the bridge debit but not the
        // Ledger mempool row, retry restores the exact persisted signed bytes.
        if (ledger_.has_pending_transaction(record.transaction_id)) {
            ++result.already_pending;
            continue;
        }
        try {
            const auto transaction = ledger::Transaction::deserialize(record.signed_transaction);
            std::string error;
            if (!ledger_.submit(transaction, &error)) {
                result.errors.push_back(record.transaction_id + ": " + error);
                continue;
            }
            ++result.submitted;
        } catch (const std::exception& exception) {
            result.errors.push_back(record.transaction_id + ": " + exception.what());
        }
    }
    return result;
}

std::size_t BridgeService::reconcile_confirmed_withdrawals() {
    validate_history();
    std::size_t confirmed_count = 0;
    // Snapshot the records first because confirmation writes a new snapshot.
    const auto records = financial_.bridge().withdrawals;
    for (const auto& record : records) {
        if (record.status != platform::BridgeWithdrawalStatus::Pending) continue;
        const auto confirmed = ledger_.find_confirmed_transaction(record.transaction_id);
        if (!confirmed) continue;
        verify_confirmed_withdrawal(record, *confirmed);
        if (financial_.confirm_bridge_withdrawal(record.transaction_id, confirmed->block_height)) {
            ++confirmed_count;
        }
    }
    return confirmed_count;
}

} // namespace feinstat::bridge
