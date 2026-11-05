#include "feinstat/consensus/raft_ledger_adapter.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace feinstat::consensus {
namespace {

constexpr std::array<Byte, 4> kBlockCommandMagic{'F', 'S', 'B', 'C'};
constexpr Byte kBlockCommandVersion = 1;
constexpr std::array<Byte, 4> kApplicationSnapshotMagic{'F', 'S', 'L', 'S'};
constexpr Byte kApplicationSnapshotVersion = 1;

} // namespace

RaftLedgerAdapter::RaftLedgerAdapter(
    ledger::Ledger& ledger,
    const std::string& database_path,
    RaftConfig config)
    : ledger_(ledger), snapshot_threshold_entries_(config.snapshot_threshold_entries), store_(database_path) {
    if (snapshot_threshold_entries_ == 0 || snapshot_threshold_entries_ > 1'000'000U) {
        throw std::invalid_argument("Raft snapshot threshold must be between 1 and 1,000,000 applied entries");
    }
    auto recovered = store_.load();
    if (recovered) {
        config.checkpoint_height = recovered->checkpoint_height;
        config.checkpoint_hash = recovered->checkpoint_hash;
    } else {
        config.checkpoint_height = ledger_.chain().height();
        config.checkpoint_hash = ledger_.chain().blocks().back().hash;
    }

    node_ = std::make_unique<RaftNode>(
        std::move(config), std::move(recovered),
        [this](const RaftPersistentState& state) { store_.save(state); },
        [this](std::uint64_t index) { return store_.load_snapshot(index); },
        [this](std::uint64_t index, const Bytes& payload) { store_.save_snapshot(index, payload); },
        [this](const RaftPersistentState& state, const Bytes& payload) {
            const auto [index, blocks] = decode_application_snapshot(payload);
            if (index != state.snapshot_index) {
                throw std::runtime_error("received Ledger snapshot index does not match Raft metadata");
            }
            store_.install_snapshot(state, payload, ledger_, blocks);
        },
        [this] { return store_.load_snapshot_staging(); },
        [this](const RaftSnapshotTransfer& transfer, std::uint64_t offset, const Bytes& chunk) {
            store_.append_snapshot_staging_chunk(transfer, offset, chunk);
        },
        [this] { store_.clear_snapshot_staging(); },
        [this](std::uint64_t index, std::uint64_t offset, std::size_t maximum) {
            return store_.read_snapshot_chunk(index, offset, maximum);
        },
        [this](std::uint64_t offset, std::size_t maximum) {
            return store_.read_snapshot_staging_chunk(offset, maximum);
        });
    validate_recovered_ledger();
    (void)apply_committed();
}

Bytes RaftLedgerAdapter::encode_block_command(const ledger::Block& block) {
    const Bytes encoded_block = block.serialize();
    if (encoded_block.empty() || encoded_block.size() > kRaftMaximumCommandBytes - 9U) {
        throw std::length_error("Ledger block exceeds the Raft application-command size limit");
    }
    ByteWriter writer;
    writer.fixed(kBlockCommandMagic);
    writer.u8(kBlockCommandVersion);
    writer.blob(encoded_block);
    return std::move(writer).take();
}

ledger::Block RaftLedgerAdapter::decode_block_command(const Bytes& command) {
    if (command.empty() || command.size() > kRaftMaximumCommandBytes) {
        throw std::length_error("Raft Ledger command is empty or exceeds its size limit");
    }
    ByteReader reader(command);
    if (reader.fixed<4>() != kBlockCommandMagic || reader.u8() != kBlockCommandVersion) {
        throw std::runtime_error("unknown or unsupported Raft Ledger command encoding");
    }
    const Bytes encoded_block = reader.blob(kRaftMaximumCommandBytes - 9U);
    if (encoded_block.empty()) throw std::runtime_error("Raft Ledger block command has an empty block payload");
    if (!reader.empty()) throw std::runtime_error("trailing bytes in Raft Ledger block command");
    const auto block = ledger::Block::deserialize(encoded_block);
    if (block.serialize() != encoded_block) {
        throw std::runtime_error("Raft Ledger block command is not canonically encoded");
    }
    return block;
}

void RaftLedgerAdapter::validate_recovered_ledger() const {
    const auto& state = node_->persistent_state();
    const auto& blocks = ledger_.chain().blocks();
    if (state.checkpoint_height >= blocks.size()) {
        throw std::runtime_error("Ledger is behind the persisted Raft checkpoint");
    }
    if (blocks[static_cast<std::size_t>(state.checkpoint_height)].hash != state.checkpoint_hash) {
        throw std::runtime_error("Ledger history does not contain the persisted Raft checkpoint");
    }

    std::uint64_t expected_ledger_height = state.checkpoint_height;
    if (state.snapshot_size > 0) {
        const Bytes payload = store_.load_snapshot(state.snapshot_index);
        if (payload.size() != state.snapshot_size || crypto::sha256(payload) != state.snapshot_hash) {
            throw std::runtime_error("persisted Raft snapshot metadata does not match its durable payload");
        }
        const auto [snapshot_index, snapshot_blocks] = decode_application_snapshot(payload);
        if (snapshot_index != state.snapshot_index || snapshot_blocks.empty() ||
            snapshot_blocks.size() > blocks.size()) {
            throw std::runtime_error("persisted Raft application snapshot has an inconsistent Ledger prefix");
        }
        for (std::size_t i = 0; i < snapshot_blocks.size(); ++i) {
            if (snapshot_blocks[i].serialize() != blocks[i].serialize()) {
                throw std::runtime_error("persisted Raft application snapshot does not match the recovered Ledger");
            }
        }
        expected_ledger_height = static_cast<std::uint64_t>(snapshot_blocks.size() - 1U);
    }

    for (const auto& entry : state.log) {
        if (entry.index > state.last_applied) break;
        if (entry.command.empty() || RaftNode::is_membership_command(entry.command)) continue;
        const ledger::Block block = decode_block_command(entry.command);
        if (expected_ledger_height == UINT64_MAX) {
            throw std::overflow_error("Ledger height exhausted while validating recovered Raft application history");
        }
        ++expected_ledger_height;
        if (block.height != expected_ledger_height || expected_ledger_height >= blocks.size() ||
            block.serialize() != blocks[static_cast<std::size_t>(expected_ledger_height)].serialize()) {
            throw std::runtime_error("persisted Raft applied history does not match the recovered Ledger chain");
        }
    }
    if (expected_ledger_height != ledger_.chain().height()) {
        throw std::runtime_error("Ledger contains blocks not accounted for by the persisted Raft applied index");
    }
}

Bytes RaftLedgerAdapter::encode_application_snapshot(std::uint64_t index) const {
    const auto& blocks = ledger_.chain().blocks();
    if (blocks.empty() || blocks.size() > UINT32_MAX) {
        throw std::length_error("Ledger chain cannot be represented in a Raft application snapshot");
    }
    ByteWriter writer;
    writer.fixed(kApplicationSnapshotMagic);
    writer.u8(kApplicationSnapshotVersion);
    writer.u64(index);
    writer.u32(static_cast<std::uint32_t>(blocks.size()));
    for (const auto& block : blocks) {
        const Bytes payload = block.serialize();
        writer.blob(payload);
        if (writer.data().size() > kRaftMaximumSnapshotBytes) {
            throw std::length_error("Ledger application snapshot exceeds the 64 MiB transfer limit");
        }
    }
    return std::move(writer).take();
}

std::pair<std::uint64_t, std::vector<ledger::Block>>
RaftLedgerAdapter::decode_application_snapshot(const Bytes& payload) {
    if (payload.empty() || payload.size() > kRaftMaximumSnapshotBytes) {
        throw std::length_error("Ledger application snapshot is empty or exceeds 64 MiB");
    }
    ByteReader reader(payload);
    if (reader.fixed<4>() != kApplicationSnapshotMagic || reader.u8() != kApplicationSnapshotVersion) {
        throw std::runtime_error("unsupported Ledger application snapshot encoding");
    }
    const std::uint64_t index = reader.u64();
    const std::uint32_t count = reader.u32();
    if (index == 0 || count == 0 || count > 1'000'000U) {
        throw std::runtime_error("Ledger application snapshot has invalid index or block count");
    }
    std::vector<ledger::Block> blocks;
    blocks.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const Bytes block_payload = reader.blob(kRaftMaximumCommandBytes);
        blocks.push_back(ledger::Block::deserialize(block_payload));
    }
    if (!reader.empty()) throw std::runtime_error("trailing bytes in Ledger application snapshot");
    return {index, std::move(blocks)};
}

std::vector<RaftMessage> RaftLedgerAdapter::tick(std::chrono::milliseconds elapsed) {
    auto outbound = node_->tick(elapsed);
    (void)apply_committed();
    return outbound;
}

std::vector<RaftMessage> RaftLedgerAdapter::receive(
    const RaftMessage& message,
    const std::string& authenticated_peer_id) {
    if (authenticated_peer_id.empty() || message.from != authenticated_peer_id) {
        throw std::runtime_error("Raft message sender does not match the authenticated peer identity");
    }
    auto outbound = node_->receive(message);
    (void)apply_committed();
    return outbound;
}

RaftBlockProposal RaftLedgerAdapter::propose_next_block(const Address& miner) {
    if (node_->role() != RaftRole::Leader || !node_->leader_ready()) {
        throw std::logic_error("only a ready Raft leader can propose a Ledger block");
    }
    for (const auto& entry : node_->persistent_state().log) {
        if (entry.index > node_->persistent_state().last_applied && !entry.command.empty()) {
            throw std::logic_error("a previous Ledger block proposal is not yet applied");
        }
    }

    const ledger::Block block = ledger_.chain().make_next_block(
        ledger_.mempool().transactions(), miner);
    std::string validation_error;
    if (!ledger_.chain().can_append(block, &validation_error)) {
        throw std::runtime_error("locally constructed Ledger block failed validation: " + validation_error);
    }

    const Bytes command = encode_block_command(block);
    auto outbound = node_->propose(command);
    const std::uint64_t index = node_->last_log_index();
    return RaftBlockProposal{index, node_->persistent_state().current_term, block.hash,
                             std::move(outbound)};
}

std::vector<RaftMessage> RaftLedgerAdapter::propose_membership_change(
    const std::vector<std::string>& new_members) {
    return node_->propose_membership_change(new_members);
}

RaftApplicationResult RaftLedgerAdapter::apply_committed() {
    RaftApplicationResult result;
    const auto entries = node_->committed_entries();
    for (const auto& entry : entries) {
        const bool contains_block = !entry.command.empty() && !RaftNode::is_membership_command(entry.command);
        node_->mark_applied_atomically(entry.index,
            [this, &entry](const RaftPersistentState& candidate_state) {
                std::vector<ledger::Block> blocks;
                if (!entry.command.empty() && !RaftNode::is_membership_command(entry.command)) {
                    blocks.push_back(decode_block_command(entry.command));
                }
                store_.save_with_ledger_application(candidate_state, ledger_, blocks);
            });
        ++result.entries_applied;
        if (contains_block) ++result.blocks_applied;
    }
    const auto& state = node_->persistent_state();
    if (state.last_applied > state.snapshot_index &&
        state.last_applied - state.snapshot_index >= snapshot_threshold_entries_) {
        (void)compact_applied();
    }
    return result;
}

bool RaftLedgerAdapter::compact_applied() {
    const auto& state = node_->persistent_state();
    if (state.last_applied <= state.snapshot_index) return false;
    const Bytes snapshot = encode_application_snapshot(state.last_applied);
    return node_->compact_applied(snapshot);
}

} // namespace feinstat::consensus
