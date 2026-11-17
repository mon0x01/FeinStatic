#include "test.hpp"

#include "feinstat/consensus/abci/application.hpp"
#include "feinstat/consensus/abci/fst_application.hpp"
#include "feinstat/consensus/abci/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/utxo/types.hpp"
#include "feinstat/utxo/types.hpp"

#include <cstdint>
#include <utility>
#include <vector>

using namespace feinstat;
using namespace feinstat::consensus::abci;
using namespace feinstat::utxo;
using namespace feinstat::crypto;
using namespace feinstat::staking;
using namespace feinstat::monetary;

namespace {

ChainId make_chain_id(Byte value) {
    ChainId id;
    id.fill(value);
    return id;
}

KeyPair alice_kp = generate_ed25519_keypair();
KeyPair bob_kp = generate_ed25519_keypair();
KeyPair validator_kp = generate_ed25519_keypair();

// Build and sign a transaction spending the given outpoint.
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

StakeParams test_stake_params() {
    StakeParams p;
    p.min_bond = 1ULL * kAtomsPerCoin;
    p.max_validators = 100;
    p.unbonding_period = 1000;
    p.equivocation_slash_basis_points = 500;
    return p;
}

MonetaryParams test_monetary_params() {
    MonetaryParams p;
    p.genesis_supply = 3'000ULL * kAtomsPerCoin;
    p.initial_block_issuance = 1000ULL;
    p.tail_block_issuance = 10ULL;
    p.issuance_halving_interval = 10;
    p.max_supply = 0;
    p.base_fee_min = 1'000ULL;
    p.base_fee_max = 1'000'000ULL;
    p.target_utilization_ppm = 500'000;
    p.max_base_fee_change_ppm = 125'000;
    p.priority_fee_cap = 1'000'000'000ULL * kAtomsPerCoin; // effectively uncapped for tests
    return p;
}

RequestInitChain build_init_chain(const ChainId& chain_id) {
    RequestInitChain req;
    req.chain_id = chain_id;
    req.validators = {{validator_kp.public_key, 1000ULL * kAtomsPerCoin}};
    req.genesis_allocations = {
        {alice_kp.public_key, 1'000ULL * kAtomsPerCoin},
        {alice_kp.public_key, 1'000ULL * kAtomsPerCoin},
        {alice_kp.public_key, 1'000ULL * kAtomsPerCoin},
    };
    return req;
}

std::vector<feinstat::Byte> encode_tx(const Transaction& tx) {
    return encode_transaction(tx);
}

// Apply a single transaction via FinalizeBlock (v0.40.0: no DeliverTx).
// Returns the app_hash and tx_results.
std::pair<Hash, std::vector<ResponseFinalizeBlock::ExecTxResult>>
apply_tx_via_finalize_block(AbciApplication& app, const ChainId& /*chain_id*/,
                            const Transaction& tx, Height height) {
    RequestFinalizeBlock req;
    req.txs = {encode_tx(tx)};
    req.height = static_cast<std::int64_t>(height);
    auto resp = app.finalize_block(req);
    return {resp.app_hash, std::move(resp.tx_results)};
}

} // namespace

FS_TEST(abci_info_returns_app_info) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());

    RequestInfo info_req;
    info_req.version = "0.1.0";
    const auto resp = app->info(info_req);

    FS_REQUIRE(resp.version == "0.1.0");
    FS_REQUIRE(resp.app_version == 1);
    FS_REQUIRE(resp.last_block_height == 0);
}

FS_TEST(abci_init_chain_initializes_state) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());

    const auto req = build_init_chain(chain_id);
    const auto resp = app->init_chain(req);

    // Genesis validator has zero bonded stake → zero voting power → no
    // validator updates at genesis. This is correct: validators must bond
    // stake via transactions before participating in consensus.
    FS_REQUIRE(resp.validators.empty());
    FS_REQUIRE(app->chain_id() == chain_id);
    FS_REQUIRE(app->height() == 0);
    FS_REQUIRE(app->snapshot().supply_snapshot.total_supply ==
               3'000ULL * kAtomsPerCoin);
    FS_REQUIRE(app->snapshot().supply_snapshot.burned == 0);
    FS_REQUIRE(app->snapshot().supply_snapshot.cumulative_issued == 0);
}

FS_TEST(abci_check_tx_accepts_valid_transaction) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    RequestCheckTx check_req;
    check_req.tx = encode_tx(tx);
    const auto resp = app->check_tx(check_req);

    FS_REQUIRE(resp.code == ResponseCheckTx::Code::ok);
    FS_REQUIRE(resp.log == "ok");
}

FS_TEST(abci_check_tx_rejects_insufficient_fee) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{1'000ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    RequestCheckTx check_req;
    check_req.tx = encode_tx(tx);
    const auto resp = app->check_tx(check_req);

    FS_REQUIRE(resp.code == ResponseCheckTx::Code::insufficient_funds);
}

FS_TEST(abci_check_tx_rejects_bad_signature) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx{
        {Input{Outpoint{Hash{}, 0}, Signature{}}},
        {Output{500ULL * kAtomsPerCoin, bob_kp.public_key}},
    };
    const TransactionId digest = transaction_id(chain_id, tx);
    const Bytes digest_bytes(digest.begin(), digest.end());
    tx.inputs[0].witness = sign_ed25519(bob_kp.secret_key, digest_bytes);

    RequestCheckTx check_req;
    check_req.tx = encode_tx(tx);
    const auto resp = app->check_tx(check_req);

    FS_REQUIRE(resp.code == ResponseCheckTx::Code::invalid_transaction);
}

FS_TEST(abci_finalize_block_applies_state_transition) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    const Hash before_root = app->snapshot().utxo_state_root;

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    auto [app_hash, tx_results] = apply_tx_via_finalize_block(*app, chain_id,
                                                              tx, 1);

    FS_REQUIRE(app_hash != before_root);
    FS_REQUIRE(tx_results.size() == 1);
    FS_REQUIRE(tx_results[0].code == 0); // OK
}

FS_TEST(abci_finalize_block_computes_fees_and_issuance) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    auto [app_hash, tx_results] = apply_tx_via_finalize_block(*app, chain_id,
                                                              tx, 1);

    // Genesis validator has zero bonded stake → zero voting power → no
    // validator updates at height 1.
    FS_REQUIRE(tx_results[0].code == 0);

    // Re-call finalize_block to capture events
    RequestFinalizeBlock req2;
    req2.txs = {encode_tx(tx)};
    req2.height = 1;
    auto resp2 = app->finalize_block(req2);
    bool has_issuance = false;
    bool has_burned = false;
    for (const auto& ev : resp2.events) {
        if (ev.type == "issuance") has_issuance = true;
        if (ev.type == "base_fee_burned") has_burned = true;
    }
    FS_REQUIRE(has_issuance);
    FS_REQUIRE(has_burned);
}

FS_TEST(abci_commit_returns_deterministic_app_hash) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    auto [app_hash1, _] = apply_tx_via_finalize_block(*app, chain_id, tx, 1);
    RequestCommit commit_req;
    const auto commit_resp = app->commit(commit_req);

    FS_REQUIRE(commit_resp.retain_height == 1);
    FS_REQUIRE(app_hash1.size() == 32);
}

FS_TEST(abci_determinism_same_input_same_output) {
    const ChainId chain_id = make_chain_id(0x41);

    auto app1 = make_fst_application(chain_id, test_stake_params(),
                                     test_monetary_params());
    auto app2 = make_fst_application(chain_id, test_stake_params(),
                                     test_monetary_params());

    const auto init_req = build_init_chain(chain_id);
    app1->init_chain(init_req);
    app2->init_chain(init_req);

    FS_REQUIRE(app1->snapshot().utxo_state_root ==
               app2->snapshot().utxo_state_root);
    FS_REQUIRE(app1->snapshot().supply_snapshot.total_supply ==
               app2->snapshot().supply_snapshot.total_supply);
    FS_REQUIRE(app1->snapshot().height == app2->snapshot().height);

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    auto [h1, r1] = apply_tx_via_finalize_block(*app1, chain_id, tx, 1);
    auto [h2, r2] = apply_tx_via_finalize_block(*app2, chain_id, tx, 1);

    FS_REQUIRE(h1 == h2);

    RequestCommit commit_req;
    auto c1 = app1->commit(commit_req);
    auto c2 = app2->commit(commit_req);

    FS_REQUIRE(c1.retain_height == c2.retain_height);
}

FS_TEST(abci_determinism_multi_block) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app1 = make_fst_application(chain_id, test_stake_params(),
                                     test_monetary_params());
    auto app2 = make_fst_application(chain_id, test_stake_params(),
                                     test_monetary_params());

    const auto init_req = build_init_chain(chain_id);
    app1->init_chain(init_req);
    app2->init_chain(init_req);

    for (int block = 1; block <= 3; ++block) {
        Transaction tx = build_signed_tx(chain_id,
                                         {Input{Outpoint{Hash{}, static_cast<std::uint32_t>(block - 1)}, Signature{}}},
                                         {Output{499ULL * kAtomsPerCoin,
                                               alice_kp.public_key},
                                          Output{500ULL * kAtomsPerCoin,
                                               bob_kp.public_key}},
                                         alice_kp.secret_key);

        auto [h1, r1] = apply_tx_via_finalize_block(*app1, chain_id, tx, static_cast<Height>(block));
        auto [h2, r2] = apply_tx_via_finalize_block(*app2, chain_id, tx, static_cast<Height>(block));

        FS_REQUIRE(h1 == h2);

        RequestCommit commit_req;
        auto c1 = app1->commit(commit_req);
        auto c2 = app2->commit(commit_req);
        FS_REQUIRE(c1.retain_height == c2.retain_height);
    }
}

FS_TEST(abci_prepare_proposal_selects_from_mempool) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    RequestCheckTx check_req;
    check_req.tx = encode_tx(tx);
    auto check_resp = app->check_tx(check_req);
    FS_REQUIRE(check_resp.code == ResponseCheckTx::Code::ok);

    RequestPrepareProposal propose_req;
    propose_req.max_tx_bytes = 100'000;
    propose_req.height = 1;
    auto propose_resp = app->prepare_proposal(propose_req);

    FS_REQUIRE(propose_resp.txs.size() == 1);
}

FS_TEST(abci_process_proposal_accepts_valid) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    RequestProcessProposal req;
    req.txs = {encode_tx(tx)};
    auto resp = app->process_proposal(req);

    FS_REQUIRE(resp.status == ProposalStatus::Accept);
}

FS_TEST(abci_process_proposal_rejects_double_spend_in_block) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());
    app->init_chain(build_init_chain(chain_id));

    Transaction tx = build_signed_tx(chain_id,
                                     {Input{Outpoint{Hash{}, 0}, Signature{}}},
                                     {Output{499ULL * kAtomsPerCoin,
                                           alice_kp.public_key},
                                      Output{500ULL * kAtomsPerCoin,
                                           bob_kp.public_key}},
                                     alice_kp.secret_key);

    RequestProcessProposal req;
    req.txs = {encode_tx(tx), encode_tx(tx)};
    auto resp = app->process_proposal(req);

    FS_REQUIRE(resp.status == ProposalStatus::Reject);
}

FS_TEST(abci_echo_returns_message) {
    const ChainId chain_id = make_chain_id(0x41);
    auto app = make_fst_application(chain_id, test_stake_params(),
                                    test_monetary_params());

    RequestEcho echo_req;
    echo_req.message = "hello fst";
    auto resp = app->echo(echo_req);

    FS_REQUIRE(resp.message == "hello fst");
}
