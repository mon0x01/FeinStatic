#include "test.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace feinstat;
using namespace feinstat::ledger;

namespace {

std::string mempool_test_db(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            (std::string("feinstatic-mempool-") + suffix + "-" + std::to_string(stamp) + ".db")).string();
}

void remove_mempool_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

void execute_sql(const std::string& path, const char* sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        const std::string detail = db == nullptr ? "unable to open test database" : sqlite3_errmsg(db);
        if (db != nullptr) sqlite3_close(db);
        throw std::runtime_error(detail);
    }
    char* message = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    const std::string detail = message == nullptr ? std::string{} : message;
    sqlite3_free(message);
    const int close_result = sqlite3_close(db);
    if (result != SQLITE_OK) throw std::runtime_error("test SQL failed: " + detail);
    if (close_result != SQLITE_OK) throw std::runtime_error("unable to close test SQLite handle");
}

} // namespace

FS_TEST(durable_mempool_survives_restart_and_preserves_sequential_nonce_order) {
    const std::string path = mempool_test_db("restart");
    remove_mempool_db(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 100 * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "durable-mempool-alice-passphrase");
        ledger.register_wallet(bob, "durable-mempool-bob-passphrase");
        const auto first = ledger.create_transfer(alice, bob.address(), 10 * kAtomsPerCoin, 7);
        const auto second = Transaction::create_signed(alice, bob.address(), 15 * kAtomsPerCoin, 9, 2);
        FS_REQUIRE(ledger.submit(first));
        FS_REQUIRE(ledger.submit(second));
        FS_REQUIRE(ledger.mempool().transactions().size() == 2);
        FS_REQUIRE(ledger.mempool().serialized_bytes() == first.serialize().size() + second.serialize().size());
    }

    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 2);
        FS_REQUIRE(recovered.mempool().transactions()[0].nonce == 1);
        FS_REQUIRE(recovered.mempool().transactions()[1].nonce == 2);
        const auto next = recovered.create_transfer(
            recovered.unlock_wallet(alice.address(), "durable-mempool-alice-passphrase"),
            bob.address(), 5 * kAtomsPerCoin, 11);
        FS_REQUIRE(next.nonce == 3);
        std::string error;
        FS_REQUIRE(!recovered.submit(recovered.mempool().transactions().front(), &error));
        FS_REQUIRE(error.find("duplicate") != std::string::npos || error.find("replayed") != std::string::npos);
        FS_REQUIRE(recovered.submit(next));
    }

    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 3);
        const auto block = recovered.mine(bob.address());
        FS_REQUIRE(block.transactions.size() == 3);
        FS_REQUIRE(recovered.mempool().transactions().empty());
        FS_REQUIRE(recovered.chain().nonce_of(alice.address()) == 3);
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.chain().height() == 1);
        FS_REQUIRE(recovered.mempool().transactions().empty());
        FS_REQUIRE(recovered.chain().balance_of(alice.address()) == 70 * kAtomsPerCoin - 27);
        FS_REQUIRE(recovered.chain().balance_of(bob.address()) == 32 * kAtomsPerCoin + 27);
    }
    remove_mempool_db(path);
}

FS_TEST(durable_mempool_recovery_rejects_payload_id_mismatch) {
    const std::string path = mempool_test_db("tamper");
    remove_mempool_db(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 10 * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "mempool-tamper-alice-passphrase");
        const auto transaction = ledger.create_transfer(alice, bob.address(), kAtomsPerCoin, 0);
        FS_REQUIRE(ledger.submit(transaction));
    }
    execute_sql(path,
        "UPDATE mempool_transactions SET transaction_id=lower(hex(zeroblob(32)));");
    FS_REQUIRE_THROWS(Ledger::open(path, 0));
    remove_mempool_db(path);
}

FS_TEST(durable_mempool_schema_v2_migration_is_additive) {
    const std::string path = mempool_test_db("migration-v2");
    remove_mempool_db(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 50 * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "mempool-migration-alice-passphrase");
        ledger.register_wallet(bob, "mempool-migration-bob-passphrase");
    }
    execute_sql(path, "DROP TABLE mempool_transactions; PRAGMA user_version=2;");
    {
        auto migrated = Ledger::open(path, 0);
        FS_REQUIRE(migrated.chain().height() == 0);
        FS_REQUIRE(migrated.chain().balance_of(alice.address()) == 50 * kAtomsPerCoin);
        FS_REQUIRE(migrated.wallets().size() == 2);
        const auto transaction = migrated.create_transfer(alice, bob.address(), 4 * kAtomsPerCoin, 0);
        FS_REQUIRE(migrated.submit(transaction));
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.chain().height() == 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
        FS_REQUIRE(recovered.mempool().transactions().front().nonce == 1);
    }
    remove_mempool_db(path);
}

FS_TEST(block_append_and_mempool_clear_commit_atomically) {
    const std::string path = mempool_test_db("atomic-mine");
    remove_mempool_db(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 20 * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "atomic-mine-alice-passphrase");
        ledger.register_wallet(bob, "atomic-mine-bob-passphrase");
        const auto transaction = ledger.create_transfer(alice, bob.address(), 3 * kAtomsPerCoin, 0);
        FS_REQUIRE(ledger.submit(transaction));
        execute_sql(path,
            "CREATE TRIGGER reject_mempool_clear BEFORE DELETE ON mempool_transactions "
            "BEGIN SELECT RAISE(ABORT, 'injected mempool-clear failure'); END;");
        FS_REQUIRE_THROWS(ledger.mine(bob.address()));
        FS_REQUIRE(ledger.chain().height() == 0);
        FS_REQUIRE(ledger.mempool().transactions().size() == 1);
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.chain().height() == 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    }
    execute_sql(path, "DROP TRIGGER reject_mempool_clear;");
    {
        auto recovered = Ledger::open(path, 0);
        (void)recovered.mine(bob.address());
        FS_REQUIRE(recovered.chain().height() == 1);
        FS_REQUIRE(recovered.mempool().transactions().empty());
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.chain().height() == 1);
        FS_REQUIRE(recovered.mempool().transactions().empty());
    }
    remove_mempool_db(path);
}

FS_TEST(peer_block_append_and_mempool_reconciliation_commit_atomically) {
    const std::string source_path = mempool_test_db("atomic-peer-source");
    const std::string target_path = mempool_test_db("atomic-peer-target");
    remove_mempool_db(source_path);
    remove_mempool_db(target_path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    Block source_block;
    {
        auto source = Ledger::initialize(source_path,
            {{alice.address(), 50 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}}, 0);
        source.register_wallet(alice, "atomic-peer-alice-passphrase");
        source.register_wallet(bob, "atomic-peer-bob-passphrase");
        const auto transaction = source.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
        FS_REQUIRE(source.submit(transaction));
        source_block = source.mine(alice.address());
    }
    {
        auto target = Ledger::initialize(target_path,
            {{alice.address(), 50 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}}, 0);
        target.register_wallet(alice, "atomic-peer-alice-passphrase");
        target.register_wallet(bob, "atomic-peer-bob-passphrase");
        const auto pending = target.create_transfer(bob, alice.address(), 7 * kAtomsPerCoin, 0);
        FS_REQUIRE(target.submit(pending));
        execute_sql(target_path,
            "CREATE TRIGGER reject_peer_mempool_reconcile BEFORE DELETE ON mempool_transactions "
            "BEGIN SELECT RAISE(ABORT, 'injected peer-mempool reconciliation failure'); END;");
        FS_REQUIRE_THROWS(target.accept_blocks({source_block}));
        FS_REQUIRE(target.chain().height() == 0);
        FS_REQUIRE(target.mempool().transactions().size() == 1);
    }
    {
        auto recovered = Ledger::open(target_path, 0);
        FS_REQUIRE(recovered.chain().height() == 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    }
    execute_sql(target_path, "DROP TRIGGER reject_peer_mempool_reconcile;");
    {
        auto target = Ledger::open(target_path, 0);
        target.accept_blocks({source_block});
        FS_REQUIRE(target.chain().height() == 1);
        FS_REQUIRE(target.mempool().transactions().size() == 1);
        FS_REQUIRE(target.mempool().transactions().front().sender == bob.address());
    }
    {
        auto recovered = Ledger::open(target_path, 0);
        FS_REQUIRE(recovered.chain().height() == 1);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    }
    remove_mempool_db(source_path);
    remove_mempool_db(target_path);
}

FS_TEST(mempool_submit_storage_failure_does_not_publish_in_memory_transaction) {
    const std::string path = mempool_test_db("atomic-submit");
    remove_mempool_db(path);
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{alice.address(), 15 * kAtomsPerCoin}}, 0);
        ledger.register_wallet(alice, "atomic-submit-alice-passphrase");
        const auto transaction = ledger.create_transfer(alice, bob.address(), 2 * kAtomsPerCoin, 0);
        execute_sql(path,
            "CREATE TRIGGER reject_mempool_insert BEFORE INSERT ON mempool_transactions "
            "BEGIN SELECT RAISE(ABORT, 'injected mempool-insert failure'); END;");
        FS_REQUIRE_THROWS(ledger.submit(transaction));
        FS_REQUIRE(ledger.mempool().transactions().empty());
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.mempool().transactions().empty());
    }
    execute_sql(path, "DROP TRIGGER reject_mempool_insert;");
    {
        auto recovered = Ledger::open(path, 0);
        const auto transaction = recovered.create_transfer(
            recovered.unlock_wallet(alice.address(), "atomic-submit-alice-passphrase"),
            bob.address(), 2 * kAtomsPerCoin, 0);
        FS_REQUIRE(recovered.submit(transaction));
    }
    {
        auto recovered = Ledger::open(path, 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    }
    remove_mempool_db(path);
}
