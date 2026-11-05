#pragma once

#include "feinstat/common/types.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace feinstat::consensus {

// Durable joint-consensus configuration state machine (Raft §6).
// This component deliberately does not perform network orchestration: callers
// must only advance phases after the corresponding Raft log entry is committed.
enum class MembershipPhase : std::uint8_t {
    Stable = 0,
    JointPending = 1,
    JointCommitted = 2,
    FinalPending = 3
};

class RaftMembershipState {
public:
    explicit RaftMembershipState(std::vector<std::string> voters,
                                 std::vector<std::string> learners = {});

    // Begin C_old,new. The caller supplies the Raft index of the joint entry.
    // Both majorities are required from this point until final configuration commit.
    void begin_joint(std::vector<std::string> new_voters, std::uint64_t joint_index);
    void mark_joint_committed(std::uint64_t committed_index);
    void cancel_uncommitted_joint();
    // Append the C_new entry only after C_old,new is committed.
    void begin_final(std::uint64_t final_index);
    // After C_new commits, old-only voters become learners; new voters become stable.
    void mark_final_committed(std::uint64_t committed_index);
    void cancel_uncommitted_final();

    [[nodiscard]] MembershipPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool stable() const noexcept { return phase_ == MembershipPhase::Stable; }
    [[nodiscard]] bool joint() const noexcept { return phase_ != MembershipPhase::Stable; }
    [[nodiscard]] std::uint64_t configuration_index() const noexcept { return configuration_index_; }
    [[nodiscard]] std::uint64_t joint_index() const noexcept { return joint_index_; }
    [[nodiscard]] std::uint64_t final_index() const noexcept { return final_index_; }
    [[nodiscard]] const std::vector<std::string>& voters() const noexcept { return voters_; }
    [[nodiscard]] const std::vector<std::string>& learners() const noexcept { return learners_; }
    [[nodiscard]] const std::vector<std::string>& new_voters() const noexcept { return new_voters_; }
    [[nodiscard]] std::vector<std::string> replication_members() const;
    [[nodiscard]] bool is_voter(const std::string& id) const noexcept;
    [[nodiscard]] bool is_replication_member(const std::string& id) const noexcept;

    // A joint configuration is satisfied only by a majority of EACH config.
    // The set input naturally deduplicates repeated acknowledgements.
    [[nodiscard]] bool has_quorum(const std::set<std::string>& acknowledgements) const noexcept;
    [[nodiscard]] static std::uint64_t majority(std::size_t member_count) noexcept;

    // Checksummed, bounded, canonical encoding suitable for a durable Raft state row.
    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] static RaftMembershipState deserialize(const Bytes& bytes);

private:
    static std::vector<std::string> canonical_members(std::vector<std::string> members,
                                                       const char* label);
    static void validate_members(const std::vector<std::string>& members, const char* label);
    [[nodiscard]] bool has_majority(const std::vector<std::string>& config,
                                    const std::set<std::string>& acknowledgements) const noexcept;

    std::vector<std::string> voters_;
    std::vector<std::string> learners_;
    std::vector<std::string> new_voters_;
    MembershipPhase phase_{MembershipPhase::Stable};
    std::uint64_t configuration_index_{0};
    std::uint64_t joint_index_{0};
    std::uint64_t final_index_{0};
};

} // namespace feinstat::consensus
