#include "feinstat/consensus/raft_membership.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/consensus/raft.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace feinstat::consensus {
namespace {
constexpr std::array<Byte, 4> kMagic{'F', 'S', 'R', 'C'};
constexpr Byte kVersion = 2;
constexpr std::size_t kMaximumIdBytes = 128;

bool valid_id(const std::string& id) noexcept {
    if (id.empty() || id.size() > kMaximumIdBytes) return false;
    for (const char raw : id) {
        const auto c = static_cast<unsigned char>(raw);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) {
            return false;
        }
    }
    return true;
}

void write_members(ByteWriter& writer, const std::vector<std::string>& members) {
    writer.u32(static_cast<std::uint32_t>(members.size()));
    for (const auto& member : members) writer.string(member);
}

std::vector<std::string> read_members(ByteReader& reader, const char* label) {
    const auto count = reader.u32();
    if (count > kRaftMaximumMembers) throw std::length_error(std::string(label) + " exceeds member limit");
    std::vector<std::string> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) result.push_back(reader.string(kMaximumIdBytes));
    return result;
}
} // namespace

std::vector<std::string> RaftMembershipState::canonical_members(
    std::vector<std::string> members, const char* label) {
    validate_members(members, label);
    std::sort(members.begin(), members.end());
    if (std::adjacent_find(members.begin(), members.end()) != members.end()) {
        throw std::invalid_argument(std::string(label) + " contains duplicate node IDs");
    }
    return members;
}

void RaftMembershipState::validate_members(const std::vector<std::string>& members,
                                          const char* label) {
    if (members.size() < 3 || members.size() > kRaftMaximumMembers || members.size() % 2U == 0U) {
        throw std::invalid_argument(std::string(label) + " must contain an odd number of 3, 5, 7, or 9 voters");
    }
    for (const auto& member : members) {
        if (!valid_id(member)) throw std::invalid_argument(std::string(label) + " contains an invalid node ID");
    }
}

RaftMembershipState::RaftMembershipState(std::vector<std::string> voters,
                                         std::vector<std::string> learners)
    : voters_(canonical_members(std::move(voters), "voter configuration")) {
    if (learners.size() > kRaftMaximumMembers) throw std::length_error("learner set exceeds member limit");
    for (const auto& learner : learners) {
        if (!valid_id(learner)) throw std::invalid_argument("learner set contains an invalid node ID");
    }
    std::sort(learners.begin(), learners.end());
    if (std::adjacent_find(learners.begin(), learners.end()) != learners.end()) {
        throw std::invalid_argument("learner set contains duplicate node IDs");
    }
    for (const auto& learner : learners) {
        if (std::binary_search(voters_.begin(), voters_.end(), learner)) {
            throw std::invalid_argument("a node cannot be both voter and learner");
        }
    }
    if (voters_.size() + learners.size() > kRaftMaximumMembers) {
        throw std::length_error("voters and pre-provisioned learners exceed cluster member limit");
    }
    learners_ = std::move(learners);
}

void RaftMembershipState::begin_joint(std::vector<std::string> new_voters,
                                      std::uint64_t joint_index) {
    if (phase_ != MembershipPhase::Stable) throw std::logic_error("membership change already in progress");
    if (joint_index <= configuration_index_) throw std::invalid_argument("joint configuration index must advance the configuration history");
    new_voters = canonical_members(std::move(new_voters), "new voter configuration");
    if (new_voters == voters_) throw std::invalid_argument("new voter configuration is identical to current configuration");
    std::set<std::string> known(voters_.begin(), voters_.end());
    known.insert(learners_.begin(), learners_.end());
    for (const auto& voter : new_voters) {
        if (!known.contains(voter)) {
            throw std::invalid_argument("new voter is not pre-provisioned as a voter or learner");
        }
    }
    std::set<std::string> replication = known;
    replication.insert(new_voters.begin(), new_voters.end());
    if (replication.size() > kRaftMaximumMembers) {
        throw std::length_error("joint configuration and pre-provisioned learners exceed cluster member limit");
    }
    new_voters_ = std::move(new_voters);
    joint_index_ = joint_index;
    final_index_ = 0;
    phase_ = MembershipPhase::JointPending;
}

void RaftMembershipState::mark_joint_committed(std::uint64_t committed_index) {
    if (phase_ != MembershipPhase::JointPending || committed_index < joint_index_) {
        throw std::logic_error("joint configuration cannot be marked committed before its log entry commits");
    }
    phase_ = MembershipPhase::JointCommitted;
}

void RaftMembershipState::cancel_uncommitted_joint() {
    if (phase_ != MembershipPhase::JointPending) throw std::logic_error("only an uncommitted joint configuration can be cancelled");
    new_voters_.clear();
    joint_index_ = 0;
    final_index_ = 0;
    phase_ = MembershipPhase::Stable;
}

void RaftMembershipState::begin_final(std::uint64_t final_index) {
    if (phase_ != MembershipPhase::JointCommitted) {
        throw std::logic_error("final configuration requires a committed joint configuration");
    }
    if (final_index <= joint_index_) throw std::invalid_argument("final configuration index must follow joint index");
    final_index_ = final_index;
    phase_ = MembershipPhase::FinalPending;
}

void RaftMembershipState::mark_final_committed(std::uint64_t committed_index) {
    if (phase_ != MembershipPhase::FinalPending || committed_index < final_index_) {
        throw std::logic_error("final configuration cannot be installed before its log entry commits");
    }
    for (const auto& old_voter : voters_) {
        if (!std::binary_search(new_voters_.begin(), new_voters_.end(), old_voter) &&
            !std::binary_search(learners_.begin(), learners_.end(), old_voter)) {
            learners_.push_back(old_voter);
        }
    }
    for (const auto& new_voter : new_voters_) {
        learners_.erase(std::remove(learners_.begin(), learners_.end(), new_voter), learners_.end());
    }
    std::sort(learners_.begin(), learners_.end());
    voters_ = std::move(new_voters_);
    new_voters_.clear();
    phase_ = MembershipPhase::Stable;
    configuration_index_ = final_index_;
    joint_index_ = 0;
    final_index_ = 0;
}

void RaftMembershipState::cancel_uncommitted_final() {
    if (phase_ != MembershipPhase::FinalPending) throw std::logic_error("only an uncommitted final configuration can be cancelled");
    final_index_ = 0;
    phase_ = MembershipPhase::JointCommitted;
}

std::vector<std::string> RaftMembershipState::replication_members() const {
    std::vector<std::string> result = voters_;
    result.insert(result.end(), learners_.begin(), learners_.end());
    result.insert(result.end(), new_voters_.begin(), new_voters_.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool RaftMembershipState::is_voter(const std::string& id) const noexcept {
    const bool old_voter = std::binary_search(voters_.begin(), voters_.end(), id);
    return old_voter || (joint() && std::binary_search(new_voters_.begin(), new_voters_.end(), id));
}

bool RaftMembershipState::is_replication_member(const std::string& id) const noexcept {
    return std::binary_search(voters_.begin(), voters_.end(), id) ||
           std::binary_search(learners_.begin(), learners_.end(), id) ||
           std::binary_search(new_voters_.begin(), new_voters_.end(), id);
}

std::uint64_t RaftMembershipState::majority(std::size_t member_count) noexcept {
    return static_cast<std::uint64_t>(member_count / 2U + 1U);
}

bool RaftMembershipState::has_majority(const std::vector<std::string>& config,
                                       const std::set<std::string>& acknowledgements) const noexcept {
    std::size_t count = 0;
    for (const auto& member : config) if (acknowledgements.contains(member)) ++count;
    return count >= majority(config.size());
}

bool RaftMembershipState::has_quorum(const std::set<std::string>& acknowledgements) const noexcept {
    if (!has_majority(voters_, acknowledgements)) return false;
    return !joint() || has_majority(new_voters_, acknowledgements);
}

Bytes RaftMembershipState::serialize() const {
    ByteWriter body;
    body.u8(static_cast<Byte>(phase_));
    body.u64(configuration_index_);
    body.u64(joint_index_);
    body.u64(final_index_);
    write_members(body, voters_);
    write_members(body, learners_);
    write_members(body, new_voters_);
    Bytes payload = std::move(body).take();
    ByteWriter envelope;
    envelope.fixed(kMagic);
    envelope.u8(kVersion);
    envelope.blob(payload);
    envelope.fixed(crypto::sha256(payload));
    return std::move(envelope).take();
}

RaftMembershipState RaftMembershipState::deserialize(const Bytes& bytes) {
    if (bytes.size() > 8192U) throw std::length_error("Raft membership state exceeds 8 KiB");
    ByteReader envelope(bytes);
    if (envelope.fixed<4>() != kMagic || envelope.u8() != kVersion) {
        throw std::runtime_error("unsupported Raft membership state encoding");
    }
    const Bytes payload = envelope.blob(4096U);
    const Hash checksum = envelope.fixed<32>();
    if (!envelope.empty() || crypto::sha256(payload) != checksum) {
        throw std::runtime_error("Raft membership state checksum or envelope validation failed");
    }
    ByteReader reader(payload);
    const auto raw_phase = reader.u8();
    if (raw_phase > static_cast<Byte>(MembershipPhase::FinalPending)) {
        throw std::runtime_error("unknown Raft membership phase");
    }
    const auto configuration_index = reader.u64();
    const auto joint_index = reader.u64();
    const auto final_index = reader.u64();
    auto voters = read_members(reader, "voter configuration");
    auto learners = read_members(reader, "learner set");
    auto new_voters = read_members(reader, "new voter configuration");
    if (!reader.empty()) throw std::runtime_error("trailing bytes in Raft membership state");
    RaftMembershipState result(std::move(voters), std::move(learners));
    result.phase_ = static_cast<MembershipPhase>(raw_phase);
    result.configuration_index_ = configuration_index;
    result.new_voters_ = new_voters.empty() ? std::vector<std::string>{}
                                           : canonical_members(std::move(new_voters), "new voter configuration");
    result.joint_index_ = joint_index;
    result.final_index_ = final_index;
    if (result.phase_ == MembershipPhase::Stable) {
        if (!result.new_voters_.empty() || joint_index != 0 || final_index != 0) {
            throw std::runtime_error("stable membership state contains unfinished transition metadata");
        }
    } else {
        if (result.new_voters_.empty() || joint_index <= configuration_index || result.new_voters_ == result.voters_) {
            throw std::runtime_error("joint membership state is missing or has invalid transition metadata");
        }
        for (const auto& id : result.new_voters_) {
            if (!std::binary_search(result.voters_.begin(), result.voters_.end(), id) &&
                !std::binary_search(result.learners_.begin(), result.learners_.end(), id)) {
                throw std::runtime_error("joint membership references an unknown learner/voter");
            }
        }
        if (result.phase_ == MembershipPhase::FinalPending) {
            if (final_index <= joint_index) throw std::runtime_error("final configuration index is invalid");
        } else if (final_index != 0) {
            throw std::runtime_error("unexpected final configuration index");
        }
    }
    return result;
}

} // namespace feinstat::consensus
