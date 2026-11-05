#pragma once

#include "feinstat/consensus/raft.hpp"
#include "feinstat/ledger/ledger.hpp"
#include "feinstat/storage/sqlite_store.hpp"

#include <optional>
#include <string>

namespace feinstat::consensus {

// Durable revisioned Raft state backed by the node's existing SQLite database.
// A single checksummed snapshot makes term/vote/log/commit-index persistence atomic.
class RaftStateStore {
public:
    explicit RaftStateStore(const std::string& database_path);

    [[nodiscard]] std::optional<RaftPersistentState> load();
    void save(const RaftPersistentState& state);
    void save_snapshot(std::uint64_t snapshot_index, const Bytes& payload);
    [[nodiscard]] Bytes load_snapshot(std::uint64_t snapshot_index) const;
    [[nodiscard]] std::optional<RaftSnapshotTransfer> load_snapshot_staging() const;
    void append_snapshot_staging_chunk(const RaftSnapshotTransfer& transfer,
                                       std::uint64_t offset,
                                       const Bytes& chunk);
    void clear_snapshot_staging();
    void install_snapshot(const RaftPersistentState& state,
                          const Bytes& payload,
                          ledger::Ledger& ledger,
                          const std::vector<ledger::Block>& blocks);
    void save_with_ledger_application(const RaftPersistentState& state,
                                      ledger::Ledger& ledger,
                                      const std::vector<ledger::Block>& blocks);
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    storage::SqliteStore store_;
    std::uint64_t revision_{0};
};

} // namespace feinstat::consensus
