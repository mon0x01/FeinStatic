#include "test.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/utxo/mempool.hpp"
#include "feinstat/utxo/types.hpp"
#include "feinstat/utxo/utxo_set.hpp"

#include <cstdint>
#include <utility>

using namespace feinstat;
using namespace feinstat::utxo;
using namespace feinstat::crypto;

namespace {

ChainId alice_chain{};
ChainId bob_chain{};

void fill_chain_id(ChainId& id, Byte value) {
    id.fill(value);
}

KeyPair alice = generate_ed25519_keypair();
KeyPair bob = generate_ed25519_keypair();

// Build a transaction whose inputs are all spendable by `secret_key` and whose
// witness signatures cover the chain-bound digest.
Transaction build_signed_tx(const ChainId& chain_id,
                            std::vector<Input> inputs,
                            std::vector<Output> outputs,
                            const SecretKey& secret_key) {
    Transaction tx{std::move(inputs), std::move(outputs)};
    const TransactionId digest = transaction_id(chain_id, tx);
    const Bytes digest_bytes(digest.begin(), digest.end());
    for (auto& input : tx.inputs) {
        input.witness = sign_ed25519(secret_key, digest_bytes);
    }
    return tx;
}

// Spend a specific outpoint (known to be funded by `secret_key`) and create
// the given outputs, returning the transaction and its id.
Transaction spend(const ChainId& chain_id, const Outpoint& outpoint,
                  std::vector<Output> outputs, const SecretKey& secret_key) {
    return build_signed_tx(chain_id, {Input{outpoint, Signature{}}},
                           std::move(outputs), secret_key);
}

} // namespace

FS_TEST(utxo_encode_decode_roundtrip) {
    fill_chain_id(alice_chain, 0x41);
    Transaction tx{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{1'000ULL * kAtomsPerCoin, alice.public_key}},
    };
    const Bytes encoded = encode_transaction(tx);
    const Transaction decoded = decode_transaction(encoded);
    FS_REQUIRE(decoded.inputs.size() == 1);
    FS_REQUIRE(decoded.outputs.size() == 1);
    FS_REQUIRE(decoded.inputs[0].outpoint.output_index == 0);
    FS_REQUIRE(decoded.outputs[0].amount == 1'000ULL * kAtomsPerCoin);
    FS_REQUIRE(decoded.outputs[0].spending_key == alice.public_key);
}

FS_TEST(utxo_decode_rejects_trailing_bytes) {
    Transaction tx{{}, {Output{kAtomsPerCoin, alice.public_key}}};
    const Bytes encoded = encode_transaction(tx);
    Bytes padded = encoded;
    padded.push_back(0x00);
    FS_REQUIRE_THROWS(decode_transaction(padded));
}

FS_TEST(utxo_digest_is_deterministic) {
    fill_chain_id(alice_chain, 0x41);
    Transaction tx{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{1'000ULL * kAtomsPerCoin, alice.public_key}},
    };
    FS_REQUIRE(transaction_id(alice_chain, tx) == transaction_id(alice_chain, tx));
}

FS_TEST(utxo_digest_is_chain_bound) {
    fill_chain_id(alice_chain, 0x41);
    fill_chain_id(bob_chain, 0x42);
    Transaction tx{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{1'000ULL * kAtomsPerCoin, alice.public_key}},
    };
    FS_REQUIRE(transaction_id(alice_chain, tx) != transaction_id(bob_chain, tx));
}

FS_TEST(utxo_digest_changes_with_content) {
    fill_chain_id(alice_chain, 0x41);
    Transaction tx_a{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{1'000ULL * kAtomsPerCoin, alice.public_key}},
    };
    Transaction tx_b{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{2'000ULL * kAtomsPerCoin, alice.public_key}},
    };
    FS_REQUIRE(transaction_id(alice_chain, tx_a) != transaction_id(alice_chain, tx_b));
}

FS_TEST(utxo_validate_valid_transaction_passes) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{700ULL * kAtomsPerCoin, alice.public_key},
                            Output{200ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);

    const auto result = validate_transaction(alice_chain, tx, set);
    FS_REQUIRE(result.valid);
    FS_REQUIRE(result.fee == 100ULL * kAtomsPerCoin);
}

FS_TEST(utxo_validate_duplicate_input_rejected) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx = build_signed_tx(alice_chain,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}},
                                      Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{1'000ULL * kAtomsPerCoin, alice.public_key}},
                                     alice.secret_key);
    const auto result = validate_transaction(alice_chain, tx, set);
    FS_REQUIRE(!result.valid);
}

FS_TEST(utxo_validate_insufficient_value_rejected) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{2'000ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    const auto result = validate_transaction(alice_chain, tx, set);
    FS_REQUIRE(!result.valid);
}

FS_TEST(utxo_validate_unknown_outpoint_rejected) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set;
    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{1'000ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    const auto result = validate_transaction(alice_chain, tx, set);
    FS_REQUIRE(!result.valid);
}

FS_TEST(utxo_validate_bad_signature_rejected) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{500ULL * kAtomsPerCoin, bob.public_key}},
    };
    const TransactionId digest = transaction_id(alice_chain, tx);
    const Bytes digest_bytes(digest.begin(), digest.end());
    tx.inputs[0].witness = sign_ed25519(bob.secret_key, digest_bytes);
    const auto result = validate_transaction(alice_chain, tx, set);
    FS_REQUIRE(!result.valid);
}

FS_TEST(utxo_apply_consumes_inputs_and_creates_outputs) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{600ULL * kAtomsPerCoin, alice.public_key},
                            Output{300ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);

    const Amount fee = set.apply(alice_chain, tx);
    FS_REQUIRE(fee == 100ULL * kAtomsPerCoin);
    FS_REQUIRE(!set.contains(Outpoint{Hash{}, 0}));
    const TransactionId tx_id = transaction_id(alice_chain, tx);
    FS_REQUIRE(set.contains(Outpoint{tx_id, 0}));
    FS_REQUIRE(set.contains(Outpoint{tx_id, 1}));
    FS_REQUIRE(set.get(Outpoint{tx_id, 0}).amount == 600ULL * kAtomsPerCoin);
    FS_REQUIRE(set.get(Outpoint{tx_id, 1}).amount == 300ULL * kAtomsPerCoin);
}

FS_TEST(utxo_single_consumption_prevents_double_spend) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    set.apply(alice_chain, tx);

    FS_REQUIRE_THROWS(set.apply(alice_chain, tx));
}

FS_TEST(utxo_state_root_is_reproducible) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet a = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});
    UtxoSet b = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});

    FS_REQUIRE(a.state_root() == b.state_root());

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{400ULL * kAtomsPerCoin, alice.public_key},
                            Output{500ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    a.apply(alice_chain, tx);
    b.apply(alice_chain, tx);
    FS_REQUIRE(a.state_root() == b.state_root());
}

FS_TEST(utxo_balance_query) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {bob.public_key, 500ULL * kAtomsPerCoin},
        {alice.public_key, 250ULL * kAtomsPerCoin},
    });
    FS_REQUIRE(set.balance(alice.public_key) == 1'250ULL * kAtomsPerCoin);
    FS_REQUIRE(set.balance(bob.public_key) == 500ULL * kAtomsPerCoin);
    KeyPair nobody;
    FS_REQUIRE(set.balance(nobody.public_key) == 0);
}

FS_TEST(utxo_genesis_allocation_respects_limits) {
    FS_REQUIRE_THROWS(
        (UtxoSet::genesis({{alice.public_key, kMaxAmount + 1}})));
}

FS_TEST(utxo_mempool_admits_valid_transaction) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});
    Mempool pool(1'000, 1'000'000);

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    std::string error;
    FS_REQUIRE(pool.add(alice_chain, set, tx, &error));
    FS_REQUIRE(pool.size() == 1);
    FS_REQUIRE(pool.contains(transaction_id(alice_chain, tx)));
}

FS_TEST(utxo_mempool_rejects_double_spend_within_pool) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});
    Mempool pool(1'000, 1'000'000);

    Transaction tx_a = spend(alice_chain, Outpoint{Hash{}, 0},
                             {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                             alice.secret_key);
    const TransactionId id_a = transaction_id(alice_chain, tx_a);
    std::string error;
    FS_REQUIRE(pool.add(alice_chain, set, tx_a, &error));

    Transaction tx_b = spend(alice_chain, Outpoint{Hash{}, 0},
                             {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                             alice.secret_key);
    FS_REQUIRE(!pool.add(alice_chain, set, tx_b, &error));
    FS_REQUIRE(pool.size() == 1);
    FS_REQUIRE(pool.contains(id_a));
}

FS_TEST(utxo_mempool_remove_spent_on_confirmation) {
    fill_chain_id(alice_chain, 0x41);
    UtxoSet set = UtxoSet::genesis({{alice.public_key, 1'000ULL * kAtomsPerCoin}});
    Mempool pool(1'000, 1'000'000);

    Transaction tx = spend(alice_chain, Outpoint{Hash{}, 0},
                           {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                           alice.secret_key);
    std::string error;
    FS_REQUIRE(pool.add(alice_chain, set, tx, &error));
    FS_REQUIRE(pool.size() == 1);

    pool.remove_spent(Outpoint{Hash{}, 0});
    FS_REQUIRE(pool.size() == 0);
    FS_REQUIRE(!pool.contains(transaction_id(alice_chain, tx)));
}

FS_TEST(utxo_mempool_evicts_oldest_when_full) {
    fill_chain_id(alice_chain, 0x41);
    Mempool pool(2, 1'000'000);
    UtxoSet set = UtxoSet::genesis({
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
    });

    Transaction tx_a = spend(alice_chain, Outpoint{Hash{}, 0},
                             {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                             alice.secret_key);
    Transaction tx_b = spend(alice_chain, Outpoint{Hash{}, 1},
                             {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                             alice.secret_key);
    std::string error;
    FS_REQUIRE(pool.add(alice_chain, set, tx_a, &error));
    FS_REQUIRE(pool.add(alice_chain, set, tx_b, &error));
    FS_REQUIRE(pool.size() == 2);

    Transaction tx_c = spend(alice_chain, Outpoint{Hash{}, 2},
                             {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                             alice.secret_key);
    FS_REQUIRE(pool.add(alice_chain, set, tx_c, &error));
    FS_REQUIRE(pool.size() == 2);
    FS_REQUIRE(!pool.contains(transaction_id(alice_chain, tx_a)));
    FS_REQUIRE(pool.contains(transaction_id(alice_chain, tx_b)));
    FS_REQUIRE(pool.contains(transaction_id(alice_chain, tx_c)));
}

FS_TEST(utxo_transactions_are_deterministically_ordered) {
    fill_chain_id(alice_chain, 0x41);
    Mempool pool(100, 1'000'000);
    UtxoSet set = UtxoSet::genesis({
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
        {alice.public_key, 1'000ULL * kAtomsPerCoin},
    });

    for (std::uint32_t i = 0; i < 5; ++i) {
        Transaction tx = spend(alice_chain, Outpoint{Hash{}, i},
                               {Output{500ULL * kAtomsPerCoin, bob.public_key}},
                               alice.secret_key);
        std::string error;
        FS_REQUIRE(pool.add(alice_chain, set, tx, &error));
    }
    const auto ordered = pool.transactions();
    FS_REQUIRE(ordered.size() == 5);
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        const TransactionId prev = transaction_id(alice_chain, ordered[i - 1]);
        const TransactionId curr = transaction_id(alice_chain, ordered[i]);
        FS_REQUIRE(prev < curr);
    }
}
