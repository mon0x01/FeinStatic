#pragma once

#include "feinstat/consensus/abci/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace feinstat::consensus::abci {

// Abstract interface for an ABCI v0.40.0 application. Implementations must be
// deterministic: given identical committed transactions and prior state, every
// correct node must calculate identical resulting application state and
// application hash. No non-deterministic external API call, wall-clock
// dependency, unordered iteration, floating-point calculation or local
// filesystem input may influence consensus state transitions.
//
// The application owns the complete deterministic FST state: UTXO ledger,
// staking/validator state, and monetary/fee accounting. The socket server
// drives the lifecycle via the local Unix-domain socket using the ABCI
// protocol and converts between protobuf wire format and these FST-native
// types.
//
// v0.40.0 interface methods:
//   Info/Query Connection:       Info, Query
//   Mempool Connection:          CheckTx, InsertTx, ReapTxs
//   Consensus Connection:        InitChain, PrepareProposal, ProcessProposal,
//                                FinalizeBlock, ExtendVote, VerifyVoteExtension,
//                                Commit
//   State Sync Connection:       ListSnapshots, OfferSnapshot,
//                                LoadSnapshotChunk, ApplySnapshotChunk
//
// NOTE: DeliverTx does NOT exist in v0.40.0. Transactions are delivered via
// FinalizeBlock(req.txs). Each transaction's result is returned in
// ResponseFinalizeBlock.tx_results in the same order.
//
// NOTE: app_hash is returned from FinalizeBlock, NOT from Commit.
// ResponseCommit only contains retain_height.
//
// NOTE: Validator updates use activation-height semantics: updates returned at
// height H take effect at height H+2.
class AbciApplication {
public:
    virtual ~AbciApplication() = default;

    // --- Info/Query Connection -------------------------------------------

    virtual ResponseInfo info(const RequestInfo& request) = 0;

    virtual ResponseQuery query(const RequestQuery& request) = 0;

    // --- Mempool Connection ---------------------------------------------

    virtual ResponseCheckTx check_tx(const RequestCheckTx& request) = 0;

    virtual ResponseInsertTx insert_tx(const RequestInsertTx& request) = 0;

    virtual ResponseReapTxs reap_txs(const RequestReapTxs& request) = 0;

    // --- Consensus Connection -------------------------------------------

    virtual ResponseInitChain init_chain(const RequestInitChain& request) = 0;

    virtual ResponsePrepareProposal prepare_proposal(
        const RequestPrepareProposal& request) = 0;

    virtual ResponseProcessProposal process_proposal(
        const RequestProcessProposal& request) = 0;

    virtual ResponseFinalizeBlock finalize_block(
        const RequestFinalizeBlock& request) = 0;

    virtual ResponseExtendVote extend_vote(
        const RequestExtendVote& request) = 0;

    virtual ResponseVerifyVoteExtension verify_vote_extension(
        const RequestVerifyVoteExtension& request) = 0;

    virtual ResponseCommit commit(const RequestCommit& request) = 0;

    // --- State Sync ------------------------------------------------------

    virtual ResponseListSnapshots list_snapshots(
        const RequestListSnapshots& request) = 0;

    virtual ResponseOfferSnapshot offer_snapshot(
        const RequestOfferSnapshot& request) = 0;

    virtual ResponseLoadSnapshotChunk load_snapshot_chunk(
        const RequestLoadSnapshotChunk& request) = 0;

    virtual ResponseApplySnapshotChunk apply_snapshot_chunk(
        const RequestApplySnapshotChunk& request) = 0;

    // --- Echo ------------------------------------------------------------

    virtual ResponseEcho echo(const RequestEcho& request) = 0;

    // Return a point-in-time snapshot of the complete deterministic state.
    virtual ApplicationSnapshot snapshot() const = 0;

    virtual void restore(const ApplicationSnapshot& snapshot) = 0;

    [[nodiscard]] virtual ChainId chain_id() const noexcept = 0;

    [[nodiscard]] virtual Height height() const noexcept = 0;
};

std::unique_ptr<AbciApplication> make_fst_application(
    const ChainId& chain_id,
    const staking::StakeParams& stake_params,
    const monetary::MonetaryParams& monetary_params);

} // namespace feinstat::consensus::abci
