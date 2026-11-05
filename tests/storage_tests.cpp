#include "test.hpp"

#include "feinstat/ledger/ledger.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <sqlite3.h>

using namespace feinstat;
using namespace feinstat::ledger;

namespace {
std::string temporary_database_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            ("feinstatic-test-" + std::to_string(stamp) + ".db")).string();
}
void remove_database(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}
}

FS_TEST(sqlite_restart_recovers_multiple_blocks_and_rejects_replay) {
    const std::string path = temporary_database_path();
    remove_database(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    Transaction first_transaction;
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 50ULL * kAtomsPerCoin}}, 1);
        struct stat database_status {};
        FS_REQUIRE(::stat(path.c_str(), &database_status) == 0);
        FS_REQUIRE((database_status.st_mode & 0077) == 0);
        ledger.register_wallet(alice, "a-strong-local-passphrase");
        ledger.register_wallet(bob, "another-local-passphrase");
        first_transaction = ledger.create_transfer(alice, bob.address(), 7ULL * kAtomsPerCoin, 0);
        FS_REQUIRE(ledger.submit(first_transaction));
        (void)ledger.mine(alice.address());
    }
    {
        auto recovered = Ledger::open(path, 1);
        FS_REQUIRE(recovered.chain().height() == 1);
        FS_REQUIRE(recovered.chain().balance_of(alice.address()) == 45ULL * kAtomsPerCoin);
        FS_REQUIRE(recovered.chain().balance_of(bob.address()) == 7ULL * kAtomsPerCoin);
        FS_REQUIRE(recovered.wallets().size() == 2);
        const Wallet unlocked = recovered.unlock_wallet(alice.address(), "a-strong-local-passphrase");
        FS_REQUIRE(unlocked.address() == alice.address());
        FS_REQUIRE_THROWS(recovered.unlock_wallet(alice.address(), "incorrect"));
        std::string replay_error;
        FS_REQUIRE(!recovered.submit(first_transaction, &replay_error));
        FS_REQUIRE(replay_error.find("replayed") != std::string::npos ||
                   replay_error.find("duplicate") != std::string::npos);

        const auto second_transaction = recovered.create_transfer(
            unlocked, bob.address(), 3ULL * kAtomsPerCoin, 0);
        FS_REQUIRE(second_transaction.nonce == 2);
        FS_REQUIRE(recovered.submit(second_transaction));
        (void)recovered.mine(bob.address());
        FS_REQUIRE(recovered.chain().height() == 2);
        FS_REQUIRE(recovered.chain().nonce_of(alice.address()) == 2);
        FS_REQUIRE(recovered.chain().validate());
    }
    {
        const auto recovered_again = Ledger::open(path, 1);
        FS_REQUIRE(recovered_again.chain().height() == 2);
        FS_REQUIRE(recovered_again.chain().blocks().size() == 3);
        FS_REQUIRE(recovered_again.chain().balance_of(alice.address()) == 42ULL * kAtomsPerCoin);
        FS_REQUIRE(recovered_again.chain().balance_of(bob.address()) == 12ULL * kAtomsPerCoin);
        FS_REQUIRE(recovered_again.chain().total_supply() == 54ULL * kAtomsPerCoin);
        FS_REQUIRE(recovered_again.chain().nonce_of(alice.address()) == 2);
        FS_REQUIRE(recovered_again.chain().validate());
    }
    remove_database(path);
}

FS_TEST(sqlite_recovery_rejects_corrupted_persisted_block_payload) {
    const std::string path = temporary_database_path();
    remove_database(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 50ULL * kAtomsPerCoin}}, 1);
        ledger.register_wallet(alice, "corruption-test-passphrase");
        const auto tx = ledger.create_transfer(alice, bob.address(), 5ULL * kAtomsPerCoin, 0);
        FS_REQUIRE(ledger.submit(tx));
        (void)ledger.mine(alice.address());
    }

    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int result = sqlite3_exec(db,
        "UPDATE blocks SET payload=zeroblob(length(payload)) WHERE height=1;",
        nullptr, nullptr, &error);
    if (error != nullptr) sqlite3_free(error);
    FS_REQUIRE(result == SQLITE_OK);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);
    FS_REQUIRE_THROWS(Ledger::open(path, 1));
    remove_database(path);
}


FS_TEST(sqlite_recovery_rejects_mismatched_hash_index_metadata) {
    const std::string path = temporary_database_path();
    remove_database(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 20ULL * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "hash-index-check-passphrase");
        const auto tx = ledger.create_transfer(alice, bob.address(), 2ULL * kAtomsPerCoin, 0);
        FS_REQUIRE(ledger.submit(tx));
        (void)ledger.mine(alice.address());
    }

    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int result = sqlite3_exec(db,
        "UPDATE blocks SET hash=zeroblob(32) WHERE height=1;",
        nullptr, nullptr, &error);
    if (error != nullptr) sqlite3_free(error);
    FS_REQUIRE(result == SQLITE_OK);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);
    FS_REQUIRE_THROWS(Ledger::open(path, 0));
    remove_database(path);
}

FS_TEST(sqlite_rejects_unknown_future_schema_version) {
    const std::string path = temporary_database_path();
    remove_database(path);
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    FS_REQUIRE(sqlite3_exec(db, "PRAGMA user_version=999;", nullptr, nullptr, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);
    FS_REQUIRE_THROWS(Ledger::open(path));
    remove_database(path);
}

FS_TEST(open_missing_ledger_database_does_not_create_a_file) {
    const std::string path = temporary_database_path();
    remove_database(path);
    FS_REQUIRE(!std::filesystem::exists(path));
    FS_REQUIRE_THROWS(Ledger::open(path));
    FS_REQUIRE(!std::filesystem::exists(path));
}

FS_TEST(wallet_list_rejects_corrupted_public_identity) {
    const std::string path = temporary_database_path();
    remove_database(path);
    const Wallet alice = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 10ULL * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "wallet-integrity-test-passphrase");
    }
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    const int update_result = sqlite3_exec(db,
        "UPDATE wallets SET public_key=zeroblob(32);", nullptr, nullptr, nullptr);
    FS_REQUIRE(update_result == SQLITE_OK);
    FS_REQUIRE(sqlite3_close(db) == SQLITE_OK);
    {
        auto ledger = Ledger::open(path, 0);
        FS_REQUIRE_THROWS(ledger.wallets());
    }
    remove_database(path);
}
