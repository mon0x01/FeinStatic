#include "test.hpp"

#include "feinstat/bridge/bridge_service.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/application/verification.hpp"
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace feinstat;

namespace {
std::string bridge_test_db() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("feinstatic-bridge-" + std::to_string(stamp) + ".db")).string();
}

void remove_bridge_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

std::string txid(const ledger::Transaction& transaction) {
    const auto hash = transaction.id();
    return crypto::hex(hash.data(), hash.size());
}
}

FS_TEST(bridge_confirmed_deposits_are_idempotent_and_withdrawals_recover_after_restart) {
    constexpr Amount coin = kAtomsPerCoin;
    const std::string path = bridge_test_db();
    remove_bridge_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() { remove_bridge_db(path); }
    } cleanup{path};

    const auto customer = ledger::Wallet::generate();
    const auto vault = ledger::Wallet::generate();
    const auto miner = ledger::Wallet::generate();
    const auto stranger = ledger::Wallet::generate();
    {
        auto chain = ledger::Ledger::initialize(path, {
            {customer.address(), 1'000 * coin},
            {vault.address(), 1'000 * coin},
            {miner.address(), 10 * coin},
            {stranger.address(), 100 * coin}
        });
        chain.register_wallet(customer, "bridge-customer-test-passphrase");
        chain.register_wallet(vault, "bridge-vault-test-passphrase");
        chain.register_wallet(miner, "bridge-miner-test-passphrase");
        // Deliberately do not store stranger's wallet record. Chain signatures
        // are valid, but an unowned off-chain account must not be created.
    }

    std::string deposit_id;
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        service.configure_vault(vault.address());

        const auto deposit = chain.create_transfer(customer, vault.address(), 100 * coin, coin / 1000);
        deposit_id = txid(deposit);
        std::string error;
        FS_REQUIRE(chain.submit(deposit, &error));
        FS_REQUIRE_THROWS(service.claim_deposit(deposit_id)); // unconfirmed is not creditable
        FS_REQUIRE(financial.bridge().deposits.empty());

        (void)chain.mine(miner.address());
        const auto claim = service.claim_deposit(deposit_id);
        FS_REQUIRE(claim.newly_credited);
        FS_REQUIRE(claim.amount == 100 * coin);
        FS_REQUIRE(claim.block_height == 1);
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 100 * coin);
        FS_REQUIRE(financial.total_fst_liabilities() == 100 * coin);
        const auto protected_references = financial.bridge_transaction_references();
        FS_REQUIRE(protected_references.size() == 1);
        FS_REQUIRE(protected_references.front().first == deposit_id);
        FS_REQUIRE(protected_references.front().second == claim.block_height);

        // Pool reserves are liabilities too. Moving customer FST into an AMM
        // reserve must not make the aggregate bridge liability disappear.
        financial.fund_test_asset(customer.address(), "USD", 100 * coin,
                                  "bridge test AMM quote-side funding");
        const auto pool_id = financial.create_pool("FST", "USD", 30);
        (void)financial.add_liquidity(customer.address(), pool_id, 20 * coin, 20 * coin);
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 80 * coin);
        FS_REQUIRE(financial.total_fst_liabilities() == 100 * coin);

        const auto revision_after_claim = financial.revision();
        const auto duplicate = service.claim_deposit(deposit_id);
        FS_REQUIRE(!duplicate.newly_credited);
        FS_REQUIRE(financial.revision() == revision_after_claim);
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 80 * coin);
        FS_REQUIRE(service.status().fully_collateralized);

        // A valid on-chain payment to another address is not a bridge deposit.
        const auto wrong_recipient = chain.create_transfer(customer, miner.address(), coin, 0);
        const auto wrong_id = txid(wrong_recipient);
        FS_REQUIRE(chain.submit(wrong_recipient));
        (void)chain.mine(miner.address());
        FS_REQUIRE_THROWS(service.claim_deposit(wrong_id));
        FS_REQUIRE(financial.bridge().deposits.size() == 1);

        // A validly signed transaction from a genesis address without a stored
        // wallet record must not auto-create a platform account on claim.
        const auto unowned = ledger::Transaction::create_signed(
            stranger, vault.address(), coin, 0, chain.chain().nonce_of(stranger.address()) + 1);
        const auto unowned_id = txid(unowned);
        FS_REQUIRE(chain.submit(unowned));
        (void)chain.mine(miner.address());
        FS_REQUIRE_THROWS(service.claim_deposit(unowned_id));
        FS_REQUIRE(!financial.settlement_book().has_account(stranger.address()));

        // The withdrawal is durably recorded and debited before it is placed
        // in the volatile mempool. The exact signed bytes allow retry after a
        // process restart without exposing/re-requesting the vault secret.
        const auto withdrawal = service.request_withdrawal(customer.address(), 25 * coin,
                                                             coin / 1000, vault);
        FS_REQUIRE(withdrawal.submitted_to_mempool);
        FS_REQUIRE(chain.has_pending_transaction(withdrawal.transaction_id));
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 55 * coin);
        FS_REQUIRE(financial.bridge().withdrawals.size() == 1);
        FS_REQUIRE(financial.bridge().withdrawals.front().status == platform::BridgeWithdrawalStatus::Pending);
        const auto pending_references = financial.bridge_transaction_references();
        FS_REQUIRE(pending_references.size() == 2);
        FS_REQUIRE(pending_references[0] == std::make_pair(deposit_id, std::uint64_t{1}));
        FS_REQUIRE(pending_references[1].first == withdrawal.transaction_id);
        FS_REQUIRE(pending_references[1].second == 0);
        const auto pending_status = service.status();
        FS_REQUIRE(pending_status.off_chain_fst_liabilities == 75 * coin);
        FS_REQUIRE(pending_status.pending_withdrawal_reserve == 25 * coin + coin / 1000);
        FS_REQUIRE(pending_status.required_collateral == 100 * coin + coin / 1000);
        FS_REQUIRE(pending_status.fully_collateralized);
        FS_REQUIRE(pending_status.collateral_shortfall == 0);
        const auto pending_report = application::verify_database(path);
        FS_REQUIRE(pending_report.success());
        FS_REQUIRE(pending_report.bridge_pending_withdrawal_count == 1);
        FS_REQUIRE(pending_report.bridge_pending_withdrawal_reserve == 25 * coin + coin / 1000);
        FS_REQUIRE(pending_report.bridge_required_collateral == 100 * coin + coin / 1000);
        FS_REQUIRE(pending_report.bridge_fully_collateralized);
        FS_REQUIRE_THROWS(service.request_withdrawal(customer.address(), coin, 0, vault));
    }

    // A process restart clears the in-memory mempool. The bridge journal still
    // contains the signed pending transaction, which can be safely retried.
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 55 * coin);
        FS_REQUIRE(financial.bridge().withdrawals.size() == 1);
        FS_REQUIRE(financial.bridge().withdrawals.front().status == platform::BridgeWithdrawalStatus::Pending);

        const auto retry = service.retry_pending_withdrawals();
        FS_REQUIRE(retry.submitted == 0);
        FS_REQUIRE(retry.already_pending == 1);
        FS_REQUIRE(retry.errors.empty());
        const auto duplicate_retry = service.retry_pending_withdrawals();
        FS_REQUIRE(duplicate_retry.already_pending == 1);
        FS_REQUIRE(duplicate_retry.submitted == 0);

        // Mine the signed transaction but deliberately do not reconcile the
        // bridge snapshot. The next process must discover and confirm it.
        (void)chain.mine(miner.address());
        FS_REQUIRE(chain.chain().find_transaction(financial.bridge().withdrawals.front().transaction_id).has_value());
        const auto mined_unreconciled_status = service.status();
        FS_REQUIRE(mined_unreconciled_status.pending_withdrawals == 1);
        FS_REQUIRE(mined_unreconciled_status.pending_awaiting_reconciliation == 1);
        FS_REQUIRE(mined_unreconciled_status.pending_withdrawal_reserve == 0);
        FS_REQUIRE(mined_unreconciled_status.required_collateral == 75 * coin);
        FS_REQUIRE(mined_unreconciled_status.fully_collateralized);
        const auto unreconciled_report = application::verify_database(path);
        FS_REQUIRE(unreconciled_report.success());
        FS_REQUIRE(unreconciled_report.bridge_pending_withdrawal_count == 1);
        FS_REQUIRE(unreconciled_report.bridge_pending_awaiting_reconciliation_count == 1);
        FS_REQUIRE(unreconciled_report.bridge_pending_withdrawal_reserve == 0);
        FS_REQUIRE(unreconciled_report.bridge_required_collateral == 75 * coin);
        FS_REQUIRE(unreconciled_report.bridge_fully_collateralized);
    }
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        FS_REQUIRE(service.reconcile_confirmed_withdrawals() == 1);
        FS_REQUIRE(service.reconcile_confirmed_withdrawals() == 0);
        FS_REQUIRE(financial.bridge().withdrawals.front().status == platform::BridgeWithdrawalStatus::Confirmed);
        FS_REQUIRE(financial.bridge().withdrawals.front().block_height == chain.chain().height());
        const auto confirmed_references = financial.bridge_transaction_references();
        FS_REQUIRE(confirmed_references.size() == 2);
        FS_REQUIRE(confirmed_references[1].first == financial.bridge().withdrawals.front().transaction_id);
        FS_REQUIRE(confirmed_references[1].second == chain.chain().height());
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == 55 * coin);
        FS_REQUIRE(financial.total_fst_liabilities() == 75 * coin);
        FS_REQUIRE(service.status().pending_withdrawal_reserve == 0);
        FS_REQUIRE(service.status().fully_collateralized);
        FS_REQUIRE(financial.validate());
        FS_REQUIRE(chain.chain().validate());
    }

    // Aggregate collateral protection includes all off-chain FST liabilities.
    // Synthetic credits are not deposits; if they create liabilities beyond
    // the vault's reserves, the bridge must reject an otherwise valid request.
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        const auto before_revision = financial.revision();
        financial.fund_test_asset(customer.address(), "FST", 10'000 * coin,
                                  "verify bridge aggregate collateral rejection");
        const auto after_credit_revision = financial.revision();
        FS_REQUIRE(after_credit_revision == before_revision + 1);
        const auto before_balance = financial.balance(customer.address(), "FST").total;
        FS_REQUIRE_THROWS(service.request_withdrawal(customer.address(), coin, 0, vault));
        FS_REQUIRE(financial.balance(customer.address(), "FST").total == before_balance);
        FS_REQUIRE(financial.revision() == after_credit_revision);
        FS_REQUIRE(financial.bridge().withdrawals.size() == 1);
        FS_REQUIRE(!service.status().fully_collateralized);
        const auto report = application::verify_database(path);
        FS_REQUIRE(report.success());
        FS_REQUIRE(report.bridge_configured);
        FS_REQUIRE(!report.bridge_fully_collateralized);
        FS_REQUIRE(report.bridge_deposit_count == 1);
        FS_REQUIRE(report.bridge_confirmed_withdrawal_count == 1);
        FS_REQUIRE(report.bridge_pending_withdrawal_count == 0);
        FS_REQUIRE(report.bridge_pending_awaiting_reconciliation_count == 0);
        FS_REQUIRE(report.bridge_pending_withdrawal_reserve == 0);
        FS_REQUIRE(report.bridge_required_collateral == report.bridge_fst_liabilities);
        FS_REQUIRE(report.bridge_collateral_shortfall > 0);
    }
}

FS_TEST(bridge_vault_configuration_is_immutable_and_cannot_be_a_customer_account) {
    const std::string path = bridge_test_db();
    remove_bridge_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() { remove_bridge_db(path); }
    } cleanup{path};

    const auto customer = ledger::Wallet::generate();
    const auto vault = ledger::Wallet::generate();
    {
        auto chain = ledger::Ledger::initialize(path, {
            {customer.address(), 10 * kAtomsPerCoin},
            {vault.address(), 10 * kAtomsPerCoin}
        });
        chain.register_wallet(customer, "bridge-customer-test-passphrase");
        chain.register_wallet(vault, "bridge-vault-test-passphrase");
    }
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        FS_REQUIRE_THROWS(service.configure_vault("not-a-wallet"));
        financial.create_account(customer.address());
        service.configure_vault(vault.address());
        service.configure_vault(vault.address()); // idempotent same-address configuration
        FS_REQUIRE_THROWS(service.configure_vault(customer.address()));
        FS_REQUIRE(financial.bridge().vault_address == vault.address());
        FS_REQUIRE(financial.validate());
    }
    {
        auto recovered = platform::FinancialPlatform::open(path);
        FS_REQUIRE(recovered.bridge().vault_address == vault.address());
        FS_REQUIRE(recovered.validate());
    }
}



FS_TEST(bridge_accounts_for_untracked_vault_mempool_transactions) {
    constexpr Amount coin = kAtomsPerCoin;
    const std::string path = bridge_test_db();
    remove_bridge_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() { remove_bridge_db(path); }
    } cleanup{path};

    const auto customer = ledger::Wallet::generate();
    const auto vault = ledger::Wallet::generate();
    const auto miner = ledger::Wallet::generate();
    {
        auto chain = ledger::Ledger::initialize(path, {
            {customer.address(), 10 * coin},
            {vault.address(), 100 * coin},
            {miner.address(), 10 * coin}
        });
        chain.register_wallet(customer, "bridge-mempool-customer-passphrase");
        chain.register_wallet(vault, "bridge-mempool-vault-passphrase");
        chain.register_wallet(miner, "bridge-mempool-miner-passphrase");
    }
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);

        // Do not silently adopt a vault that already has an outgoing transfer
        // waiting in the volatile mempool before Bridge takes control of it.
        const auto before_configuration = chain.create_transfer(vault, customer.address(), coin, 0);
        FS_REQUIRE(chain.submit(before_configuration));
        FS_REQUIRE_THROWS(service.configure_vault(vault.address()));
        (void)chain.mine(miner.address());

        service.configure_vault(vault.address());
        financial.create_account(customer.address());
        financial.fund_test_asset(customer.address(), "FST", coin,
                                  "untracked vault mempool collateral test");

        // A second unrelated outgoing transfer must be visible as encumbered
        // collateral even when it was submitted outside BridgeService.
        const auto outside_bridge = chain.create_transfer(vault, customer.address(), 99 * coin, 0);
        FS_REQUIRE(chain.submit(outside_bridge));
        const auto status = service.status();
        FS_REQUIRE(status.untracked_vault_mempool_transactions == 1);
        FS_REQUIRE(status.untracked_vault_mempool_reserve == 99 * coin);
        FS_REQUIRE(status.required_collateral == 100 * coin);
        FS_REQUIRE(status.collateral_shortfall == 1 * coin);
        FS_REQUIRE(!status.fully_collateralized);
        FS_REQUIRE_THROWS(service.request_withdrawal(customer.address(), coin, 0, vault));

        const auto report = application::verify_database(path);
        FS_REQUIRE(report.success());
        // The non-interactive verifier recovers the durable mempool too, so
        // it must report the unrelated vault transfer as encumbered collateral.
        FS_REQUIRE(report.bridge_pending_withdrawal_count == 0);
        FS_REQUIRE(report.bridge_required_collateral == 100 * coin);
        FS_REQUIRE(report.bridge_collateral_shortfall == coin);
        FS_REQUIRE(!report.bridge_fully_collateralized);
    }
}


FS_TEST(bridge_database_verifier_cross_checks_persisted_history_against_chain) {
    constexpr Amount coin = kAtomsPerCoin;
    const std::string path = bridge_test_db();
    remove_bridge_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() { remove_bridge_db(path); }
    } cleanup{path};

    const auto customer = ledger::Wallet::generate();
    const auto vault = ledger::Wallet::generate();
    const auto miner = ledger::Wallet::generate();
    std::string deposit_id;
    {
        auto chain = ledger::Ledger::initialize(path, {
            {customer.address(), 20 * coin},
            {vault.address(), 5 * coin},
            {miner.address(), 5 * coin}
        });
        chain.register_wallet(customer, "bridge-history-customer-passphrase");
        chain.register_wallet(vault, "bridge-history-vault-passphrase");
        chain.register_wallet(miner, "bridge-history-miner-passphrase");
    }
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        service.configure_vault(vault.address());
        const auto deposit = chain.create_transfer(customer, vault.address(), coin, 0);
        deposit_id = txid(deposit);
        FS_REQUIRE(chain.submit(deposit));
        (void)chain.mine(miner.address());
        FS_REQUIRE(service.claim_deposit(deposit_id).newly_credited);
        service.validate_history();
    }

    // Alter a valid-looking transaction ID in the checksummed platform
    // payload, then recompute the checksum. Snapshot validation alone cannot
    // establish chain provenance; the non-interactive verifier must cross-check
    // the recorded transaction against the recovered Ledger blocks.
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* query = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "SELECT payload FROM financial_state WHERE singleton=1;",
                                  -1, &query, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(query) == SQLITE_ROW);
    const auto* raw = static_cast<const Byte*>(sqlite3_column_blob(query, 0));
    const int raw_size = sqlite3_column_bytes(query, 0);
    FS_REQUIRE(raw != nullptr && raw_size > 0);
    Bytes payload(raw, raw + raw_size);
    sqlite3_finalize(query);
    const auto found = std::find_end(payload.begin(), payload.end(), deposit_id.begin(), deposit_id.end());
    FS_REQUIRE(found != payload.end());
    const auto offset = static_cast<std::size_t>(std::distance(payload.begin(), found));
    payload[offset] = payload[offset] == static_cast<Byte>('0') ? static_cast<Byte>('1') : static_cast<Byte>('0');
    const Hash checksum = crypto::sha256(payload);
    sqlite3_stmt* update = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db,
        "UPDATE financial_state SET payload=?1, checksum=?2 WHERE singleton=1;",
        -1, &update, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_bind_blob(update, 1, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    FS_REQUIRE(sqlite3_bind_blob(update, 2, checksum.data(), static_cast<int>(checksum.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(update) == SQLITE_DONE);
    sqlite3_finalize(update);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);

    const auto report = application::verify_database(path);
    FS_REQUIRE(report.ledger_opened);
    FS_REQUIRE(report.ledger_valid);
    FS_REQUIRE(report.platform_opened);
    FS_REQUIRE(!report.platform_valid);
    FS_REQUIRE(!report.success());
    FS_REQUIRE(report.platform_error.find("absent from the Ledger chain") != std::string::npos);

    // The live service must enforce the same provenance check before exposing
    // status or performing another bridge mutation, not only in the offline verifier.
    {
        auto chain = ledger::Ledger::open(path);
        auto financial = platform::FinancialPlatform::open(path);
        bridge::BridgeService service(chain, financial);
        FS_REQUIRE_THROWS(service.status());
        FS_REQUIRE_THROWS(service.claim_deposit(deposit_id));
    }
}

FS_TEST(v0_3_platform_snapshot_opens_and_upgrades_without_bridge_state_loss) {
    constexpr Amount coin = kAtomsPerCoin;
    const std::string path = bridge_test_db();
    remove_bridge_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() { remove_bridge_db(path); }
    } cleanup{path};

    const auto customer = ledger::Wallet::generate();
    {
        auto chain = ledger::Ledger::initialize(path, {{customer.address(), 100 * coin}});
        chain.register_wallet(customer, "legacy-snapshot-test-passphrase");
    }
    {
        auto financial = platform::FinancialPlatform::open(path);
        financial.create_account(customer.address());
        financial.fund_test_asset(customer.address(), "USD", 7 * coin, "legacy v0.3 snapshot fixture");
    }

    // The v0.3 FSPF v1 payload is byte-for-byte the v0.4 v2 payload prefix;
    // only the v2 bridge extension is appended. Convert the no-bridge fixture
    // to its genuine legacy representation and recompute the SHA-256 checksum.
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* query = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "SELECT payload FROM financial_state WHERE singleton=1;",
                                  -1, &query, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(query) == SQLITE_ROW);
    const auto* raw = static_cast<const Byte*>(sqlite3_column_blob(query, 0));
    const int raw_size = sqlite3_column_bytes(query, 0);
    FS_REQUIRE(raw != nullptr && raw_size > 16);
    Bytes payload(raw, raw + raw_size);
    sqlite3_finalize(query);
    FS_REQUIRE(payload[0] == 'F' && payload[1] == 'S' && payload[2] == 'P' && payload[3] == 'F');
    FS_REQUIRE(payload[4] == 2 && payload[5] == 0 && payload[6] == 0 && payload[7] == 0);
    // Empty bridge extension: four-byte empty vault string plus two zero counts.
    FS_REQUIRE(payload.size() >= 12);
    payload.resize(payload.size() - 12);
    payload[4] = 1;
    const Hash checksum = crypto::sha256(payload);
    sqlite3_stmt* update = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db,
        "UPDATE financial_state SET payload=?1, checksum=?2 WHERE singleton=1;",
        -1, &update, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_bind_blob(update, 1, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    FS_REQUIRE(sqlite3_bind_blob(update, 2, checksum.data(), static_cast<int>(checksum.size()), SQLITE_TRANSIENT) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(update) == SQLITE_DONE);
    sqlite3_finalize(update);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);

    {
        auto recovered = platform::FinancialPlatform::open(path);
        FS_REQUIRE(recovered.accounts().size() == 1);
        FS_REQUIRE(recovered.balance(customer.address(), "USD").total == 7 * coin);
        FS_REQUIRE(recovered.bridge().vault_address.empty());
        FS_REQUIRE(recovered.validate());
        // First mutation writes the new v2 snapshot, preserving legacy state.
        recovered.fund_test_asset(customer.address(), "FST", coin,
                                  "v0.4 snapshot upgrade regression");
    }
    {
        auto recovered = platform::FinancialPlatform::open(path);
        FS_REQUIRE(recovered.balance(customer.address(), "USD").total == 7 * coin);
        FS_REQUIRE(recovered.balance(customer.address(), "FST").total == coin);
        FS_REQUIRE(recovered.bridge().vault_address.empty());
        FS_REQUIRE(recovered.validate());
    }
}
