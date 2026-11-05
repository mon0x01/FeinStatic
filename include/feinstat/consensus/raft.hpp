#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/consensus/raft_membership.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace feinstat::consensus {

enum class RaftRole : std::uint8_t { Follower = 0, Candidate = 1, Leader = 2 };
enum class RaftMessageType : std::uint8_t {
    RequestVote = 1,
    VoteResponse = 2,
    AppendEntries = 3,
    AppendResponse = 4,
    InstallSnapshot = 5,
    SnapshotResponse = 6
};

inline constexpr std::size_t kRaftMaximumMembers = 9;
inline constexpr std::size_t kRaftMaximumEntriesPerMessage = 16;
inline constexpr std::size_t kRaftMaximumCommandBytes = 1U * 1024U * 1024U;
inline constexpr std::size_t kRaftMaximumSnapshotBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kRaftSnapshotChunkBytes = 512U * 1024U;

struct RaftLogEntry {
    std::uint64_t index{0};
    std::uint64_t term{0};
    Bytes command; // Empty command is a Raft no-op; applications validate non-empty commands.
    bool operator==(const RaftLogEntry&) const = default;
};

struct RaftMessage {
    RaftMessageType type{RaftMessageType::RequestVote};
    std::uint64_t term{0};
    std::string from;
    std::string to;

    // RequestVote / VoteResponse.
    std::uint64_t last_log_index{0};
    std::uint64_t last_log_term{0};
    bool vote_granted{false};

    // AppendEntries / AppendResponse.
    std::uint64_t previous_log_index{0};
    std::uint64_t previous_log_term{0};
    std::vector<RaftLogEntry> entries;
    std::uint64_t leader_commit{0};
    bool success{false};
    std::uint64_t match_index{0};
    std::uint64_t conflict_index{0};
    std::uint64_t conflict_term{0};

    // InstallSnapshot / SnapshotResponse. Payloads are transferred in bounded,
    // independently acknowledged chunks; the complete snapshot is checksummed.
    std::uint64_t snapshot_index{0};
    std::uint64_t snapshot_term{0};
    std::uint64_t snapshot_offset{0};
    std::uint64_t snapshot_total_size{0};
    Hash snapshot_hash{};
    Bytes snapshot_chunk;
    // Membership state at the snapshot boundary. Empty only for legacy peers.
    Bytes snapshot_membership;

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static RaftMessage deserialize(const Bytes& bytes);
};

struct RaftPersistentState {
    // The checkpoint is the identical, already-validated Ledger prefix from
    // which every member begins consensus. Raft log indices are anchored at
    // checkpoint_height, but later indices are not Ledger heights: Raft no-op
    // entries and application commands have distinct lifecycle semantics.
    std::uint64_t checkpoint_height{0};
    Hash checkpoint_hash{};
    Hash configuration_hash{};
    std::uint64_t current_term{0};
    std::string voted_for;
    std::vector<RaftLogEntry> log;
    std::uint64_t commit_index{0};
    std::uint64_t last_applied{0};

    // The compacted Raft prefix is represented by this boundary and a separate
    // checksummed application snapshot. checkpoint_* remains the immutable
    // common initial Ledger checkpoint used to identify the cluster bootstrap.
    std::uint64_t snapshot_index{0};
    std::uint64_t snapshot_term{0};
    Hash snapshot_hash{};
    std::uint64_t snapshot_size{0};
    // v1.3.0+: current stable/joint configuration. Absent only when loading
    // a legacy v1.2.0 Raft state; RaftNode upgrades it from bootstrap config.
    std::optional<RaftMembershipState> membership;
    // Membership exactly at snapshot_index, distinct from current configuration
    // when later configuration entries exist in the retained log.
    std::optional<RaftMembershipState> snapshot_membership;

    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static RaftPersistentState deserialize(const Bytes& bytes);
};

struct RaftConfig {
    std::string node_id;
    std::string cluster_id;
    std::vector<std::string> members; // Initial voting configuration / immutable bootstrap fingerprint.
    std::vector<std::string> learners; // Pre-provisioned non-voting peers eligible for replication/promotion.
    std::uint64_t checkpoint_height{0};
    Hash checkpoint_hash{};
    std::chrono::milliseconds election_timeout_min{350};
    std::chrono::milliseconds election_timeout_max{650};
    std::chrono::milliseconds heartbeat_interval{100};
    std::uint64_t random_seed{0}; // Zero selects a process-local random seed.
    std::uint64_t snapshot_threshold_entries{128}; // Auto-compact after this many applied Raft entries.
};

using RaftPersistCallback = std::function<void(const RaftPersistentState&)>;
using RaftSnapshotReadCallback = std::function<Bytes(std::uint64_t)>;
using RaftSnapshotSaveCallback = std::function<void(std::uint64_t, const Bytes&)>;
using RaftSnapshotInstallCallback = std::function<void(
    const RaftPersistentState&, const Bytes&)>;

// A deterministic, crash-fault-tolerant Raft state machine. The operator
// pre-provisions endpoints/trust for voters and learners. Membership changes
// are committed through C_old,new then C_new log entries; both majorities are
// required while joint. The caller remains responsible for authenticated
// transport and application of committed Ledger commands.
class RaftNode {
public:
    RaftNode(RaftConfig config,
             std::optional<RaftPersistentState> recovered_state = std::nullopt,
             RaftPersistCallback persist = {},
             RaftSnapshotReadCallback read_snapshot = {},
             RaftSnapshotSaveCallback save_snapshot = {},
             RaftSnapshotInstallCallback install_snapshot = {});

    [[nodiscard]] std::vector<RaftMessage> tick(std::chrono::milliseconds elapsed);
    [[nodiscard]] std::vector<RaftMessage> receive(const RaftMessage& message);
    [[nodiscard]] std::vector<RaftMessage> propose(const Bytes& command);
    // Propose a safe online voter-set transition. Every new voter must already
    // be provisioned as a voter or learner in the initial peer roster.
    [[nodiscard]] std::vector<RaftMessage> propose_membership_change(
        const std::vector<std::string>& new_members);
    [[nodiscard]] const RaftMembershipState& membership() const noexcept { return *state_.membership; }
    [[nodiscard]] static bool is_membership_command(const Bytes& command) noexcept;

    [[nodiscard]] std::vector<RaftLogEntry> committed_entries() const;
    void mark_applied(std::uint64_t index);
    // Persist the next applied index through a caller-supplied atomic boundary.
    // This is used when an application state transition (for example, a Ledger
    // block) must commit in the same database transaction as last_applied.
    void mark_applied_atomically(std::uint64_t index,
                                 const RaftPersistCallback& atomic_commit);
    // Compact only an already-applied prefix. Snapshot bytes are durably stored
    // before the Raft state is allowed to reference them.
    [[nodiscard]] bool compact_applied(const Bytes& application_snapshot);

    [[nodiscard]] const RaftPersistentState& persistent_state() const noexcept { return state_; }
    [[nodiscard]] RaftRole role() const noexcept { return role_; }
    [[nodiscard]] const std::string& leader_id() const noexcept { return leader_id_; }
    [[nodiscard]] const std::string& node_id() const noexcept { return config_.node_id; }
    [[nodiscard]] std::uint64_t last_log_index() const noexcept;
    [[nodiscard]] std::uint64_t last_log_term() const noexcept;
    [[nodiscard]] std::uint64_t snapshot_index() const noexcept { return state_.snapshot_index; }
    // Stable majority size, or the larger of the two joint majorities. This
    // scalar is informational only; joint safety always uses has_quorum().
    [[nodiscard]] std::uint64_t quorum_size() const noexcept;
    [[nodiscard]] bool leader_ready() const noexcept;
    [[nodiscard]] bool healthy() const noexcept { return !persistence_failed_; }

private:
    [[nodiscard]] Hash calculate_configuration_hash() const;
    void reset_election_timeout();
    void persist();
    void ensure_healthy() const;
    void become_follower(std::uint64_t new_term);
    [[nodiscard]] std::vector<RaftMessage> start_election();
    [[nodiscard]] std::vector<RaftMessage> become_leader();
    [[nodiscard]] RaftMessage make_vote_response(const RaftMessage& request, bool granted) const;
    [[nodiscard]] RaftMessage make_append_response(const RaftMessage& request, bool success,
                                                   std::uint64_t match_index,
                                                   std::uint64_t conflict_index,
                                                   std::uint64_t conflict_term = 0) const;
    [[nodiscard]] RaftMessage make_append_request(const std::string& peer_id) const;
    [[nodiscard]] RaftMessage make_snapshot_request(const std::string& peer_id,
                                                    std::uint64_t offset) const;
    [[nodiscard]] RaftMessage make_snapshot_response(const RaftMessage& request,
                                                     bool success,
                                                     std::uint64_t next_offset) const;
    [[nodiscard]] std::vector<RaftMessage> receive_install_snapshot(const RaftMessage& message);
    [[nodiscard]] std::vector<RaftMessage> receive_snapshot_response(const RaftMessage& message);
    [[nodiscard]] std::uint64_t term_at(std::uint64_t index) const;
    [[nodiscard]] std::uint64_t first_index_of_term(std::uint64_t term) const;
    [[nodiscard]] std::vector<RaftMessage> broadcast_append_entries() const;
    void advance_commit_index();
    void append_final_membership_entry();
    void refresh_membership_from_log(RaftPersistentState& candidate);
    [[nodiscard]] bool is_member(const std::string& peer_id) const noexcept;
    [[nodiscard]] bool is_replication_member(const std::string& peer_id) const noexcept;

    RaftConfig config_;
    RaftPersistentState state_;
    RaftPersistCallback persist_callback_;
    RaftSnapshotReadCallback read_snapshot_callback_;
    RaftSnapshotSaveCallback save_snapshot_callback_;
    RaftSnapshotInstallCallback install_snapshot_callback_;
    RaftRole role_{RaftRole::Follower};
    std::string leader_id_;
    std::map<std::string, bool> votes_received_;
    std::map<std::string, std::uint64_t> next_index_;
    std::map<std::string, std::uint64_t> match_index_;
    std::map<std::string, std::uint64_t> snapshot_send_offset_;
    std::uint64_t incoming_snapshot_index_{0};
    std::uint64_t incoming_snapshot_term_{0};
    std::uint64_t incoming_snapshot_total_size_{0};
    Hash incoming_snapshot_hash_{};
    Bytes incoming_snapshot_buffer_;
    std::string incoming_snapshot_from_;
    mutable Bytes outgoing_snapshot_buffer_;
    mutable std::uint64_t outgoing_snapshot_index_{0};
    std::chrono::milliseconds election_elapsed_{0};
    std::chrono::milliseconds heartbeat_elapsed_{0};
    std::chrono::milliseconds election_timeout_current_{0};
    std::uint64_t rng_state_{0};
    bool persistence_failed_{false};
};

} // namespace feinstat::consensus
