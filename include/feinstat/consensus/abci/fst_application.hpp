#pragma once

#include "feinstat/consensus/abci/application.hpp"
#include "feinstat/monetary/engine.hpp"
#include "feinstat/staking/state.hpp"
#include "feinstat/utxo/mempool.hpp"
#include "feinstat/utxo/types.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace feinstat::consensus::abci {

// Maximum block size in bytes (testnet-labelled; not production policy).
inline constexpr std::size_t kMaxBlockBytes = 1'000'000ULL;

// Deterministic FST ABCI application. Combines UTXO ledger, staking/validator
// state, and monetary/fee accounting into a single deterministic state
// machine. Every method is a pure function of prior state + inputs: no wall
// clock, no unordered iteration, no floating point, no filesystem input, no
// external API calls. Given identical committed transactions and prior state,
// every correct node computes identical resulting state and app_hash.
//
// Implements the ABCI v0.40.0 interface:
// - DeliverTx does NOT exist; transactions are applied in FinalizeBlock.
// - app_hash is returned from FinalizeBlock, not Commit.
// - InsertTx/ReapTxs support application-managed mempool.
// - Validator updates use activation-height semantics (H -> H+2).
class FstAbciApplication : public AbciApplication {
public:
    FstAbciApplication(ChainId chain_id,
                       staking::StakeParams stake_params,
                       monetary::MonetaryParams monetary_params);

    // --- AbciApplication -------------------------------------------------

    ResponseInfo info(const RequestInfo& request) override;

    ResponseInitChain init_chain(const RequestInitChain& request) override;

    ResponseCheckTx check_tx(const RequestCheckTx& request) override;

    ResponseQuery query(const RequestQuery& request) override;

    ResponseInsertTx insert_tx(const RequestInsertTx& request) override;

    ResponseReapTxs reap_txs(const RequestReapTxs& request) override;

    ResponsePrepareProposal prepare_proposal(
        const RequestPrepareProposal& request) override;

    ResponseProcessProposal process_proposal(
        const RequestProcessProposal& request) override;

    ResponseFinalizeBlock finalize_block(
        const RequestFinalizeBlock& request) override;

    ResponseExtendVote extend_vote(
        const RequestExtendVote& request) override;

    ResponseVerifyVoteExtension verify_vote_extension(
        const RequestVerifyVoteExtension& request) override;

    ResponseCommit commit(const RequestCommit& request) override;

    ResponseListSnapshots list_snapshots(
        const RequestListSnapshots& request) override;

    ResponseOfferSnapshot offer_snapshot(
        const RequestOfferSnapshot& request) override;

    ResponseLoadSnapshotChunk load_snapshot_chunk(
        const RequestLoadSnapshotChunk& request) override;

    ResponseApplySnapshotChunk apply_snapshot_chunk(
        const RequestApplySnapshotChunk& request) override;

    ResponseEcho echo(const RequestEcho& request) override;

    [[nodiscard]] ApplicationSnapshot snapshot() const override;

    void restore(const ApplicationSnapshot& snapshot) override;

    [[nodiscard]] ChainId chain_id() const noexcept override;

    [[nodiscard]] staking::Height height() const noexcept override;

private:
    ChainId chain_id_;
    staking::Height height_{0};

    UtxoSet utxo_set_;
    staking::StakingState staking_state_;
    monetary::MonetaryEngine monetary_engine_;
    Mempool mempool_{10'000, 50'000'000ULL};

    // Base fee carried over from the previous block (for congestion
    // adjustment). Testnet-labelled initial value.
    Amount prev_base_fee_{1'000ULL};

    // State accumulated during the current block (cleared at commit).
    std::vector<Transaction> delivered_txs_;
    std::vector<Outpoint> spent_outpoints_;
    Amount block_base_fees_{0};
    Amount block_priority_fees_{0};
    std::size_t block_total_tx_bytes_{0};

    // Decode a transaction from raw bytes. Throws std::runtime_error on
    // malformed data.
    [[nodiscard]] Transaction decode_tx(
        const std::vector<feinstat::Byte>& raw) const;

    // Validate and apply a single transaction to the UTXO set. Returns the
    // implicit fee (sum(inputs) - sum(outputs)). Throws on invalid tx.
    [[nodiscard]] Amount apply_delivered_tx(const Transaction& tx);

    // Compute the congestion-adjusted base fee for a block with the given
    // utilization (parts per million of block capacity).
    [[nodiscard]] Amount compute_base_fee(std::uint64_t utilization_ppm) const;

    // Compute the deterministic application hash over the combined state.
    [[nodiscard]] feinstat::Hash compute_app_hash() const;

    // Clear per-block tracking state.
    void clear_block_state();

    // Remove all spent outpoints from the mempool (called at commit).
    void cleanup_mempool();
};

} // namespace feinstat::consensus::abci
