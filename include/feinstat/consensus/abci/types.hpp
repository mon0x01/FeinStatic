#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/monetary/types.hpp"
#include "feinstat/staking/types.hpp"
#include "feinstat/utxo/mempool.hpp"
#include "feinstat/utxo/types.hpp"
#include "feinstat/utxo/utxo_set.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::consensus::abci {

//------------------------------------------------------------------------------
// FST-native ABCI types. These are the types used by the AbciApplication
// interface. The socket server (abci_socket_server) converts between these
// and the CometBFT protobuf wire format.
//------------------------------------------------------------------------------

using ChainId = utxo::ChainId;
using Height = staking::Height;
using UtxoSet = utxo::UtxoSet;
using Mempool = utxo::Mempool;
using Transaction = utxo::Transaction;
using TransactionId = utxo::TransactionId;
using Input = utxo::Input;
using Output = utxo::Output;
using Outpoint = utxo::Outpoint;

//------------------------------------------------------------------------------
// ABCI request/response kind enums (protobuf oneof discriminators)
//------------------------------------------------------------------------------

enum class RequestKind : std::int32_t {
    Unknown = 0,
    Echo = 1,
    Flush = 2,
    Info = 3,
    InitChain = 5,
    Query = 6,
    CheckTx = 8,
    Commit = 11,
    ListSnapshots = 12,
    OfferSnapshot = 13,
    LoadSnapshotChunk = 14,
    ApplySnapshotChunk = 15,
    PrepareProposal = 16,
    ProcessProposal = 17,
    ExtendVote = 18,
    VerifyVoteExtension = 19,
    FinalizeBlock = 20,
    InsertTx = 21,
    ReapTxs = 22,
};

enum class ResponseKind : std::int32_t {
    Unknown = 0,
    Exception = 1,
    Echo = 2,
    Flush = 3,
    Info = 4,
    InitChain = 6,
    Query = 7,
    CheckTx = 9,
    Commit = 12,
    ListSnapshots = 13,
    OfferSnapshot = 14,
    LoadSnapshotChunk = 15,
    ApplySnapshotChunk = 16,
    PrepareProposal = 17,
    ProcessProposal = 18,
    ExtendVote = 19,
    VerifyVoteExtension = 20,
    FinalizeBlock = 21,
    InsertTx = 22,
    ReapTxs = 23,
};

//------------------------------------------------------------------------------
// Request types (FST-native)
//------------------------------------------------------------------------------

struct RequestEcho {
    std::string message;
};

struct RequestFlush {};

struct RequestInfo {
    std::string version;
    std::uint64_t block_version{0};
    std::uint64_t p2p_version{0};
    std::string abci_version;
};

struct RequestInitChain {
    // Application-specific genesis state bytes. FST encodes genesis UTXO
    // allocations and validator registrations here (opaque to CometBFT).
    Bytes app_state_bytes;
    ChainId chain_id;
    // Initial validator set from genesis.
    struct ValidatorUpdate {
        PublicKey pub_key{};
        std::int64_t power{0};
    };
    std::vector<ValidatorUpdate> validators;
    // Genesis UTXO allocations: spending key -> amount (atoms).
    std::vector<std::pair<PublicKey, Amount>> genesis_allocations;
};

//------------------------------------------------------------------------------
// Response types (FST-native)
//------------------------------------------------------------------------------

struct ResponseEcho {
    std::string message;
};

struct ResponseFlush {};

struct ResponseInfo {
    std::string data;
    std::string version;
    std::uint64_t app_version{1};
    std::int64_t last_block_height{0};
    Hash last_block_app_hash{};
};

struct ResponseInitChain {
    // Validator updates with activation-height semantics (H -> H+2).
    struct ValidatorUpdate {
        PublicKey pub_key{};
        std::int64_t power{0};
    };
    std::vector<ValidatorUpdate> validators;
    Hash app_hash{};
};

//------------------------------------------------------------------------------
// Mempool connection
//------------------------------------------------------------------------------

enum class CheckTxType : std::int32_t {
    New = 0,
    Recheck = 1,
};

struct RequestCheckTx {
    Bytes tx;
    CheckTxType type{CheckTxType::New};
};

struct ResponseCheckTx {
    enum class Code : std::uint32_t {
        ok = 0,
        invalid_transaction = 1,
        double_spend = 2,
        insufficient_funds = 3,
        internal_error = 4,
    };
    Code code{Code::ok};
    Bytes data;
    std::string log;
    std::string info;
    std::int64_t gas_wanted{0};
    std::int64_t gas_used{0};
    struct Event {
        std::string type;
        struct Attribute {
            std::string key;
            std::string value;
            bool index{false};
        };
        std::vector<Attribute> attributes;
    };
    std::vector<Event> events;
    std::string codespace;
};

struct RequestInsertTx {
    Bytes tx;
};

struct ResponseInsertTx {
    std::uint32_t code{0};
};

struct RequestReapTxs {
    std::uint64_t max_bytes{0};
    std::uint64_t max_gas{0};
};

struct ResponseReapTxs {
    std::vector<Bytes> txs;
};

//------------------------------------------------------------------------------
// Consensus connection
//------------------------------------------------------------------------------

struct RequestCommit {};

struct ResponseCommit {
    std::int64_t retain_height{0};
};

struct RequestQuery {
    Bytes data;
    std::string path;
    std::int64_t height{0};
    bool prove{false};
};

struct ResponseQuery {
    std::uint32_t code{0};
    std::string log;
    std::string info;
    std::int64_t index{0};
    Bytes key;
    Bytes value;
    std::int64_t height{0};
    std::string codespace;
};

//------------------------------------------------------------------------------
// Proposal / FinalizeBlock
//------------------------------------------------------------------------------

struct RequestPrepareProposal {
    std::int64_t max_tx_bytes{0};
    std::vector<Bytes> txs; // proposed transactions from mempool
    std::int64_t height{0};
};

struct ResponsePrepareProposal {
    std::vector<Bytes> txs; // transactions to include in block
};

struct RequestProcessProposal {
    std::vector<Bytes> txs;
};

enum class ProposalStatus : std::int32_t {
    Unknown = 0,
    Accept = 1,
    Reject = 2,
};

struct ResponseProcessProposal {
    ProposalStatus status{ProposalStatus::Accept};
};

struct RequestFinalizeBlock {
    std::vector<Bytes> txs; // transactions in the block (in order)
    std::int64_t height{0};
    // Application computes app_hash deterministically over the state after
    // applying all transactions.
    // Validator updates use activation-height semantics (H -> H+2).
};

struct ResponseFinalizeBlock {
    struct Event {
        std::string type;
        struct Attribute {
            std::string key;
            std::string value;
        };
        std::vector<Attribute> attributes;
    };
    struct ExecTxResult {
        std::uint32_t code{0};
        Bytes data;
        std::string log;
        std::string info;
        std::int64_t gas_wanted{0};
        std::int64_t gas_used{0};
        std::vector<Event> events;
        std::string codespace;
    };
    std::vector<Event> events;
    std::vector<ExecTxResult> tx_results; // one per transaction, in order
    struct ValidatorUpdate {
        PublicKey pub_key{};
        std::int64_t power{0};
    };
    std::vector<ValidatorUpdate> validator_updates; // active at H+2
    Hash app_hash{}; // deterministic state commitment
};

//------------------------------------------------------------------------------
// Vote extensions (optional; FST may disable)
//------------------------------------------------------------------------------

struct RequestExtendVote {
    Bytes hash;
};

struct ResponseExtendVote {
    Bytes vote_extension;
};

struct RequestVerifyVoteExtension {
    Bytes hash;
    Bytes extension;
};

enum class VerifyStatus : std::int32_t {
    Unknown = 0,
    Accept = 1,
    Reject = 2,
};

struct ResponseVerifyVoteExtension {
    VerifyStatus status{VerifyStatus::Accept};
};

//------------------------------------------------------------------------------
// State sync (optional; FST may reject)
//------------------------------------------------------------------------------

struct RequestListSnapshots {};

struct ResponseListSnapshots {};

struct RequestOfferSnapshot {};

struct ResponseOfferSnapshot {
    enum class Result : std::int32_t {
        Unknown = 0,
        Accept = 1,
        Reject = 2,
        Abort = 3,
    };
    Result result{Result::Reject};
};

struct RequestLoadSnapshotChunk {};

struct ResponseLoadSnapshotChunk {
    Bytes chunk;
};

struct RequestApplySnapshotChunk {};

struct ResponseApplySnapshotChunk {
    enum class Result : std::int32_t {
        Unknown = 0,
        Accept = 1,
        Retry = 2,
        Abort = 3,
    };
    Result result{Result::Abort};
};

//------------------------------------------------------------------------------
// Top-level Request / Response wrappers (protobuf oneof)
//------------------------------------------------------------------------------

struct Request {
    RequestKind kind{RequestKind::Unknown};
    RequestEcho echo;
    RequestFlush flush;
    RequestInfo info;
    RequestInitChain init_chain;
    RequestQuery query;
    RequestCheckTx check_tx;
    RequestCommit commit;
    RequestListSnapshots list_snapshots;
    RequestOfferSnapshot offer_snapshot;
    RequestLoadSnapshotChunk load_snapshot_chunk;
    RequestApplySnapshotChunk apply_snapshot_chunk;
    RequestPrepareProposal prepare_proposal;
    RequestProcessProposal process_proposal;
    RequestExtendVote extend_vote;
    RequestVerifyVoteExtension verify_vote_extension;
    RequestFinalizeBlock finalize_block;
    RequestInsertTx insert_tx;
    RequestReapTxs reap_txs;
};

struct Response {
    ResponseKind kind{ResponseKind::Unknown};
    std::string exception; // if kind == Exception
    ResponseEcho echo;
    ResponseFlush flush;
    ResponseInfo info;
    ResponseInitChain init_chain;
    ResponseQuery query;
    ResponseCheckTx check_tx;
    ResponseCommit commit;
    ResponseListSnapshots list_snapshots;
    ResponseOfferSnapshot offer_snapshot;
    ResponseLoadSnapshotChunk load_snapshot_chunk;
    ResponseApplySnapshotChunk apply_snapshot_chunk;
    ResponsePrepareProposal prepare_proposal;
    ResponseProcessProposal process_proposal;
    ResponseExtendVote extend_vote;
    ResponseVerifyVoteExtension verify_vote_extension;
    ResponseFinalizeBlock finalize_block;
    ResponseInsertTx insert_tx;
    ResponseReapTxs reap_txs;
};

//------------------------------------------------------------------------------
// Application state snapshot (for crash recovery / state sync)
//------------------------------------------------------------------------------

struct ApplicationSnapshot {
    Hash utxo_state_root{};
    std::vector<ResponseInitChain::ValidatorUpdate> validator_updates{};
    monetary::SupplySnapshot supply_snapshot{};
    Height height{0};
};

} // namespace feinstat::consensus::abci
