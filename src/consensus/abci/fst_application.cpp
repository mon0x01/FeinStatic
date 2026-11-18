#include "feinstat/consensus/abci/fst_application.hpp"

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/utxo/types.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace feinstat::consensus::abci {

namespace {

std::uint64_t utilization_ppm(std::size_t used, std::size_t capacity) {
    if (capacity == 0) {
        return 0;
    }
    const std::uint64_t scaled =
        static_cast<std::uint64_t>(used) * 1'000'000ULL /
        static_cast<std::uint64_t>(capacity);
    return std::min(scaled, static_cast<std::uint64_t>(1'000'000ULL));
}

Bytes validator_update_to_bytes(const staking::ValidatorUpdate& update) {
    ByteWriter writer;
    writer.fixed(update.consensus_key);
    writer.u64(update.voting_power);
    return std::move(writer).take();
}

} // namespace

FstAbciApplication::FstAbciApplication(ChainId chain_id,
                                       staking::StakeParams stake_params,
                                       monetary::MonetaryParams monetary_params)
    : chain_id_(std::move(chain_id)),
      staking_state_(std::move(stake_params)),
      monetary_engine_(std::move(monetary_params)) {}

ResponseInfo FstAbciApplication::info(const RequestInfo& /*request*/) {
    ResponseInfo response;
    response.data = "FST deterministic ABCI application v0";
    response.version = "0.1.0";
    response.app_version = 1;
    response.last_block_height = static_cast<std::int64_t>(height_);
    response.last_block_app_hash = compute_app_hash();
    return response;
}

ResponseInitChain FstAbciApplication::init_chain(
    const RequestInitChain& request) {
    chain_id_ = request.chain_id;
    height_ = 0;

    // Register the genesis validators. They start with zero bonded stake
    // (bonding happens via staking transactions). Voting power is derived
    // deterministically from bonded stake once bonds are made.
    for (const auto& update : request.validators) {
        staking_state_.register_validator(update.pub_key, update.pub_key);
    }

    // Initialize genesis UTXO set from allocations.
    std::vector<std::pair<PublicKey, Amount>> allocations;
    allocations.reserve(request.genesis_allocations.size());
    for (const auto& alloc : request.genesis_allocations) {
        allocations.emplace_back(alloc.first, alloc.second);
    }
    utxo_set_ = UtxoSet::genesis(allocations);

    clear_block_state();

    ResponseInitChain response;
    for (const auto& vu : staking_state_.validator_updates(0)) {
        ResponseInitChain::ValidatorUpdate ru;
        ru.pub_key = vu.consensus_key;
        ru.power = static_cast<std::int64_t>(vu.voting_power);
        response.validators.push_back(ru);
    }
    return response;
}

ResponseCheckTx FstAbciApplication::check_tx(
    const RequestCheckTx& request) {
    ResponseCheckTx response;

    Transaction tx;
    try {
        tx = decode_tx(request.tx);
    } catch (const std::exception& e) {
        response.code = ResponseCheckTx::Code::invalid_transaction;
        response.log = std::string("failed to decode transaction: ") + e.what();
        return response;
    }

    const auto result = utxo::validate_transaction(chain_id_, tx, utxo_set_);
    if (!result.valid) {
        response.code = ResponseCheckTx::Code::invalid_transaction;
        response.log = result.error;
        return response;
    }

    if (result.fee < prev_base_fee_) {
        response.code = ResponseCheckTx::Code::insufficient_funds;
        response.log = "transaction fee below minimum base fee";
        return response;
    }

    std::string error;
    if (!mempool_.add(chain_id_, utxo_set_, tx, &error)) {
        response.code = ResponseCheckTx::Code::double_spend;
        response.log = error;
        return response;
    }

    response.code = ResponseCheckTx::Code::ok;
    response.log = "ok";
    response.gas_wanted = 1;
    response.gas_used = 1;
    return response;
}

ResponsePrepareProposal FstAbciApplication::prepare_proposal(
    const RequestPrepareProposal& request) {
    ResponsePrepareProposal response;

    const auto all_txs = mempool_.transactions();
    std::size_t total_bytes = 0;
    const std::size_t max_bytes =
        (request.max_tx_bytes > 0)
            ? static_cast<std::size_t>(request.max_tx_bytes)
            : kMaxBlockBytes;

    for (const auto& tx : all_txs) {
        const std::size_t tx_bytes = utxo::encode_transaction(tx).size();
        if (total_bytes + tx_bytes > max_bytes) {
            break;
        }
        total_bytes += tx_bytes;
        response.txs.push_back(utxo::encode_transaction(tx));
    }

    return response;
}

ResponseProcessProposal FstAbciApplication::process_proposal(
    const RequestProcessProposal& request) {
    ResponseProcessProposal response;

    std::set<Outpoint> block_spent;
    for (const auto& dtx : delivered_txs_) {
        for (const auto& input : dtx.inputs) {
            block_spent.insert(input.outpoint);
        }
    }

    for (const auto& raw_tx : request.txs) {
        Transaction tx;
        try {
            tx = decode_tx(raw_tx);
        } catch (const std::exception& e) {
            response.status = ProposalStatus::Reject;
            return response;
        }

        for (const auto& input : tx.inputs) {
            if (!utxo_set_.contains(input.outpoint)) {
                response.status = ProposalStatus::Reject;
                return response;
            }
            if (block_spent.find(input.outpoint) != block_spent.end()) {
                response.status = ProposalStatus::Reject;
                return response;
            }
            block_spent.insert(input.outpoint);
        }

        const auto inputs_total = utxo::sum_inputs(tx, utxo_set_);
        const auto outputs_total = utxo::sum_outputs(tx);
        if (!inputs_total || !outputs_total || *outputs_total > *inputs_total) {
            response.status = ProposalStatus::Reject;
            return response;
        }

        const TransactionId digest =
            utxo::transaction_digest(chain_id_, tx);
        const Bytes digest_bytes(digest.begin(), digest.end());
        for (const auto& input : tx.inputs) {
            const PublicKey spending_key =
                utxo_set_.get(input.outpoint).spending_key;
            if (!crypto::verify_ed25519(spending_key, digest_bytes,
                                        input.witness)) {
                response.status = ProposalStatus::Reject;
                return response;
            }
        }
    }

    response.status = ProposalStatus::Accept;
    return response;
}

ResponseFinalizeBlock FstAbciApplication::finalize_block(
    const RequestFinalizeBlock& request) {
    ResponseFinalizeBlock response;

    // Fork detection: a block at or below the CometBFT-finalized height is a
    // stale or conflicting block. CometBFT remains authoritative for the
    // canonical chain; this is defense-in-depth application-level tracking.
    if (finality_checker_.would_fork(
            static_cast<std::uint64_t>(request.height))) {
        ResponseFinalizeBlock::Event ev;
        ev.type = "fork_detected";
        response.events.push_back(std::move(ev));
    }

    height_ = static_cast<staking::Height>(request.height);

    // Epoch transition (FST-REQ-0037, ADR-0003): at each epoch boundary the
    // inactivity leak is applied to inactive bonded validators and the
    // active committee is rotated (recomputed from bonded stake).
    // Deterministic in height; a no-op unless this block begins a new epoch.
    const staking::EpochInfo epoch_transition =
        staking_state_.transition_epoch(height_);
    (void)epoch_transition;

    // Apply each transaction in the block (v0.40.0: no DeliverTx).
    std::set<Outpoint> block_spent;
    for (const auto& raw_tx : request.txs) {
        Transaction tx;
        try {
            tx = decode_tx(raw_tx);
        } catch (const std::exception& e) {
            ResponseFinalizeBlock::ExecTxResult result;
            result.code = 1; // invalid transaction
            result.log = std::string("failed to decode: ") + e.what();
            response.tx_results.push_back(result);
            continue;
        }

        // Validate against UTXO set and block-spent outpoints.
        bool valid = true;
        std::string error;
        for (const auto& input : tx.inputs) {
            if (!utxo_set_.contains(input.outpoint)) {
                valid = false;
                error = "input references spent or unknown outpoint";
                break;
            }
            if (block_spent.find(input.outpoint) != block_spent.end()) {
                valid = false;
                error = "double-spend within block";
                break;
            }
            block_spent.insert(input.outpoint);
        }

        if (valid) {
            const auto inputs_total = utxo::sum_inputs(tx, utxo_set_);
            const auto outputs_total = utxo::sum_outputs(tx);
            if (!inputs_total || !outputs_total || *outputs_total > *inputs_total) {
                valid = false;
                error = "value not conserved";
            }
        }

        if (valid) {
            const TransactionId digest =
                utxo::transaction_digest(chain_id_, tx);
            const Bytes digest_bytes(digest.begin(), digest.end());
            for (const auto& input : tx.inputs) {
                const PublicKey spending_key =
                    utxo_set_.get(input.outpoint).spending_key;
                if (!crypto::verify_ed25519(spending_key, digest_bytes,
                                            input.witness)) {
                    valid = false;
                    error = "invalid witness signature";
                    break;
                }
            }
        }

        if (!valid) {
            ResponseFinalizeBlock::ExecTxResult result;
            result.code = 1;
            result.log = error;
            response.tx_results.push_back(result);
            continue;
        }

        // Apply the transaction.
        const Amount implicit_fee = utxo_set_.apply(chain_id_, tx);

        // Split fee into base fee (burned) and priority fee (to validator).
        const Amount base_fee_portion =
            (implicit_fee >= prev_base_fee_) ? prev_base_fee_ : implicit_fee;
        const Amount priority_fee_portion = implicit_fee - base_fee_portion;

        block_base_fees_ += base_fee_portion;
        block_priority_fees_ += priority_fee_portion;
        block_total_tx_bytes_ += raw_tx.size();

        delivered_txs_.push_back(tx);
        for (const auto& input : tx.inputs) {
            spent_outpoints_.push_back(input.outpoint);
        }

        ResponseFinalizeBlock::ExecTxResult result;
        result.code = 0;
        response.tx_results.push_back(result);
    }

    // 1. Compute issuance for this block (deterministic in height).
    const Amount issuance = monetary_engine_.apply_issuance(height_);

    // 2. Compute congestion-adjusted base fee from this block's utilization.
    const std::uint64_t utilization =
        utilization_ppm(block_total_tx_bytes_, kMaxBlockBytes);
    const Amount base_fee =
        monetary_engine_.adjust_base_fee(prev_base_fee_, utilization);
    prev_base_fee_ = base_fee;

    // 3. Apply fees: base fees burned, priority fees to validator.
    monetary_engine_.apply_fees(block_base_fees_, block_priority_fees_);

    // 4. Total validator reward = issuance + priority fees.
    const Amount validator_reward = issuance + block_priority_fees_;

    // 5. Add reward to staking pool and distribute.
    staking_state_.add_reward(validator_reward);
    staking_state_.distribute_rewards();

    // 6. Derive validator updates from staking state. CometBFT applies these
    //    with activation-height semantics: updates returned at height H take
    //    effect at height H+2.
    const auto validator_set = staking_state_.validator_set(height_);
    for (const auto& vu : validator_set.updates) {
        ResponseFinalizeBlock::ValidatorUpdate ru;
        ru.pub_key = vu.consensus_key;
        ru.power = static_cast<std::int64_t>(vu.voting_power);
        response.validator_updates.push_back(ru);
    }

    // 7. Emit events for observability.
    auto emit_event = [&](const char* type, const std::string& value) {
        ResponseFinalizeBlock::Event ev;
        ev.type = type;
        response.events.push_back(std::move(ev));
        (void)value;
    };
    emit_event("issuance", std::to_string(issuance));
    emit_event("base_fee_burned", std::to_string(block_base_fees_));
    emit_event("priority_fees", std::to_string(block_priority_fees_));
    emit_event("validator_reward", std::to_string(validator_reward));

    // Record the deterministic state commitment as the app hash (CometBFT
    // returns this in the block header; v0.40.0: app_hash from FinalizeBlock).
    response.app_hash = compute_app_hash();
    last_app_hash_ = response.app_hash;

    return response;
}

ResponseCommit FstAbciApplication::commit(const RequestCommit& /*request*/) {
    ResponseCommit response;

    response.retain_height = static_cast<std::int64_t>(height_);

    // Record the CometBFT-finalized checkpoint. If this commit conflicts with
    // an existing finalized checkpoint, a fork is detected; the application
    // tracks it but CometBFT remains authoritative for fork resolution.
    const bool fork_detected =
        !finality_checker_.record_commit(static_cast<std::uint64_t>(height_), last_app_hash_);
    (void)fork_detected;

    cleanup_mempool();

    clear_block_state();

    return response;
}

ResponseListSnapshots FstAbciApplication::list_snapshots(
    const RequestListSnapshots& /*request*/) {
    ResponseListSnapshots response;
    return response;
}

ResponseOfferSnapshot FstAbciApplication::offer_snapshot(
    const RequestOfferSnapshot& /*request*/) {
    ResponseOfferSnapshot response;
    response.result = ResponseOfferSnapshot::Result::Reject;
    return response;
}

ResponseLoadSnapshotChunk FstAbciApplication::load_snapshot_chunk(
    const RequestLoadSnapshotChunk& /*request*/) {
    ResponseLoadSnapshotChunk response;
    return response;
}

ResponseApplySnapshotChunk FstAbciApplication::apply_snapshot_chunk(
    const RequestApplySnapshotChunk& /*request*/) {
    ResponseApplySnapshotChunk response;
    response.result = ResponseApplySnapshotChunk::Result::Retry;
    return response;
}

ResponseEcho FstAbciApplication::echo(const RequestEcho& request) {
    ResponseEcho response;
    response.message = request.message;
    return response;
}

ResponseExtendVote FstAbciApplication::extend_vote(
    const RequestExtendVote& /*request*/) {
    ResponseExtendVote response;
    return response;
}

ResponseVerifyVoteExtension FstAbciApplication::verify_vote_extension(
    const RequestVerifyVoteExtension& /*request*/) {
    ResponseVerifyVoteExtension response;
    response.status = VerifyStatus::Accept;
    return response;
}

ApplicationSnapshot FstAbciApplication::snapshot() const {
    ApplicationSnapshot snap;
    snap.utxo_state_root = utxo_set_.state_root();
    snap.validator_updates.clear();
    for (const auto& vu : staking_state_.validator_set(height_).updates) {
        ResponseInitChain::ValidatorUpdate ru;
        ru.pub_key = vu.consensus_key;
        ru.power = static_cast<std::int64_t>(vu.voting_power);
        snap.validator_updates.push_back(ru);
    }
    snap.supply_snapshot = monetary_engine_.snapshot(height_);
    snap.height = height_;
    return snap;
}

void FstAbciApplication::restore(const ApplicationSnapshot& snap) {
    height_ = snap.height;
    prev_base_fee_ = 1'000ULL;
    clear_block_state();
    (void)snap;
}

ChainId FstAbciApplication::chain_id() const noexcept {
    return chain_id_;
}

staking::Height FstAbciApplication::height() const noexcept {
    return height_;
}

std::uint64_t FstAbciApplication::finalized_height() const noexcept {
    return finality_checker_.finalized_height();
}

FinalityStatus FstAbciApplication::finality_status(std::uint64_t height) const noexcept {
    return finality_checker_.finality_status(height);
}

Transaction FstAbciApplication::decode_tx(
    const std::vector<feinstat::Byte>& raw) const {
    return utxo::decode_transaction(Bytes(raw.begin(), raw.end()));
}

Amount FstAbciApplication::compute_base_fee(
    std::uint64_t utilization_ppm) const {
    return monetary_engine_.adjust_base_fee(prev_base_fee_, utilization_ppm);
}

feinstat::Hash FstAbciApplication::compute_app_hash() const {
    ByteWriter writer;
    writer.fixed(utxo_set_.state_root());
    const auto validator_set = staking_state_.validator_set(height_);
    ByteWriter stake_writer;
    for (const auto& update : validator_set.updates) {
        const Bytes vb = validator_update_to_bytes(update);
        stake_writer.raw(vb.data(), vb.size());
    }
    writer.fixed(crypto::sha256(std::move(stake_writer).take()));
    const auto supply = monetary_engine_.snapshot(height_);
    ByteWriter money_writer;
    money_writer.u64(supply.total_supply);
    money_writer.u64(supply.burned);
    money_writer.u64(supply.total_bonded_stake);
    money_writer.u64(supply.cumulative_issued);
    writer.fixed(crypto::sha256(std::move(money_writer).take()));
    writer.u64(height_);
    return crypto::sha256(writer.data());
}

void FstAbciApplication::clear_block_state() {
    delivered_txs_.clear();
    spent_outpoints_.clear();
    block_base_fees_ = 0;
    block_priority_fees_ = 0;
    block_total_tx_bytes_ = 0;
}

void FstAbciApplication::cleanup_mempool() {
    for (const auto& outpoint : spent_outpoints_) {
        mempool_.remove_spent(outpoint);
    }
}

ResponseQuery FstAbciApplication::query(const RequestQuery& /*request*/) {
    ResponseQuery response;
    response.code = 0;
    return response;
}

ResponseInsertTx FstAbciApplication::insert_tx(
    const RequestInsertTx& request) {
    ResponseInsertTx response;
    try {
        auto tx = decode_tx(request.tx);
        auto result = utxo::validate_transaction(chain_id_, tx, utxo_set_);
        if (result.valid) {
            response.code = 0;
        } else {
            response.code = 1;
        }
    } catch (...) {
        response.code = 1;
    }
    return response;
}

ResponseReapTxs FstAbciApplication::reap_txs(
    const RequestReapTxs& request) {
    ResponseReapTxs response;
    const auto all_txs = mempool_.transactions();
    std::size_t total_bytes = 0;
    for (const auto& tx : all_txs) {
        const std::size_t tx_bytes = utxo::encode_transaction(tx).size();
        if (total_bytes + tx_bytes > request.max_bytes) {
            break;
        }
        total_bytes += tx_bytes;
        response.txs.push_back(utxo::encode_transaction(tx));
    }
    (void)request.max_gas;
    return response;
}

std::unique_ptr<AbciApplication> make_fst_application(
    const ChainId& chain_id,
    const staking::StakeParams& stake_params,
    const monetary::MonetaryParams& monetary_params) {
    return std::make_unique<FstAbciApplication>(
        ChainId(chain_id), staking::StakeParams(stake_params),
        monetary::MonetaryParams(monetary_params));
}

} // namespace feinstat::consensus::abci
