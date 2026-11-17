#include "feinstat/consensus/abci/abci_codec.hpp"
#include "feinstat/consensus/abci/types.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace feinstat::consensus::abci {

namespace {

// Convert a PublicKey (32 bytes) to protobuf bytes.
[[maybe_unused]] tendermint::crypto::PublicKey to_protobuf_pubkey(
    const PublicKey& key) {
    tendermint::crypto::PublicKey pk;
    pk.set_ed25519(std::string(reinterpret_cast<const char*>(key.data()),
                               key.size()));
    return pk;
}

// Convert protobuf PublicKey to FST-native PublicKey.
[[maybe_unused]] PublicKey from_protobuf_pubkey(
    const tendermint::crypto::PublicKey& pk) {
    PublicKey key{};
    const std::string& value = pk.ed25519();
    if (value.size() != key.size()) {
        throw std::runtime_error("invalid public key size");
    }
    std::memcpy(key.data(), value.data(), key.size());
    return key;
}

} // namespace

//------------------------------------------------------------------------------
// Request conversion
//------------------------------------------------------------------------------

tendermint::abci::Request to_protobuf(const Request& req) {
    tendermint::abci::Request proto;

    switch (req.kind) {
        case RequestKind::Echo: {
            auto* echo = proto.mutable_echo();
            echo->set_message(req.echo.message);
            break;
        }
        case RequestKind::Flush:
            proto.mutable_flush();
            break;
        case RequestKind::Info: {
            auto* info = proto.mutable_info();
            info->set_version(req.info.version);
            info->set_block_version(req.info.block_version);
            info->set_p2p_version(req.info.p2p_version);
            info->set_abci_version(req.info.abci_version);
            break;
        }
        case RequestKind::InitChain: {
            auto* ic = proto.mutable_init_chain();
            ic->set_chain_id(std::string(req.init_chain.chain_id.begin(),
                                         req.init_chain.chain_id.end()));
            ic->mutable_app_state_bytes()->assign(
                req.init_chain.app_state_bytes.begin(),
                req.init_chain.app_state_bytes.end());
            for (const auto& vu : req.init_chain.validators) {
                auto* v = ic->add_validators();
                *v->mutable_pub_key() = to_protobuf_pubkey(vu.pub_key);
                v->set_power(vu.power);
            }
            break;
        }
        case RequestKind::Query: {
            auto* q = proto.mutable_query();
            q->mutable_data()->assign(req.query.data.begin(),
                                      req.query.data.end());
            q->set_path(req.query.path);
            q->set_height(req.query.height);
            q->set_prove(req.query.prove);
            break;
        }
        case RequestKind::CheckTx: {
            auto* ct = proto.mutable_check_tx();
            ct->mutable_tx()->assign(req.check_tx.tx.begin(),
                                     req.check_tx.tx.end());
            ct->set_type(static_cast<tendermint::abci::CheckTxType>(
                static_cast<int>(req.check_tx.type)));
            break;
        }
        case RequestKind::Commit:
            proto.mutable_commit();
            break;
        case RequestKind::ListSnapshots:
            proto.mutable_list_snapshots();
            break;
        case RequestKind::OfferSnapshot:
            break;
        case RequestKind::LoadSnapshotChunk:
            break;
        case RequestKind::ApplySnapshotChunk:
            break;
        case RequestKind::PrepareProposal: {
            auto* pp = proto.mutable_prepare_proposal();
            pp->set_max_tx_bytes(req.prepare_proposal.max_tx_bytes);
            for (const auto& tx : req.prepare_proposal.txs) {
                pp->add_txs(tx.data(), tx.size());
            }
            pp->set_height(req.prepare_proposal.height);
            break;
        }
        case RequestKind::ProcessProposal: {
            auto* pp = proto.mutable_process_proposal();
            for (const auto& tx : req.process_proposal.txs) {
                pp->add_txs(tx.data(), tx.size());
            }
            break;
        }
        case RequestKind::ExtendVote: {
            auto* ev = proto.mutable_extend_vote();
            ev->mutable_hash()->assign(req.extend_vote.hash.begin(),
                                       req.extend_vote.hash.end());
            break;
        }
        case RequestKind::VerifyVoteExtension: {
            auto* vve = proto.mutable_verify_vote_extension();
            vve->mutable_hash()->assign(req.verify_vote_extension.hash.begin(),
                                        req.verify_vote_extension.hash.end());
            vve->mutable_vote_extension()->assign(
                req.verify_vote_extension.extension.begin(),
                req.verify_vote_extension.extension.end());
            break;
        }
        case RequestKind::FinalizeBlock: {
            auto* fb = proto.mutable_finalize_block();
            for (const auto& tx : req.finalize_block.txs) {
                fb->add_txs(tx.data(), tx.size());
            }
            fb->set_height(req.finalize_block.height);
            break;
        }
        case RequestKind::InsertTx: {
            auto* itx = proto.mutable_insert_tx();
            itx->mutable_tx()->assign(req.insert_tx.tx.begin(),
                                      req.insert_tx.tx.end());
            break;
        }
        case RequestKind::ReapTxs: {
            auto* rt = proto.mutable_reap_txs();
            rt->set_max_bytes(req.reap_txs.max_bytes);
            rt->set_max_gas(req.reap_txs.max_gas);
            break;
        }
        case RequestKind::Unknown:
            break;
    }

    return proto;
}

Request from_protobuf(const tendermint::abci::Request& proto) {
    Request req;

    switch (proto.value_case()) {
        case tendermint::abci::Request::kEcho:
            req.kind = RequestKind::Echo;
            req.echo.message = proto.echo().message();
            break;
        case tendermint::abci::Request::kFlush:
            req.kind = RequestKind::Flush;
            break;
        case tendermint::abci::Request::kInfo:
            req.kind = RequestKind::Info;
            req.info.version = proto.info().version();
            req.info.block_version = proto.info().block_version();
            req.info.p2p_version = proto.info().p2p_version();
            req.info.abci_version = proto.info().abci_version();
            break;
        case tendermint::abci::Request::kInitChain:
            req.kind = RequestKind::InitChain;
            std::copy(proto.init_chain().chain_id().begin(),
                      proto.init_chain().chain_id().end(),
                      req.init_chain.chain_id.begin());
            req.init_chain.app_state_bytes.assign(
                proto.init_chain().app_state_bytes().begin(),
                proto.init_chain().app_state_bytes().end());
            for (const auto& v : proto.init_chain().validators()) {
                RequestInitChain::ValidatorUpdate vu;
                vu.pub_key = from_protobuf_pubkey(v.pub_key());
                vu.power = v.power();
                req.init_chain.validators.push_back(vu);
            }
            break;
        case tendermint::abci::Request::kQuery:
            req.kind = RequestKind::Query;
            req.query.data.assign(proto.query().data().begin(),
                                  proto.query().data().end());
            req.query.path = proto.query().path();
            req.query.height = proto.query().height();
            req.query.prove = proto.query().prove();
            break;
        case tendermint::abci::Request::kCheckTx:
            req.kind = RequestKind::CheckTx;
            req.check_tx.tx.assign(proto.check_tx().tx().begin(),
                                   proto.check_tx().tx().end());
            req.check_tx.type =
                static_cast<CheckTxType>(proto.check_tx().type());
            break;
        case tendermint::abci::Request::kCommit:
            req.kind = RequestKind::Commit;
            break;
        case tendermint::abci::Request::kListSnapshots:
            req.kind = RequestKind::ListSnapshots;
            break;
        case tendermint::abci::Request::kOfferSnapshot:
            req.kind = RequestKind::OfferSnapshot;
            break;
        case tendermint::abci::Request::kLoadSnapshotChunk:
            req.kind = RequestKind::LoadSnapshotChunk;
            break;
        case tendermint::abci::Request::kApplySnapshotChunk:
            req.kind = RequestKind::ApplySnapshotChunk;
            break;
        case tendermint::abci::Request::kPrepareProposal:
            req.kind = RequestKind::PrepareProposal;
            req.prepare_proposal.max_tx_bytes =
                proto.prepare_proposal().max_tx_bytes();
            for (const auto& tx : proto.prepare_proposal().txs()) {
                req.prepare_proposal.txs.emplace_back(tx.begin(), tx.end());
            }
            req.prepare_proposal.height =
                proto.prepare_proposal().height();
            break;
        case tendermint::abci::Request::kProcessProposal:
            req.kind = RequestKind::ProcessProposal;
            for (const auto& tx : proto.process_proposal().txs()) {
                req.process_proposal.txs.emplace_back(tx.begin(), tx.end());
            }
            break;
        case tendermint::abci::Request::kExtendVote:
            req.kind = RequestKind::ExtendVote;
            req.extend_vote.hash.assign(proto.extend_vote().hash().begin(),
                                        proto.extend_vote().hash().end());
            break;
        case tendermint::abci::Request::kVerifyVoteExtension:
            req.kind = RequestKind::VerifyVoteExtension;
            req.verify_vote_extension.hash.assign(
                proto.verify_vote_extension().hash().begin(),
                proto.verify_vote_extension().hash().end());
            req.verify_vote_extension.extension.assign(
                proto.verify_vote_extension().vote_extension().begin(),
                proto.verify_vote_extension().vote_extension().end());
            break;
        case tendermint::abci::Request::kFinalizeBlock:
            req.kind = RequestKind::FinalizeBlock;
            for (const auto& tx : proto.finalize_block().txs()) {
                req.finalize_block.txs.emplace_back(tx.begin(), tx.end());
            }
            req.finalize_block.height = proto.finalize_block().height();
            break;
        case tendermint::abci::Request::kInsertTx:
            req.kind = RequestKind::InsertTx;
            req.insert_tx.tx.assign(proto.insert_tx().tx().begin(),
                                    proto.insert_tx().tx().end());
            break;
        case tendermint::abci::Request::kReapTxs:
            req.kind = RequestKind::ReapTxs;
            req.reap_txs.max_bytes = proto.reap_txs().max_bytes();
            req.reap_txs.max_gas = proto.reap_txs().max_gas();
            break;
        case tendermint::abci::Request::VALUE_NOT_SET:
            req.kind = RequestKind::Unknown;
            break;
    }

    return req;
}

//------------------------------------------------------------------------------
// Response conversion
//------------------------------------------------------------------------------

tendermint::abci::Response to_protobuf(const Response& resp) {
    tendermint::abci::Response proto;

    switch (resp.kind) {
        case ResponseKind::Exception:
            proto.mutable_exception()->set_error(resp.exception);
            break;
        case ResponseKind::Echo:
            proto.mutable_echo()->set_message(resp.echo.message);
            break;
        case ResponseKind::Flush:
            proto.mutable_flush();
            break;
        case ResponseKind::Info: {
            auto* info = proto.mutable_info();
            info->set_data(resp.info.data);
            info->set_version(resp.info.version);
            info->set_app_version(resp.info.app_version);
            info->set_last_block_height(resp.info.last_block_height);
            info->mutable_last_block_app_hash()->assign(
                resp.info.last_block_app_hash.begin(),
                resp.info.last_block_app_hash.end());
            break;
        }
        case ResponseKind::InitChain: {
            auto* ic = proto.mutable_init_chain();
            for (const auto& vu : resp.init_chain.validators) {
                auto* v = ic->add_validators();
                *v->mutable_pub_key() = to_protobuf_pubkey(vu.pub_key);
                v->set_power(vu.power);
            }
            ic->mutable_app_hash()->assign(
                resp.init_chain.app_hash.begin(),
                resp.init_chain.app_hash.end());
            break;
        }
        case ResponseKind::Query: {
            auto* q = proto.mutable_query();
            q->set_code(resp.query.code);
            q->mutable_key()->assign(resp.query.key.begin(),
                                     resp.query.key.end());
            q->mutable_value()->assign(resp.query.value.begin(),
                                       resp.query.value.end());
            break;
        }
        case ResponseKind::CheckTx: {
            auto* ct = proto.mutable_check_tx();
            ct->set_code(static_cast<std::uint32_t>(resp.check_tx.code));
            ct->mutable_data()->assign(resp.check_tx.data.begin(),
                                       resp.check_tx.data.end());
            ct->set_log(resp.check_tx.log);
            ct->set_info(resp.check_tx.info);
            ct->set_gas_wanted(resp.check_tx.gas_wanted);
            ct->set_gas_used(resp.check_tx.gas_used);
            break;
        }
        case ResponseKind::Commit:
            proto.mutable_commit()->set_retain_height(
                resp.commit.retain_height);
            break;
        case ResponseKind::ListSnapshots:
            proto.mutable_list_snapshots();
            break;
        case ResponseKind::OfferSnapshot:
            proto.mutable_offer_snapshot()->set_result(
                static_cast<tendermint::abci::ResponseOfferSnapshot::Result>(
                    static_cast<int>(resp.offer_snapshot.result)));
            break;
        case ResponseKind::LoadSnapshotChunk: {
            auto* lsc = proto.mutable_load_snapshot_chunk();
            lsc->mutable_chunk()->assign(
                resp.load_snapshot_chunk.chunk.begin(),
                resp.load_snapshot_chunk.chunk.end());
            break;
        }
        case ResponseKind::ApplySnapshotChunk:
            proto.mutable_apply_snapshot_chunk()->set_result(
                static_cast<tendermint::abci::ResponseApplySnapshotChunk::Result>(
                    static_cast<int>(resp.apply_snapshot_chunk.result)));
            break;
        case ResponseKind::PrepareProposal: {
            auto* pp = proto.mutable_prepare_proposal();
            for (const auto& tx : resp.prepare_proposal.txs) {
                pp->add_txs(tx.data(), tx.size());
            }
            break;
        }
        case ResponseKind::ProcessProposal:
            proto.mutable_process_proposal()->set_status(
                static_cast<tendermint::abci::ResponseProcessProposal::ProposalStatus>(
                    static_cast<int>(resp.process_proposal.status)));
            break;
        case ResponseKind::ExtendVote: {
            auto* ev = proto.mutable_extend_vote();
            ev->mutable_vote_extension()->assign(
                resp.extend_vote.vote_extension.begin(),
                resp.extend_vote.vote_extension.end());
            break;
        }
        case ResponseKind::VerifyVoteExtension:
            proto.mutable_verify_vote_extension()->set_status(
                static_cast<tendermint::abci::ResponseVerifyVoteExtension::VerifyStatus>(
                    static_cast<int>(resp.verify_vote_extension.status)));
            break;
        case ResponseKind::FinalizeBlock: {
            auto* fb = proto.mutable_finalize_block();
            for (const auto& ev : resp.finalize_block.events) {
                auto* e = fb->add_events();
                e->set_type(ev.type);
                for (const auto& attr : ev.attributes) {
                    auto* a = e->add_attributes();
                    a->set_key(attr.key);
                    a->set_value(attr.value);
                }
            }
            for (const auto& txr : resp.finalize_block.tx_results) {
                auto* r = fb->add_tx_results();
                r->set_code(txr.code);
                r->mutable_data()->assign(txr.data.begin(), txr.data.end());
                r->set_log(txr.log);
                r->set_info(txr.info);
                r->set_gas_wanted(txr.gas_wanted);
                r->set_gas_used(txr.gas_used);
            }
            for (const auto& vu : resp.finalize_block.validator_updates) {
                auto* v = fb->add_validator_updates();
                *v->mutable_pub_key() = to_protobuf_pubkey(vu.pub_key);
                v->set_power(vu.power);
            }
            fb->mutable_app_hash()->assign(
                resp.finalize_block.app_hash.begin(),
                resp.finalize_block.app_hash.end());
            break;
        }
        case ResponseKind::InsertTx:
            proto.mutable_insert_tx()->set_code(resp.insert_tx.code);
            break;
        case ResponseKind::ReapTxs: {
            auto* rt = proto.mutable_reap_txs();
            for (const auto& tx : resp.reap_txs.txs) {
                rt->add_txs(tx.data(), tx.size());
            }
            break;
        }
        case ResponseKind::Unknown:
            break;
    }

    return proto;
}

Response from_protobuf(const tendermint::abci::Response& proto) {
    Response resp;

    switch (proto.value_case()) {
        case tendermint::abci::Response::kException:
            resp.kind = ResponseKind::Exception;
            resp.exception = proto.exception().error();
            break;
        case tendermint::abci::Response::kEcho:
            resp.kind = ResponseKind::Echo;
            resp.echo.message = proto.echo().message();
            break;
        case tendermint::abci::Response::kFlush:
            resp.kind = ResponseKind::Flush;
            break;
        case tendermint::abci::Response::kInfo:
            resp.kind = ResponseKind::Info;
            resp.info.data = proto.info().data();
            resp.info.version = proto.info().version();
            resp.info.app_version = proto.info().app_version();
            resp.info.last_block_height = proto.info().last_block_height();
            std::copy(proto.info().last_block_app_hash().begin(),
                      proto.info().last_block_app_hash().end(),
                      resp.info.last_block_app_hash.begin());
            break;
        case tendermint::abci::Response::kInitChain:
            resp.kind = ResponseKind::InitChain;
            for (const auto& v : proto.init_chain().validators()) {
                ResponseInitChain::ValidatorUpdate vu;
                vu.pub_key = from_protobuf_pubkey(v.pub_key());
                vu.power = v.power();
                resp.init_chain.validators.push_back(vu);
            }
            std::copy(proto.init_chain().app_hash().begin(),
                      proto.init_chain().app_hash().end(),
                      resp.init_chain.app_hash.begin());
            break;
        case tendermint::abci::Response::kQuery:
            resp.kind = ResponseKind::Query;
            resp.query.code = proto.query().code();
            resp.query.key.assign(proto.query().key().begin(),
                                  proto.query().key().end());
            resp.query.value.assign(proto.query().value().begin(),
                                    proto.query().value().end());
            break;
        case tendermint::abci::Response::kCheckTx:
            resp.kind = ResponseKind::CheckTx;
            resp.check_tx.code = static_cast<ResponseCheckTx::Code>(
                proto.check_tx().code());
            resp.check_tx.data.assign(proto.check_tx().data().begin(),
                                      proto.check_tx().data().end());
            resp.check_tx.log = proto.check_tx().log();
            resp.check_tx.info = proto.check_tx().info();
            resp.check_tx.gas_wanted = proto.check_tx().gas_wanted();
            resp.check_tx.gas_used = proto.check_tx().gas_used();
            break;
        case tendermint::abci::Response::kCommit:
            resp.kind = ResponseKind::Commit;
            resp.commit.retain_height = proto.commit().retain_height();
            break;
        case tendermint::abci::Response::kListSnapshots:
            resp.kind = ResponseKind::ListSnapshots;
            break;
        case tendermint::abci::Response::kOfferSnapshot:
            resp.kind = ResponseKind::OfferSnapshot;
            resp.offer_snapshot.result =
                static_cast<ResponseOfferSnapshot::Result>(
                    proto.offer_snapshot().result());
            break;
        case tendermint::abci::Response::kLoadSnapshotChunk:
            resp.kind = ResponseKind::LoadSnapshotChunk;
            resp.load_snapshot_chunk.chunk.assign(
                proto.load_snapshot_chunk().chunk().begin(),
                proto.load_snapshot_chunk().chunk().end());
            break;
        case tendermint::abci::Response::kApplySnapshotChunk:
            resp.kind = ResponseKind::ApplySnapshotChunk;
            resp.apply_snapshot_chunk.result =
                static_cast<ResponseApplySnapshotChunk::Result>(
                    proto.apply_snapshot_chunk().result());
            break;
        case tendermint::abci::Response::kPrepareProposal:
            resp.kind = ResponseKind::PrepareProposal;
            for (const auto& tx : proto.prepare_proposal().txs()) {
                resp.prepare_proposal.txs.emplace_back(tx.begin(), tx.end());
            }
            break;
        case tendermint::abci::Response::kProcessProposal:
            resp.kind = ResponseKind::ProcessProposal;
            resp.process_proposal.status =
                static_cast<ProposalStatus>(
                    proto.process_proposal().status());
            break;
        case tendermint::abci::Response::kExtendVote:
            resp.kind = ResponseKind::ExtendVote;
            resp.extend_vote.vote_extension.assign(
                proto.extend_vote().vote_extension().begin(),
                proto.extend_vote().vote_extension().end());
            break;
        case tendermint::abci::Response::kVerifyVoteExtension:
            resp.kind = ResponseKind::VerifyVoteExtension;
            resp.verify_vote_extension.status =
                static_cast<VerifyStatus>(
                    proto.verify_vote_extension().status());
            break;
        case tendermint::abci::Response::kFinalizeBlock:
            resp.kind = ResponseKind::FinalizeBlock;
            for (const auto& e : proto.finalize_block().events()) {
                ResponseFinalizeBlock::Event ev;
                ev.type = e.type();
                for (const auto& a : e.attributes()) {
                    ResponseFinalizeBlock::Event::Attribute attr;
                    attr.key = a.key();
                    attr.value = a.value();
                    ev.attributes.push_back(attr);
                }
                resp.finalize_block.events.push_back(ev);
            }
            for (const auto& r : proto.finalize_block().tx_results()) {
                ResponseFinalizeBlock::ExecTxResult txr;
                txr.code = r.code();
                txr.data.assign(r.data().begin(), r.data().end());
                txr.log = r.log();
                txr.info = r.info();
                txr.gas_wanted = r.gas_wanted();
                txr.gas_used = r.gas_used();
                resp.finalize_block.tx_results.push_back(txr);
            }
            for (const auto& v : proto.finalize_block().validator_updates()) {
                ResponseFinalizeBlock::ValidatorUpdate vu;
                vu.pub_key = from_protobuf_pubkey(v.pub_key());
                vu.power = v.power();
                resp.finalize_block.validator_updates.push_back(vu);
            }
            std::copy(proto.finalize_block().app_hash().begin(),
                      proto.finalize_block().app_hash().end(),
                      resp.finalize_block.app_hash.begin());
            break;
        case tendermint::abci::Response::kInsertTx:
            resp.kind = ResponseKind::InsertTx;
            resp.insert_tx.code = proto.insert_tx().code();
            break;
        case tendermint::abci::Response::kReapTxs:
            resp.kind = ResponseKind::ReapTxs;
            for (const auto& tx : proto.reap_txs().txs()) {
                resp.reap_txs.txs.emplace_back(tx.begin(), tx.end());
            }
            break;
        case tendermint::abci::Response::VALUE_NOT_SET:
            resp.kind = ResponseKind::Unknown;
            break;
    }

    return resp;
}

//------------------------------------------------------------------------------
// Framing
//------------------------------------------------------------------------------

std::vector<std::uint8_t> encode_varint(std::uint64_t value) {
    std::vector<std::uint8_t> out;
    while (value >= 0x80ULL) {
        out.push_back(static_cast<std::uint8_t>((value & 0x7fULL) | 0x80ULL));
        value >>= 7U;
    }
    out.push_back(static_cast<std::uint8_t>(value & 0x7fULL));
    return out;
}

std::uint64_t decode_varint(const std::vector<std::uint8_t>& data,
                            std::size_t& pos) {
    std::uint64_t result = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (pos >= data.size()) {
            throw std::runtime_error("varint truncated");
        }
        std::uint8_t b = data[pos++];
        result |= static_cast<std::uint64_t>(b & 0x7fULL) << shift;
        if ((b & 0x80ULL) == 0) {
            return result;
        }
        shift += 7;
    }
    throw std::runtime_error("varint exceeds 64 bits");
}

void read_exact(int fd, void* buf, std::size_t n) {
    std::uint8_t* p = static_cast<std::uint8_t*>(buf);
    std::size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, p + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("read failed");
        }
        if (r == 0) {
            throw std::runtime_error("unexpected EOF");
        }
        total += static_cast<std::size_t>(r);
    }
}

void write_exact(int fd, const void* buf, std::size_t n) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(buf);
    std::size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, p + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write failed");
        }
        total += static_cast<std::size_t>(w);
    }
}

template <typename Message>
Message read_framed_message(int fd, std::size_t max_message_size) {
    // Read varint length
    std::uint8_t len_byte;
    std::vector<std::uint8_t> len_bytes;
    for (int i = 0; i < 10; ++i) {
        read_exact(fd, &len_byte, 1);
        len_bytes.push_back(len_byte);
        if ((len_byte & 0x80) == 0) break;
    }
    std::size_t pos = 0;
    std::uint64_t length = decode_varint(len_bytes, pos);
    if (length == 0 || length > max_message_size) {
        throw std::runtime_error("invalid framed message length");
    }

    // Read message bytes
    std::vector<std::uint8_t> msg_bytes(length);
    read_exact(fd, msg_bytes.data(), length);

    Message msg;
    if (!msg.ParseFromArray(msg_bytes.data(),
                            static_cast<int>(msg_bytes.size()))) {
        throw std::runtime_error("failed to parse protobuf message");
    }
    return msg;
}

template <typename Message>
void write_framed_message(int fd, const Message& msg) {
    std::string serialized;
    bool ok = msg.SerializeToString(&serialized);
    if (!ok) {
        throw std::runtime_error("failed to serialize protobuf message");
    }
    std::vector<std::uint8_t> len_bytes = encode_varint(serialized.size());

    write_exact(fd, len_bytes.data(), len_bytes.size());
    write_exact(fd, serialized.data(), serialized.size());
}

// Explicit template instantiations
template tendermint::abci::Request read_framed_message<tendermint::abci::Request>(
    int fd, std::size_t max_message_size);
template tendermint::abci::Response read_framed_message<tendermint::abci::Response>(
    int fd, std::size_t max_message_size);
template void write_framed_message<tendermint::abci::Request>(
    int fd, const tendermint::abci::Request& msg);
template void write_framed_message<tendermint::abci::Response>(
    int fd, const tendermint::abci::Response& msg);

} // namespace feinstat::consensus::abci
