#include "test.hpp"

#include "feinstat/platform/financial_platform.hpp"
#include "feinstat/application/verification.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sqlite3.h>
#include <filesystem>
#include <string>

using namespace feinstat;
using namespace feinstat::platform;

namespace {
std::string platform_test_db() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("feinstatic-platform-" + std::to_string(stamp) + ".db")).string();
}
void remove_platform_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}
}

FS_TEST(platform_persists_shared_settlement_match_and_flow_across_restart) {
    const std::string path = platform_test_db();
    remove_platform_db(path);
    { storage::SqliteStore bootstrap(path); bootstrap.initialize_schema(); }
    std::string pool_id;
    exchange::OrderId resting_bid = 0;
    const Amount coin = kAtomsPerCoin;

    {
        auto platform = FinancialPlatform::open(path);
        platform.create_account("seller");
        platform.create_account("buyer");
        platform.create_account("liquidity-provider");

        // Synthetic platform credits are intentionally distinct from Ledger
        // genesis issuance or an on-chain deposit.
        platform.fund_test_asset("seller", "FST", 5 * coin, "platform persistence fixture");
        platform.fund_test_asset("buyer", "USD", 20 * coin, "platform persistence fixture");
        platform.fund_test_asset("liquidity-provider", "FST", 1'000 * coin, "platform persistence fixture");
        platform.fund_test_asset("liquidity-provider", "USD", 1'000 * coin, "platform persistence fixture");

        pool_id = platform.create_pool("FST", "USD", flow::kDefaultFeeBps);
        const auto liquidity = platform.add_liquidity("liquidity-provider", pool_id,
                                                       1'000 * coin, 1'000 * coin);
        FS_REQUIRE(liquidity.shares == 1'000 * coin);

        // Match quantity is whole FST; price is USD atoms per whole FST.
        (void)platform.submit_limit("seller", exchange::Side::Sell, 10 * coin, 1);
        const auto buyer_order = platform.submit_limit("buyer", exchange::Side::Buy, 10 * coin, 1);
        FS_REQUIRE(platform.match().order(buyer_order)->status == exchange::OrderStatus::Filled);
        FS_REQUIRE(platform.match().trades().size() == 1);
        FS_REQUIRE(platform.balance("seller", "FST").total == 4 * coin);
        FS_REQUIRE(platform.balance("seller", "USD").total == 10 * coin);

        // Leave a resting Match bid reserving 8 USD. Flow must see only the
        // remaining 2 USD as available from the same canonical settlement book.
        resting_bid = platform.submit_limit("seller", exchange::Side::Buy, 8 * coin, 1);
        FS_REQUIRE(platform.balance("seller", "USD").reserved == 8 * coin);
        FS_REQUIRE(platform.balance("seller", "USD").available() == 2 * coin);
        const auto failed_revision = platform.revision();
        FS_REQUIRE_THROWS(platform.swap("seller", pool_id, "USD", 3 * coin, 1));
        FS_REQUIRE(platform.revision() == failed_revision);
        FS_REQUIRE(platform.balance("seller", "USD").available() == 2 * coin);

        const auto quote = platform.quote(pool_id, "USD", coin);
        const auto swap = platform.swap("seller", pool_id, "USD", coin, quote.amount_out);
        FS_REQUIRE(swap.amount_out == quote.amount_out);
        FS_REQUIRE(platform.balance("seller", "USD").total == 9 * coin);
        FS_REQUIRE(platform.balance("seller", "USD").reserved == 8 * coin);
        FS_REQUIRE(platform.balance("seller", "FST").total > 4 * coin);
        FS_REQUIRE(platform.match().order(resting_bid)->status == exchange::OrderStatus::Open);
        FS_REQUIRE(platform.validate());
    }

    {
        auto recovered = FinancialPlatform::open(path);
        FS_REQUIRE(recovered.revision() > 0);
        FS_REQUIRE(recovered.accounts().size() == 3);
        FS_REQUIRE(recovered.match().trades().size() == 1);
        FS_REQUIRE(recovered.match().order(resting_bid)->status == exchange::OrderStatus::Open);
        FS_REQUIRE(recovered.match().best_bid() == 8 * coin);
        FS_REQUIRE(recovered.balance("seller", "USD").total == 9 * coin);
        FS_REQUIRE(recovered.balance("seller", "USD").reserved == 8 * coin);
        FS_REQUIRE(recovered.balance("seller", "USD").available() == coin);
        FS_REQUIRE(recovered.balance("seller", "FST").total > 4 * coin);
        FS_REQUIRE(recovered.flow().lp_shares_of("liquidity-provider", pool_id) == 1'000 * coin);
        const auto pool = recovered.flow().pool(pool_id).value();
        FS_REQUIRE(pool.reserve_x < 1'000 * coin);
        FS_REQUIRE(pool.reserve_y > 1'000 * coin);
        FS_REQUIRE(recovered.flow().swaps().size() == 1);
        FS_REQUIRE(recovered.settlement_book().journal().size() > 0);
        FS_REQUIRE(recovered.validate());

        // A reserved Match bid cannot be bypassed through the generic transfer
        // interface. Cancelling it releases funds, after which a transfer can
        // use the same canonical settlement balance.
        const auto before_rejected_transfer = recovered.revision();
        FS_REQUIRE_THROWS(recovered.transfer_asset("seller", "buyer", "USD", 2 * coin));
        FS_REQUIRE(recovered.revision() == before_rejected_transfer);
        FS_REQUIRE(recovered.cancel_order("seller", resting_bid));
        FS_REQUIRE(recovered.balance("seller", "USD").reserved == 0);
        recovered.transfer_asset("seller", "buyer", "USD", coin);
        FS_REQUIRE(recovered.balance("seller", "USD").total == 8 * coin);
        FS_REQUIRE(recovered.balance("buyer", "USD").total == 11 * coin);
        FS_REQUIRE(recovered.validate());

        // LP removal is persisted through the same platform snapshot too.
        const auto removed = recovered.remove_liquidity("liquidity-provider", pool_id, 100 * coin);
        FS_REQUIRE(removed.shares == 100 * coin);
        FS_REQUIRE(recovered.flow().lp_shares_of("liquidity-provider", pool_id) == 900 * coin);
        FS_REQUIRE(recovered.validate());
    }
    {
        auto recovered_again = FinancialPlatform::open(path);
        FS_REQUIRE(recovered_again.balance("seller", "USD").total == 8 * coin);
        FS_REQUIRE(recovered_again.balance("buyer", "USD").total == 11 * coin);
        FS_REQUIRE(recovered_again.balance("seller", "USD").reserved == 0);
        FS_REQUIRE(recovered_again.match().open_orders("seller").empty());
        FS_REQUIRE(recovered_again.flow().lp_shares_of("liquidity-provider", pool_id) == 900 * coin);
        FS_REQUIRE(recovered_again.flow().swaps().size() == 1);
        FS_REQUIRE(recovered_again.validate());
    }
    remove_platform_db(path);
}

FS_TEST(platform_uses_optimistic_revision_to_reject_stale_writer) {
    const std::string path = platform_test_db();
    remove_platform_db(path);
    { storage::SqliteStore bootstrap(path); bootstrap.initialize_schema(); }
    {
        auto first = FinancialPlatform::open(path);
        auto stale = FinancialPlatform::open(path);
        first.create_account("account-a");
        FS_REQUIRE(first.revision() == 1);
        FS_REQUIRE_THROWS(stale.create_account("account-b"));
        FS_REQUIRE(stale.revision() == 0);
    }
    {
        auto recovered = FinancialPlatform::open(path);
        FS_REQUIRE(recovered.accounts().size() == 1);
        FS_REQUIRE(recovered.accounts().front() == "account-a");
        FS_REQUIRE(recovered.validate());
    }
    remove_platform_db(path);
}

FS_TEST(platform_schema_v1_migration_is_additive_and_preserves_ledger_rows) {
    const std::string path = platform_test_db();
    remove_platform_db(path);
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const char* legacy_schema =
        "CREATE TABLE blocks (height INTEGER PRIMARY KEY CHECK(height >= 0),"
        "hash BLOB NOT NULL UNIQUE CHECK(length(hash) = 32),payload BLOB NOT NULL);"
        "CREATE TABLE wallets (address TEXT PRIMARY KEY,public_key BLOB NOT NULL CHECK(length(public_key)=32),"
        "salt BLOB NOT NULL CHECK(length(salt)=16),nonce BLOB NOT NULL CHECK(length(nonce)=12),"
        "ciphertext BLOB NOT NULL CHECK(length(ciphertext)=32),tag BLOB NOT NULL CHECK(length(tag)=16),"
        "iterations INTEGER NOT NULL CHECK(iterations>=100000));"
        "INSERT INTO blocks(height,hash,payload) VALUES(0,zeroblob(32),X'01');"
        "PRAGMA user_version=1;";
    char* error_message = nullptr;
    const int schema_result = sqlite3_exec(db, legacy_schema, nullptr, nullptr, &error_message);
    const std::string schema_error = error_message == nullptr ? std::string{} : error_message;
    sqlite3_free(error_message);
    (void)sqlite3_close(db);
    FS_REQUIRE(schema_result == SQLITE_OK);
    if (schema_result != SQLITE_OK) throw std::runtime_error("unable to create schema-v1 fixture: " + schema_error);

    {
        auto platform = FinancialPlatform::open(path);
        FS_REQUIRE(platform.revision() == 0);
        platform.create_account("migration-account");
        FS_REQUIRE(platform.revision() == 1);
    }
    db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int(statement, 0) == 5);
    (void)sqlite3_finalize(statement);
    statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM blocks;", -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int64(statement, 0) == 1);
    (void)sqlite3_finalize(statement);
    statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM financial_state;", -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int64(statement, 0) == 1);
    (void)sqlite3_finalize(statement);
    (void)sqlite3_close(db);
    remove_platform_db(path);
}

FS_TEST(platform_rejects_snapshot_payload_when_checksum_is_modified) {
    const std::string path = platform_test_db();
    remove_platform_db(path);
    { storage::SqliteStore bootstrap(path); bootstrap.initialize_schema(); }
    {
        auto platform = FinancialPlatform::open(path);
        platform.create_account("checksum-account");
    }
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error_message = nullptr;
    const int update_result = sqlite3_exec(
        db, "UPDATE financial_state SET payload = payload || X'00' WHERE singleton=1;",
        nullptr, nullptr, &error_message);
    const std::string update_error = error_message == nullptr ? std::string{} : error_message;
    sqlite3_free(error_message);
    (void)sqlite3_close(db);
    FS_REQUIRE(update_result == SQLITE_OK);
    if (update_result != SQLITE_OK) throw std::runtime_error("unable to corrupt snapshot fixture: " + update_error);
    FS_REQUIRE_THROWS(FinancialPlatform::open(path));
    remove_platform_db(path);
}

FS_TEST(settlement_batch_update_rejects_duplicate_keys_without_mutating_state) {
    settlement::SettlementBook book;
    book.create_account("batch-account");
    book.credit("batch-account", "FST", 100, "test-credit", "initial");
    const auto before = book.snapshot();
    const std::vector<settlement::BalanceUpdate> duplicate_updates{
        {"batch-account", "FST", {90, 0}},
        {"batch-account", "FST", {80, 0}}
    };
    FS_REQUIRE_THROWS(book.replace_states(duplicate_updates, "batch-test", "duplicate"));
    FS_REQUIRE(book.total("batch-account", "FST") == 100);
    FS_REQUIRE(book.journal().size() == before.journal.size());
    FS_REQUIRE(book.validate());

    const std::vector<settlement::BalanceUpdate> valid_updates{
        {"batch-account", "FST", {90, 10}},
        {"batch-account", "USD", {50, 0}}
    };
    book.replace_states(valid_updates, "batch-test", "valid-batch");
    FS_REQUIRE(book.total("batch-account", "FST") == 90);
    FS_REQUIRE(book.reserved("batch-account", "FST") == 10);
    FS_REQUIRE(book.available("batch-account", "FST") == 80);
    FS_REQUIRE(book.total("batch-account", "USD") == 50);
    FS_REQUIRE(book.journal().size() == before.journal.size() + 2);
    FS_REQUIRE(book.validate());
}


FS_TEST(platform_full_flow_lifecycle_and_noninteractive_database_verification) {
    const std::string path = platform_test_db();
    remove_platform_db(path);
    struct Cleanup {
        std::string path;
        ~Cleanup() {
            remove_platform_db(path);
            std::error_code ignored;
            std::filesystem::remove(path + ".verify-output", ignored);
        }
    } cleanup{path};

    constexpr Amount coin = kAtomsPerCoin;
    const auto wallet_a = ledger::Wallet::generate();
    const auto wallet_b = ledger::Wallet::generate();
    const std::string account_a = wallet_a.address();
    const std::string account_b = wallet_b.address();

    // Use a real Ledger database and actual stored wallet identities. Platform
    // credits below remain synthetic off-chain balances by design.
    {
        auto ledger_instance = ledger::Ledger::initialize(
            path, {{account_a, 10'000 * coin}, {account_b, 100 * coin}});
        ledger_instance.register_wallet(wallet_a, "test-only-account-a-passphrase");
        ledger_instance.register_wallet(wallet_b, "test-only-account-b-passphrase");
        FS_REQUIRE(ledger_instance.chain().validate());
    }

    std::string pool_id;
    exchange::OrderId resting_buy = 0;
    {
        auto financial = FinancialPlatform::open(path);
        financial.create_account(account_a);
        financial.create_account(account_b);
        financial.fund_test_asset(account_a, "FST", 1'000 * coin, "automated v0.3 lifecycle fixture");
        financial.fund_test_asset(account_b, "USD", 10'000 * coin, "automated v0.3 lifecycle fixture");

        // Reproduce the existing Match scenario: Wallet A sells 10 FST at
        // 10 USD/FST; Wallet B buys 19, partially fills, then cancels 9.
        (void)financial.submit_limit(account_a, exchange::Side::Sell, 10 * coin, 10);
        resting_buy = financial.submit_limit(account_b, exchange::Side::Buy, 10 * coin, 19);
        FS_REQUIRE(financial.match().trades().size() == 1);
        FS_REQUIRE(financial.match().trades().front().quantity == 10);
        FS_REQUIRE(financial.balance(account_b, "USD").reserved == 90 * coin);
        FS_REQUIRE(financial.cancel_order(account_b, resting_buy));
        FS_REQUIRE(financial.balance(account_b, "USD").reserved == 0);

        // Seed the AMM at the handover's intended 10 FST / 100 USD ratio.
        pool_id = financial.create_pool("FST", "USD", 30);
        FS_REQUIRE(pool_id == "POOL-1");
        const auto initial_liquidity = financial.add_liquidity(
            account_a, pool_id, 10 * coin, 100 * coin);
        FS_REQUIRE(initial_liquidity.amount_x_used == 10 * coin);
        FS_REQUIRE(initial_liquidity.amount_y_used == 100 * coin);
        FS_REQUIRE(initial_liquidity.shares == 3'162'277'660ULL);

        // Execute both swap directions and assert exact atom-level results.
        const auto fst_quote = financial.quote(pool_id, "FST", coin);
        FS_REQUIRE(fst_quote.amount_out == 906'610'893ULL);
        FS_REQUIRE(fst_quote.fee_amount == 300'000ULL);
        const auto fst_swap = financial.swap(account_b, pool_id, "FST", coin, fst_quote.amount_out);
        FS_REQUIRE(fst_swap.id == 1);
        FS_REQUIRE(fst_swap.amount_out == 906'610'893ULL);

        const auto usd_quote = financial.quote(pool_id, "USD", coin);
        FS_REQUIRE(usd_quote.amount_out == 11'929'613ULL);
        FS_REQUIRE(usd_quote.fee_amount == 300'000ULL);
        const auto usd_swap = financial.swap(account_b, pool_id, "USD", coin, usd_quote.amount_out);
        FS_REQUIRE(usd_swap.id == 2);
        FS_REQUIRE(usd_swap.amount_out == 11'929'613ULL);
        FS_REQUIRE(financial.flow().swaps().size() == 2);

        // Impossible min-output must reject before any financial mutation.
        const auto rejected_revision = financial.revision();
        const auto rejected_pool = financial.flow().pool(pool_id).value();
        const auto rejected_fst = financial.balance(account_b, "FST");
        const auto rejected_usd = financial.balance(account_b, "USD");
        FS_REQUIRE_THROWS(financial.swap(account_b, pool_id, "USD", coin, coin));
        FS_REQUIRE(financial.revision() == rejected_revision);
        const auto after_rejection_pool = financial.flow().pool(pool_id).value();
        FS_REQUIRE(after_rejection_pool.reserve_x == rejected_pool.reserve_x);
        FS_REQUIRE(after_rejection_pool.reserve_y == rejected_pool.reserve_y);
        FS_REQUIRE(financial.balance(account_b, "FST").total == rejected_fst.total);
        FS_REQUIRE(financial.balance(account_b, "USD").total == rejected_usd.total);
        FS_REQUIRE(financial.flow().swaps().size() == 2);

        // Wallet B supplies liquidity with caps of 1 FST / 9 USD. The pool
        // consumes only the ratio-matched USD amount, not the entire cap.
        const auto second_liquidity = financial.add_liquidity(
            account_b, pool_id, coin, 9 * coin);
        FS_REQUIRE(second_liquidity.amount_x_used == coin);
        FS_REQUIRE(second_liquidity.amount_y_used == 844'925'955ULL);
        FS_REQUIRE(second_liquidity.shares == 290'631'718ULL);

        // Half withdrawal, followed by full withdrawal of the remainder.
        const auto partial = financial.remove_liquidity(account_b, pool_id, 145'315'859ULL);
        FS_REQUIRE(partial.amount_x_used == 49'999'999ULL);
        FS_REQUIRE(partial.amount_y_used == 422'462'977ULL);
        FS_REQUIRE(financial.flow().lp_shares_of(account_b, pool_id) == 145'315'859ULL);

        const auto final = financial.remove_liquidity(account_b, pool_id, 145'315'859ULL);
        FS_REQUIRE(final.amount_x_used == 50'000'000ULL);
        FS_REQUIRE(final.amount_y_used == 422'462'977ULL);
        FS_REQUIRE(financial.flow().lp_shares_of(account_b, pool_id) == 0);
        FS_REQUIRE(financial.flow().lp_shares_of(account_a, pool_id) == 3'162'277'660ULL);

        // Exact final settlement balances, including one-atom floor-rounding
        // dust retained by the pool after Wallet B's two redemptions.
        FS_REQUIRE(financial.balance(account_a, "FST").available() == 980 * coin);
        FS_REQUIRE(financial.balance(account_a, "USD").available() == 0);
        FS_REQUIRE(financial.balance(account_b, "FST").available() == 911'929'612ULL);
        FS_REQUIRE(financial.balance(account_b, "USD").available() == 990'806'610'892ULL);
        FS_REQUIRE(financial.balance(account_b, "FST").reserved == 0);
        FS_REQUIRE(financial.balance(account_b, "USD").reserved == 0);

        const auto final_pool = financial.flow().pool(pool_id).value();
        FS_REQUIRE(final_pool.reserve_x == 1'088'070'388ULL);
        FS_REQUIRE(final_pool.reserve_y == 9'193'389'108ULL);
        FS_REQUIRE(final_pool.total_lp_shares == 3'162'277'660ULL);
        FS_REQUIRE(financial.revision() == 14);
        FS_REQUIRE(financial.validate());
    }

    // The new non-interactive verifier exercises the same real database open
    // and cross-engine validation path that a user can run after restart.
    const auto report = application::verify_database(path);
    FS_REQUIRE(report.success());
#ifdef FEINSTATIC_TEST_EXECUTABLE
    // Exercise the actual user-facing command, not only its underlying API.
    const std::string cli_output_path = path + ".verify-output";
    const std::string cli_command = std::string("\"") + FEINSTATIC_TEST_EXECUTABLE +
        "\" verify \"" + path + "\" > \"" + cli_output_path + "\" 2>&1";
    FS_REQUIRE(std::system(cli_command.c_str()) == 0);
    std::ifstream cli_output_file(cli_output_path);
    FS_REQUIRE(cli_output_file.good());
    const std::string cli_output((std::istreambuf_iterator<char>(cli_output_file)),
                                 std::istreambuf_iterator<char>());
    FS_REQUIRE(cli_output.find("[PASS] Ledger chain") != std::string::npos);
    FS_REQUIRE(cli_output.find("[PASS] Shared platform") != std::string::npos);
    FS_REQUIRE(cli_output.find("Flow: pools=1 LP_positions=1 swaps=2") != std::string::npos);
    FS_REQUIRE(cli_output.find("RESULT: PASS") != std::string::npos);
#endif
    FS_REQUIRE(report.chain_height == 0);
    FS_REQUIRE(report.block_count == 1);
    FS_REQUIRE(report.wallet_count == 2);
    FS_REQUIRE(report.platform_revision == 14);
    FS_REQUIRE(report.platform_account_count == 2);
    FS_REQUIRE(report.match_order_count == 2);
    FS_REQUIRE(report.match_trade_count == 1);
    FS_REQUIRE(report.flow_pool_count == 1);
    FS_REQUIRE(report.flow_lp_position_count == 1);
    FS_REQUIRE(report.flow_swap_count == 2);
    FS_REQUIRE(report.settlement_journal_entries > 0);

    // Reopen again and inspect the recovered data itself, not just the
    // verifier's status flags. This catches state that validates but is lost.
    {
        auto recovered = FinancialPlatform::open(path);
        FS_REQUIRE(recovered.revision() == 14);
        FS_REQUIRE(recovered.match().trades().size() == 1);
        const auto recovered_buy = recovered.match().order(resting_buy);
        FS_REQUIRE(recovered_buy.has_value());
        FS_REQUIRE(recovered_buy->status == exchange::OrderStatus::PartiallyFilledCancelled);
        FS_REQUIRE(recovered.flow().swaps().size() == 2);
        FS_REQUIRE(recovered.flow().swaps()[0].asset_in == "FST");
        FS_REQUIRE(recovered.flow().swaps()[1].asset_in == "USD");
        FS_REQUIRE(recovered.flow().lp_shares_of(account_a, pool_id) == 3'162'277'660ULL);
        FS_REQUIRE(recovered.flow().lp_shares_of(account_b, pool_id) == 0);
        FS_REQUIRE(recovered.balance(account_b, "FST").total == 911'929'612ULL);
        FS_REQUIRE(recovered.balance(account_b, "USD").total == 990'806'610'892ULL);
        FS_REQUIRE(recovered.validate());
    }
}
