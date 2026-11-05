#include "test.hpp"

#include "feinstat/ledger/ledger.hpp"

#include <limits>

using namespace feinstat;
using namespace feinstat::ledger;

FS_TEST(signed_transfer_mines_and_updates_balances) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:",
        {{alice.address(), 1'000ULL * kAtomsPerCoin}}, 1, 2ULL * kAtomsPerCoin);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(bob, "bob-strong-secret");

    const auto transaction = ledger.create_transfer(alice, bob.address(),
        25ULL * kAtomsPerCoin, 1'000'000ULL);
    std::string error;
    FS_REQUIRE(ledger.submit(transaction, &error));
    FS_REQUIRE(ledger.mempool().transactions().size() == 1);

    const auto block = ledger.mine(bob.address());
    FS_REQUIRE(block.height == 1);
    FS_REQUIRE(block.transactions.size() == 1);
    FS_REQUIRE(ledger.chain().balance_of(alice.address()) == 975ULL * kAtomsPerCoin - 1'000'000ULL);
    FS_REQUIRE(ledger.chain().balance_of(bob.address()) == 27ULL * kAtomsPerCoin + 1'000'000ULL);
    FS_REQUIRE(ledger.chain().total_supply() == 1'002ULL * kAtomsPerCoin);
    FS_REQUIRE(ledger.mempool().transactions().empty());
    FS_REQUIRE(ledger.chain().validate(&error));
}

FS_TEST(mempool_rejects_bad_signature_nonce_and_replay) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:", {{alice.address(), 100ULL * kAtomsPerCoin}}, 0);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(bob, "bob-strong-secret");

    auto invalid_signature = Transaction::create_signed(alice, bob.address(), kAtomsPerCoin, 0, 1);
    invalid_signature.signature[0] ^= 0x80;
    std::string error;
    FS_REQUIRE(!ledger.submit(invalid_signature, &error));

    const auto wrong_nonce = Transaction::create_signed(alice, bob.address(), kAtomsPerCoin, 0, 2);
    FS_REQUIRE(!ledger.submit(wrong_nonce, &error));

    const auto valid = ledger.create_transfer(alice, bob.address(), kAtomsPerCoin, 0);
    FS_REQUIRE(ledger.submit(valid, &error));
    FS_REQUIRE(!ledger.submit(valid, &error));
    (void)ledger.mine(alice.address());
    FS_REQUIRE(!ledger.submit(valid, &error));
}

FS_TEST(pending_transactions_use_sequential_nonces) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:", {{alice.address(), 20ULL * kAtomsPerCoin}}, 0);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(bob, "bob-strong-secret");

    const auto first = ledger.create_transfer(alice, bob.address(), 2ULL * kAtomsPerCoin, 0);
    FS_REQUIRE(ledger.submit(first));
    const auto second = ledger.create_transfer(alice, bob.address(), 3ULL * kAtomsPerCoin, 0);
    FS_REQUIRE(second.nonce == 2);
    FS_REQUIRE(ledger.submit(second));
    FS_REQUIRE(ledger.mempool().transactions().size() == 2);
}

FS_TEST(self_transfer_does_not_create_balance) {
    const Wallet alice = Wallet::generate();
    const Wallet miner = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:", {{alice.address(), 10ULL * kAtomsPerCoin}}, 0);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(miner, "miner-strong-secret");
    const auto transaction = ledger.create_transfer(alice, alice.address(), 3ULL * kAtomsPerCoin, 1'000'000ULL);
    FS_REQUIRE(ledger.submit(transaction));
    (void)ledger.mine(miner.address());
    FS_REQUIRE(ledger.chain().balance_of(alice.address()) == 10ULL * kAtomsPerCoin - 1'000'000ULL);
    FS_REQUIRE(ledger.chain().balance_of(miner.address()) == 2ULL * kAtomsPerCoin + 1'000'000ULL);
}

FS_TEST(block_tampering_is_detected) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:", {{alice.address(), 10ULL * kAtomsPerCoin}}, 0);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(bob, "bob-strong-secret");
    const auto transaction = ledger.create_transfer(alice, bob.address(), kAtomsPerCoin, 0);
    FS_REQUIRE(ledger.submit(transaction));
    auto block = ledger.chain().make_next_block(ledger.mempool().transactions(), alice.address());
    block.reward += 1;
    std::string error;
    FS_REQUIRE(!ledger.chain().can_append(block, &error));
}

FS_TEST(insufficient_funds_and_amount_overflow_are_rejected) {
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto ledger = Ledger::initialize(":memory:", {{alice.address(), kAtomsPerCoin}}, 0);
    ledger.register_wallet(alice, "alice-strong-secret");
    ledger.register_wallet(bob, "bob-strong-secret");

    const auto too_large = ledger.create_transfer(alice, bob.address(), 2ULL * kAtomsPerCoin, 0);
    std::string error;
    FS_REQUIRE(!ledger.submit(too_large, &error));
    FS_REQUIRE_THROWS(Transaction::create_signed(alice, bob.address(), UINT64_MAX, 1, 1));
    FS_REQUIRE_THROWS(Transaction::create_signed(alice, bob.address(), 0, 0, 1));
}
