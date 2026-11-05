#pragma once

#include "feinstat/consensus/raft.hpp"
#include "feinstat/consensus/raft_storage.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <chrono>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace feinstat::consensus {

struct RaftBlockProposal {
    std::uint64_t index{0};
    std::uint64_t term{0};
    Hash block_hash{};
    std::vector<RaftMessage> outbound;
};

struct RaftApplicationResult {
    std::size_t entries_applied{0};
    std::size_t blocks_applied{0};
};

// Bridges the transport-agnostic Raft state machine to the existing Ledger.
// Non-empty application commands are canonical serialized Ledger blocks;
// empty commands remain Raft no-ops. Ledger effects and Raft last_applied are
// committed in one SQLite transaction.
class RaftLedgerAdapter {
public:
    RaftLedgerAdapter(ledger::Ledger& ledger,
                      const std::string& database_path,
                      RaftConfig config);

    [[nodiscard]] std::vector<RaftMessage> tick(std::chrono::milliseconds elapsed);
    [[nodiscard]] std::vector<RaftMessage> receive(
        const RaftMessage& message,
        const std::string& authenticated_peer_id);
    [[nodiscard]] RaftBlockProposal propose_next_block(const Address& miner);
    [[nodiscard]] std::vector<RaftMessage> propose_membership_change(
        const std::vector<std::string>& new_members);
    [[nodiscard]] RaftApplicationResult apply_committed();
    [[nodiscard]] bool compact_applied();

    [[nodiscard]] const RaftNode& node() const noexcept { return *node_; }
    [[nodiscard]] const RaftPersistentState& persistent_state() const noexcept {
        return node_->persistent_state();
    }

    [[nodiscard]] static Bytes encode_block_command(const ledger::Block& block);
    [[nodiscard]] static ledger::Block decode_block_command(const Bytes& command);

private:
    void validate_recovered_ledger() const;
    [[nodiscard]] Bytes encode_application_snapshot(std::uint64_t index) const;
    [[nodiscard]] static std::pair<std::uint64_t, std::vector<ledger::Block>>
        decode_application_snapshot(const Bytes& payload);

    ledger::Ledger& ledger_;
    std::uint64_t snapshot_threshold_entries_{128};
    RaftStateStore store_;
    std::unique_ptr<RaftNode> node_;
};

} // namespace feinstat::consensus
