#include "feinstat/consensus/raft_storage.hpp"

#include <utility>
#include <filesystem>
#include <stdexcept>

namespace {
const std::string& require_existing_database(const std::string& path) {
    if (path != ":memory:" && !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Raft state store requires an existing Ledger database: " + path);
    }
    return path;
}
}

namespace feinstat::consensus {

RaftStateStore::RaftStateStore(const std::string& database_path)
    : store_(require_existing_database(database_path)) {
    store_.initialize_schema();
    const auto record = store_.load_raft_state();
    if (record) revision_ = record->revision;
}

std::optional<RaftPersistentState> RaftStateStore::load() {
    const auto record = store_.load_raft_state();
    if (!record) {
        revision_ = 0;
        return std::nullopt;
    }
    revision_ = record->revision;
    return RaftPersistentState::deserialize(record->payload);
}

void RaftStateStore::save(const RaftPersistentState& state) {
    const Bytes payload = state.serialize();
    revision_ = store_.save_raft_state(payload, revision_);
    if (state.snapshot_size > 0) store_.prune_raft_snapshots_before(state.snapshot_index);
}

void RaftStateStore::save_snapshot(std::uint64_t snapshot_index, const Bytes& payload) {
    store_.save_raft_snapshot(snapshot_index, payload);
}

Bytes RaftStateStore::load_snapshot(std::uint64_t snapshot_index) const {
    const auto payload = store_.load_raft_snapshot(snapshot_index);
    if (!payload) throw std::runtime_error("referenced Raft application snapshot is missing from SQLite");
    return *payload;
}

void RaftStateStore::install_snapshot(
    const RaftPersistentState& state,
    const Bytes& payload,
    ledger::Ledger& ledger,
    const std::vector<ledger::Block>& blocks) {
    if (state.snapshot_index == 0 || state.snapshot_size != payload.size() ||
        state.snapshot_hash != crypto::sha256(payload)) {
        throw std::invalid_argument("Raft snapshot metadata does not match the application payload");
    }
    revision_ = ledger.install_raft_snapshot(
        blocks, state.serialize(), revision_, state.snapshot_index, payload);
}

void RaftStateStore::save_with_ledger_application(
    const RaftPersistentState& state,
    ledger::Ledger& ledger,
    const std::vector<ledger::Block>& blocks) {
    const Bytes payload = state.serialize();
    revision_ = ledger.commit_raft_application(blocks, payload, revision_);
    if (state.snapshot_size > 0) store_.prune_raft_snapshots_before(state.snapshot_index);
}

} // namespace feinstat::consensus
