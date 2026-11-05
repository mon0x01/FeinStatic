#include "test.hpp"

#include "feinstat/consensus/fork_choice.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <chrono>
#include <filesystem>
#include <sqlite3.h>
#include <string>

using namespace feinstat;
using namespace feinstat::ledger;
using namespace feinstat::consensus;

namespace {

std::string consensus_test_db(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            (std::string("feinstatic-consensus-") + suffix + "-" + std::to_string(stamp) + ".db")).string();
}

void remove_consensus_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

void install_reorg_failure_trigger(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        const std::string error = db ? sqlite3_errmsg(db) : "unknown SQLite error";
        if (db) sqlite3_close(db);
        throw std::runtime_error("unable to open test database for trigger: " + error);
    }
    char* message = nullptr;
    const int result = sqlite3_exec(db,
        "CREATE TRIGGER fail_chain_replacement BEFORE DELETE ON blocks "
        "WHEN OLD.height > 0 BEGIN SELECT RAISE(ABORT, 'injected chain replacement failure'); END;",
        nullptr, nullptr, &message);
    const std::string error = message ? message : "";
    sqlite3_free(message);
    sqlite3_close(db);
    if (result != SQLITE_OK) throw std::runtime_error("unable to install test trigger: " + error);
}

} // namespace

FS_TEST(fork_choice_prefers_more_blocks_under_fixed_difficulty) {
    const Wallet alice = Wallet::generate();
    auto shorter = Ledger::initialize(":memory:", {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto longer = Ledger::initialize(":memory:", {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    (void)longer.mine(alice.address());
    (void)longer.mine(alice.address());
    const auto comparison = DeterministicForkChoice::compare(shorter.chain(), longer.chain());
    FS_REQUIRE(comparison.preference == ChainPreference::AdoptCandidate);
    FS_REQUIRE(comparison.common_ancestor_height == 0);
    FS_REQUIRE(!comparison.same_chain);
}

FS_TEST(fork_choice_equal_height_uses_deterministic_tip_hash_tie_break) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto left = Ledger::initialize(":memory:",
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto right = Ledger::initialize(":memory:",
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    (void)left.mine(alice.address());
    (void)right.mine(bob.address());
    const auto comparison = DeterministicForkChoice::compare(left.chain(), right.chain());
    const bool right_tip_lower = std::lexicographical_compare(
        right.chain().blocks().back().hash.begin(), right.chain().blocks().back().hash.end(),
        left.chain().blocks().back().hash.begin(), left.chain().blocks().back().hash.end());
    FS_REQUIRE(comparison.preference == (right_tip_lower ? ChainPreference::AdoptCandidate
                                                        : ChainPreference::KeepCurrent));
    FS_REQUIRE(comparison.common_ancestor_height == 0);
}

FS_TEST(ledger_reorganization_requeues_transactions_from_detached_branch_and_survives_restart) {
    const std::string local_path = consensus_test_db("requeue-local");
    const std::string remote_path = consensus_test_db("requeue-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "consensus-alice-passphrase");
    local.register_wallet(bob, "consensus-bob-passphrase");
    remote.register_wallet(alice, "consensus-alice-passphrase");
    remote.register_wallet(bob, "consensus-bob-passphrase");

    const auto detached_transaction = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(detached_transaction));
    (void)local.mine(bob.address());
    (void)remote.mine(alice.address());
    (void)remote.mine(alice.address());

    const auto adopted = local.consider_chain(remote.chain().blocks());
    FS_REQUIRE(adopted.adopted);
    FS_REQUIRE(!adopted.same_chain);
    FS_REQUIRE(adopted.previous_height == 1);
    FS_REQUIRE(adopted.resulting_height == 2);
    FS_REQUIRE(adopted.common_ancestor_height == 0);
    FS_REQUIRE(adopted.requeued_transactions == 1);
    FS_REQUIRE(adopted.dropped_detached_transactions == 0);
    FS_REQUIRE(local.chain().balance_of(alice.address()) == 102 * kAtomsPerCoin);
    FS_REQUIRE(local.chain().balance_of(bob.address()) == 50 * kAtomsPerCoin);
    FS_REQUIRE(local.mempool().transactions().size() == 1);
    FS_REQUIRE(local.mempool().transactions().front().id() == detached_transaction.id());

    auto recovered = Ledger::open(local_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 2);
    FS_REQUIRE(recovered.chain().blocks().back().hash == remote.chain().blocks().back().hash);
    FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    FS_REQUIRE(recovered.mempool().transactions().front().id() == detached_transaction.id());
    remove_consensus_db(local_path);
    remove_consensus_db(remote_path);
}

FS_TEST(ledger_reorganization_drops_detached_transaction_conflicting_with_candidate_nonce) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const Wallet carol = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin},
        {carol.address(), 10 * kAtomsPerCoin}};
    auto local = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "consensus-alice-passphrase");
    remote.register_wallet(alice, "consensus-alice-passphrase");
    const auto detached = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    const auto candidate = remote.create_transfer(alice, carol.address(), 7 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(detached));
    FS_REQUIRE(remote.submit(candidate));
    (void)local.mine(bob.address());
    (void)remote.mine(carol.address());
    (void)remote.mine(carol.address());

    const auto adopted = local.consider_chain(remote.chain().blocks());
    FS_REQUIRE(adopted.adopted);
    FS_REQUIRE(adopted.requeued_transactions == 0);
    FS_REQUIRE(adopted.dropped_detached_transactions == 1);
    FS_REQUIRE(local.mempool().transactions().empty());
    FS_REQUIRE(local.chain().nonce_of(alice.address()) == 1);
    FS_REQUIRE(local.chain().balance_of(carol.address()) == 19 * kAtomsPerCoin);
}

FS_TEST(ledger_reorganization_does_not_commit_if_atomic_sqlite_replacement_fails) {
    const std::string local_path = consensus_test_db("atomic-local");
    const std::string remote_path = consensus_test_db("atomic-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);
    (void)local.mine(alice.address());
    (void)remote.mine(bob.address());
    (void)remote.mine(bob.address());
    const Hash old_tip = local.chain().blocks().back().hash;
    install_reorg_failure_trigger(local_path);

    FS_REQUIRE_THROWS(local.consider_chain(remote.chain().blocks()));
    FS_REQUIRE(local.chain().height() == 1);
    FS_REQUIRE(local.chain().blocks().back().hash == old_tip);
    auto recovered = Ledger::open(local_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 1);
    FS_REQUIRE(recovered.chain().blocks().back().hash == old_tip);
    remove_consensus_db(local_path);
    remove_consensus_db(remote_path);
}

FS_TEST(ledger_reorganization_preserves_pending_external_transaction_in_durable_mempool) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "pending-bridge-alice-passphrase");
    const auto pending = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(pending));
    (void)remote.mine(bob.address());
    const auto hash = pending.id();
    const std::string id = crypto::hex(hash.data(), hash.size());

    const auto result = local.consider_chain(remote.chain().blocks(), {{id, 0}});
    FS_REQUIRE(result.adopted);
    FS_REQUIRE(local.mempool().contains(id));
    FS_REQUIRE(local.chain().height() == 1);
}

FS_TEST(ledger_reorganization_refuses_to_orphan_or_move_protected_external_transactions) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "protected-alice-passphrase");
    const auto external = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(external));
    (void)local.mine(bob.address());
    (void)remote.mine(alice.address());
    (void)remote.mine(alice.address());
    const Hash original_tip = local.chain().blocks().back().hash;
    const auto external_hash = external.id();
    const std::string external_id = crypto::hex(external_hash.data(), external_hash.size());

    FS_REQUIRE_THROWS(local.consider_chain(remote.chain().blocks(), {{external_id, 1}}));
    FS_REQUIRE(local.chain().height() == 1);
    FS_REQUIRE(local.chain().blocks().back().hash == original_tip);
    FS_REQUIRE(local.chain().find_transaction(external_id).has_value());

    // Even if the same transaction appears on the candidate branch, moving a
    // height-pinned Bridge confirmation would invalidate its durable journal.
    auto moved = Ledger::initialize(":memory:", allocations, 0, kAtomsPerCoin);
    moved.register_wallet(alice, "protected-alice-passphrase");
    (void)moved.mine(alice.address());
    const auto same_tx = Transaction::create_signed(alice, bob.address(), 5 * kAtomsPerCoin, 0, 1);
    FS_REQUIRE(moved.submit(same_tx));
    (void)moved.mine(bob.address());
    FS_REQUIRE_THROWS(local.consider_chain(moved.chain().blocks(), {{external_id, 1}}));
    FS_REQUIRE(local.chain().blocks().back().hash == original_tip);
}

FS_TEST(ledger_reorganization_rejects_wrong_genesis_without_mutation) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto local = Ledger::initialize(":memory:", {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(":memory:", {{bob.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    (void)remote.mine(bob.address());
    FS_REQUIRE_THROWS(local.consider_chain(remote.chain().blocks()));
    FS_REQUIRE(local.chain().height() == 0);
    FS_REQUIRE(local.chain().blocks().front().hash != remote.chain().blocks().front().hash);
}
