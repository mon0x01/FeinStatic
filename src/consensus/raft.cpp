#include "feinstat/consensus/raft.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

namespace feinstat::consensus {
namespace {

constexpr std::array<Byte, 4> kMessageMagic{'F', 'S', 'R', 'M'};
constexpr std::array<Byte, 4> kStateMagic{'F', 'S', 'R', 'S'};
constexpr Byte kMessageEncodingVersion = 3;
constexpr Byte kStateEncodingVersion = 3;
constexpr std::size_t kMaximumNodeIdBytes = 128;
constexpr std::uint64_t kMaximumLogEntries = 5'000'000;
constexpr std::array<Byte, 4> kMembershipCommandMagic{'F', 'S', 'R', 'G'};
constexpr Byte kMembershipCommandVersion = 1;
enum class MembershipCommandKind : Byte { Joint = 1, Final = 2 };

void validate_id(const std::string& value, const char* field, bool allow_empty = false);

struct MembershipCommand {
    MembershipCommandKind kind{MembershipCommandKind::Joint};
    std::vector<std::string> members;
};

Bytes encode_membership_command(MembershipCommandKind kind,
                                const std::vector<std::string>& members) {
    ByteWriter writer;
    writer.fixed(kMembershipCommandMagic);
    writer.u8(kMembershipCommandVersion);
    writer.u8(static_cast<Byte>(kind));
    writer.u32(static_cast<std::uint32_t>(members.size()));
    for (const auto& member : members) writer.string(member);
    return std::move(writer).take();
}

std::optional<MembershipCommand> decode_membership_command(const Bytes& command) {
    if (command.size() < kMembershipCommandMagic.size() ||
        !std::equal(kMembershipCommandMagic.begin(), kMembershipCommandMagic.end(), command.begin())) {
        return std::nullopt;
    }
    ByteReader reader(command);
    if (reader.fixed<4>() != kMembershipCommandMagic || reader.u8() != kMembershipCommandVersion) {
        throw std::runtime_error("unsupported Raft membership command encoding");
    }
    const Byte raw_kind = reader.u8();
    if (raw_kind != static_cast<Byte>(MembershipCommandKind::Joint) &&
        raw_kind != static_cast<Byte>(MembershipCommandKind::Final)) {
        throw std::runtime_error("unknown Raft membership command kind");
    }
    const auto count = reader.u32();
    if (count < 3 || count > kRaftMaximumMembers || count % 2U == 0U) {
        throw std::runtime_error("Raft membership command has an invalid voter count");
    }
    MembershipCommand result;
    result.kind = static_cast<MembershipCommandKind>(raw_kind);
    result.members.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) result.members.push_back(reader.string(kMaximumNodeIdBytes));
    if (!reader.empty()) throw std::runtime_error("trailing bytes in Raft membership command");
    auto canonical = result.members;
    std::sort(canonical.begin(), canonical.end());
    if (canonical != result.members || std::adjacent_find(canonical.begin(), canonical.end()) != canonical.end()) {
        throw std::runtime_error("Raft membership command is not canonically sorted and unique");
    }
    for (const auto& member : result.members) validate_id(member, "membership command node ID");
    return result;
}

void append_bool(ByteWriter& writer, bool value) { writer.u8(value ? 1U : 0U); }

void saturating_add(std::chrono::milliseconds& target, std::chrono::milliseconds increment) {
    using Rep = std::chrono::milliseconds::rep;
    const Rep current = target.count();
    const Rep amount = increment.count();
    if (amount > 0 && current > std::numeric_limits<Rep>::max() - amount) {
        target = std::chrono::milliseconds::max();
        return;
    }
    target += increment;
}

bool read_bool(ByteReader& reader, const char* field) {
    const Byte value = reader.u8();
    if (value > 1) throw std::runtime_error(std::string("invalid boolean in Raft ") + field);
    return value != 0;
}

void require_empty(const ByteReader& reader, const char* context) {
    if (!reader.empty()) throw std::runtime_error(std::string("trailing bytes in Raft ") + context);
}

void validate_id(const std::string& value, const char* field, bool allow_empty) {
    if (allow_empty && value.empty()) return;
    if (value.empty() || value.size() > kMaximumNodeIdBytes ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch <= 0x20U || ch == 0x7fU;
        })) {
        throw std::invalid_argument(std::string("invalid Raft ") + field);
    }
}

void validate_message_shape(const RaftMessage& message) {
    constexpr std::size_t kMaximumRaftMessageBytes = 15U * 1024U * 1024U;
    if (message.term == 0) throw std::invalid_argument("Raft protocol messages must use a positive term");
    validate_id(message.from, "message sender");
    validate_id(message.to, "message recipient");
    if (message.type == RaftMessageType::InstallSnapshot) {
        if (message.snapshot_chunk.size() > kRaftSnapshotChunkBytes ||
            message.snapshot_total_size == 0 ||
            message.snapshot_index > static_cast<std::uint64_t>(INT64_MAX) ||
            message.snapshot_total_size > kRaftMaximumSnapshotBytes ||
            message.snapshot_offset >= message.snapshot_total_size ||
            message.snapshot_chunk.size() > message.snapshot_total_size - message.snapshot_offset ||
            message.snapshot_membership.size() > 8192U) {
            throw std::length_error("Raft snapshot chunk exceeds configured bounds");
        }
    } else if (message.type == RaftMessageType::SnapshotResponse) {
        // snapshot_offset is an acknowledged byte position in the response,
        // not an offset into a response payload.
        if (message.snapshot_offset > kRaftMaximumSnapshotBytes ||
            !message.snapshot_chunk.empty() || !message.snapshot_membership.empty() || message.snapshot_total_size != 0) {
            throw std::length_error("Raft snapshot acknowledgement exceeds configured bounds");
        }
    } else if (message.snapshot_index != 0 || message.snapshot_term != 0 ||
               message.snapshot_offset != 0 || message.snapshot_total_size != 0 ||
               message.snapshot_hash != Hash{} || !message.snapshot_chunk.empty() ||
               !message.snapshot_membership.empty()) {
        throw std::invalid_argument("non-snapshot Raft message contains snapshot fields");
    }
    const auto zero_snapshot_fields = [&] {
        return message.snapshot_index == 0 && message.snapshot_term == 0 &&
               message.snapshot_offset == 0 && message.snapshot_total_size == 0 &&
               message.snapshot_hash == Hash{} && message.snapshot_chunk.empty() &&
               message.snapshot_membership.empty();
    };
    const auto zero_vote_fields = [&] {
        return message.last_log_index == 0 && message.last_log_term == 0 && !message.vote_granted;
    };
    const auto zero_append_fields = [&] {
        return message.previous_log_index == 0 && message.previous_log_term == 0 &&
               message.leader_commit == 0 && !message.success && message.match_index == 0 &&
               message.conflict_index == 0 && message.conflict_term == 0 && message.entries.empty();
    };

    std::size_t estimated_size = 4U + 2U + 8U + 4U + message.from.size() + 4U + message.to.size() +
        8U * 14U + 1U + 1U + 4U + message.snapshot_chunk.size() + 4U + message.snapshot_membership.size();
    for (const auto& entry : message.entries) {
        if (entry.command.size() > kRaftMaximumCommandBytes ||
            entry.command.size() > kMaximumRaftMessageBytes - std::min(estimated_size, kMaximumRaftMessageBytes) ||
            estimated_size > kMaximumRaftMessageBytes - std::min<std::size_t>(20U, kMaximumRaftMessageBytes)) {
            throw std::length_error("Raft message exceeds configured size limit");
        }
        estimated_size += 20U + entry.command.size();
    }
    if (estimated_size > kMaximumRaftMessageBytes) {
        throw std::length_error("Raft message exceeds configured size limit");
    }

    switch (message.type) {
    case RaftMessageType::RequestVote:
        if (message.vote_granted || !zero_append_fields() || !zero_snapshot_fields()) {
            throw std::invalid_argument("Raft RequestVote contains unexpected fields");
        }
        break;
    case RaftMessageType::VoteResponse:
        if (!zero_append_fields() || message.last_log_index != 0 || message.last_log_term != 0 ||
            !zero_snapshot_fields()) {
            throw std::invalid_argument("Raft VoteResponse contains unexpected fields");
        }
        break;
    case RaftMessageType::AppendEntries:
        if (message.vote_granted || message.last_log_index != 0 || message.last_log_term != 0 ||
            message.success || message.match_index != 0 || message.conflict_index != 0 ||
            message.conflict_term != 0 || message.entries.size() > kRaftMaximumEntriesPerMessage ||
            !zero_snapshot_fields()) {
            throw std::invalid_argument("Raft AppendEntries contains unexpected fields");
        }
        break;
    case RaftMessageType::AppendResponse:
        if (message.vote_granted || message.last_log_index != 0 || message.last_log_term != 0 ||
            !message.entries.empty() || message.previous_log_index != 0 || message.previous_log_term != 0 ||
            message.leader_commit != 0 || (message.success && message.conflict_index != 0) ||
            !zero_snapshot_fields()) {
            throw std::invalid_argument("Raft AppendResponse contains unexpected fields");
        }
        break;
    case RaftMessageType::InstallSnapshot:
        if (!zero_vote_fields() || !zero_append_fields() || message.snapshot_index == 0 ||
            message.snapshot_term == 0 || message.snapshot_term > message.term ||
            message.snapshot_total_size == 0 || message.snapshot_chunk.empty() ||
            message.snapshot_offset >= message.snapshot_total_size) {
            throw std::invalid_argument("Raft InstallSnapshot contains invalid or unexpected fields");
        }
        break;
    case RaftMessageType::SnapshotResponse:
        if (!zero_vote_fields() || message.previous_log_index != 0 || message.previous_log_term != 0 ||
            message.leader_commit != 0 || message.match_index != 0 || message.conflict_index != 0 ||
            message.conflict_term != 0 || !message.entries.empty() || message.snapshot_index == 0 ||
            message.snapshot_term != 0 || message.snapshot_total_size != 0 ||
            message.snapshot_hash != Hash{} || !message.snapshot_chunk.empty() ||
            !message.snapshot_membership.empty()) {
            throw std::invalid_argument("Raft SnapshotResponse contains invalid or unexpected fields");
        }
        break;
    default:
        throw std::invalid_argument("unknown Raft message type");
    }
}

Bytes state_body(const RaftPersistentState& state) {
    if (state.voted_for.size() > kMaximumNodeIdBytes || state.log.size() > kMaximumLogEntries) {
        throw std::length_error("Raft persistent state exceeds configured limits");
    }
    if (state.checkpoint_height > static_cast<std::uint64_t>(INT64_MAX) ||
        state.snapshot_index < state.checkpoint_height ||
        state.commit_index < state.snapshot_index ||
        state.last_applied < state.snapshot_index ||
        state.last_applied > state.commit_index ||
        state.log.size() > UINT64_MAX - state.snapshot_index) {
        throw std::invalid_argument("Raft commit/apply/snapshot indices are inconsistent");
    }
    if (state.snapshot_index == state.checkpoint_height) {
        if (state.snapshot_term != 0 || state.snapshot_size != 0 || state.snapshot_hash != Hash{}) {
            throw std::invalid_argument("initial Raft checkpoint cannot carry compacted snapshot metadata");
        }
    } else if (state.snapshot_term == 0 || state.snapshot_term > state.current_term ||
               state.snapshot_size == 0 || state.snapshot_size > kRaftMaximumSnapshotBytes) {
        throw std::invalid_argument("compacted Raft snapshot metadata is incomplete or oversized");
    }

    constexpr std::size_t kSnapshotEnvelopeOverhead = 45U;
    constexpr std::size_t kStateHeaderBytes = 160U;
    if (state.voted_for.size() > kRaftMaximumSnapshotBytes - kSnapshotEnvelopeOverhead - kStateHeaderBytes ||
        state.log.size() > (kRaftMaximumSnapshotBytes - kSnapshotEnvelopeOverhead - kStateHeaderBytes -
                            state.voted_for.size()) / 20U) {
        throw std::length_error("Raft state snapshot exceeds the 64 MiB total size limit");
    }
    std::size_t estimated_bytes = kStateHeaderBytes + state.voted_for.size() + state.log.size() * 20U;
    if (state.membership) {
        const auto membership_bytes = state.membership->serialize().size();
        if (membership_bytes > kRaftMaximumSnapshotBytes - estimated_bytes) {
            throw std::length_error("Raft membership state exceeds persistent state size limit");
        }
        estimated_bytes += membership_bytes;
    }
    if (state.snapshot_membership) {
        const auto snapshot_membership_bytes = state.snapshot_membership->serialize().size();
        if (snapshot_membership_bytes > kRaftMaximumSnapshotBytes - estimated_bytes) {
            throw std::length_error("Raft snapshot membership state exceeds persistent state size limit");
        }
        estimated_bytes += snapshot_membership_bytes;
    }
    for (const auto& entry : state.log) {
        if (entry.command.size() > kRaftMaximumSnapshotBytes - kSnapshotEnvelopeOverhead - estimated_bytes) {
            throw std::length_error("Raft state snapshot exceeds the 64 MiB total size limit");
        }
        estimated_bytes += entry.command.size();
    }

    ByteWriter writer;
    writer.fixed(state.checkpoint_hash);
    writer.u64(state.checkpoint_height);
    writer.fixed(state.configuration_hash);
    writer.u64(state.current_term);
    writer.string(state.voted_for);
    writer.u64(state.commit_index);
    writer.u64(state.last_applied);
    writer.u64(state.snapshot_index);
    writer.u64(state.snapshot_term);
    writer.fixed(state.snapshot_hash);
    writer.u64(state.snapshot_size);
    if (state.membership) writer.blob(state.membership->serialize());
    else writer.blob({});
    if (state.snapshot_membership) writer.blob(state.snapshot_membership->serialize());
    else writer.blob({});
    writer.u32(static_cast<std::uint32_t>(state.log.size()));

    std::uint64_t expected_index = state.snapshot_index;
    std::uint64_t previous_term = state.snapshot_term;
    std::size_t total_command_bytes = 0;
    for (const auto& entry : state.log) {
        if (expected_index == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
        ++expected_index;
        if (entry.index != expected_index || entry.term == 0 || entry.term < previous_term ||
            entry.term > state.current_term || entry.command.size() > kRaftMaximumCommandBytes) {
            throw std::invalid_argument("Raft log entries are not canonical");
        }
        previous_term = entry.term;
        if (entry.command.size() > kRaftMaximumSnapshotBytes - total_command_bytes) {
            throw std::length_error("Raft state snapshot exceeds the 64 MiB total size limit");
        }
        total_command_bytes += entry.command.size();
        writer.u64(entry.index);
        writer.u64(entry.term);
        writer.blob(entry.command);
    }
    if (state.commit_index > expected_index) {
        throw std::invalid_argument("Raft commit index exceeds last log index");
    }
    return std::move(writer).take();
}

} // namespace

Bytes RaftMessage::serialize() const {
    validate_message_shape(*this);
    constexpr std::size_t kMaximumRaftMessageBytes = 15U * 1024U * 1024U;
    ByteWriter writer;
    writer.fixed(kMessageMagic);
    writer.u8(kMessageEncodingVersion);
    writer.u8(static_cast<Byte>(type));
    writer.u64(term);
    writer.string(from);
    writer.string(to);
    writer.u64(last_log_index);
    writer.u64(last_log_term);
    append_bool(writer, vote_granted);
    writer.u64(previous_log_index);
    writer.u64(previous_log_term);
    writer.u64(leader_commit);
    append_bool(writer, success);
    writer.u64(match_index);
    writer.u64(conflict_index);
    writer.u64(conflict_term);
    writer.u32(static_cast<std::uint32_t>(entries.size()));
    for (const auto& entry : entries) {
        if (entry.command.size() > kRaftMaximumCommandBytes) {
            throw std::length_error("Raft log command exceeds maximum size");
        }
        writer.u64(entry.index);
        writer.u64(entry.term);
        writer.blob(entry.command);
    }
    writer.u64(snapshot_index);
    writer.u64(snapshot_term);
    writer.u64(snapshot_offset);
    writer.u64(snapshot_total_size);
    writer.fixed(snapshot_hash);
    writer.blob(snapshot_chunk);
    writer.blob(snapshot_membership);
    Bytes body = std::move(writer).take();
    if (body.size() > kMaximumRaftMessageBytes) {
        throw std::length_error("Raft message exceeds snapshot chunk frame limit");
    }
    return body;
}

RaftMessage RaftMessage::deserialize(const Bytes& bytes) {
    constexpr std::size_t kMaximumRaftMessageBytes = 15U * 1024U * 1024U;
    if (bytes.size() > kMaximumRaftMessageBytes) throw std::length_error("Raft message exceeds frame limit");
    ByteReader reader(bytes);
    if (reader.fixed<4>() != kMessageMagic) throw std::runtime_error("unsupported Raft message encoding");
    const Byte version = reader.u8();
    if (version < 1 || version > kMessageEncodingVersion) {
        throw std::runtime_error("unsupported Raft message encoding");
    }
    RaftMessage result;
    result.type = static_cast<RaftMessageType>(reader.u8());
    result.term = reader.u64();
    result.from = reader.string(kMaximumNodeIdBytes);
    result.to = reader.string(kMaximumNodeIdBytes);
    result.last_log_index = reader.u64();
    result.last_log_term = reader.u64();
    result.vote_granted = read_bool(reader, "message");
    result.previous_log_index = reader.u64();
    result.previous_log_term = reader.u64();
    result.leader_commit = reader.u64();
    result.success = read_bool(reader, "message");
    result.match_index = reader.u64();
    result.conflict_index = reader.u64();
    result.conflict_term = reader.u64();
    const auto count = reader.u32();
    if (count > kRaftMaximumEntriesPerMessage) throw std::length_error("Raft message contains too many entries");
    result.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RaftLogEntry entry;
        entry.index = reader.u64();
        entry.term = reader.u64();
        entry.command = reader.blob(kRaftMaximumCommandBytes);
        result.entries.push_back(std::move(entry));
    }
    if (version >= 2) {
        result.snapshot_index = reader.u64();
        result.snapshot_term = reader.u64();
        result.snapshot_offset = reader.u64();
        result.snapshot_total_size = reader.u64();
        result.snapshot_hash = reader.fixed<32>();
        result.snapshot_chunk = reader.blob(kRaftSnapshotChunkBytes);
        if (version >= 3) result.snapshot_membership = reader.blob(8192U);
    }
    require_empty(reader, "message");
    validate_id(result.from, "message sender");
    validate_id(result.to, "message recipient");
    validate_message_shape(result);
    if (result.term == 0) throw std::runtime_error("Raft message term must be positive");
    return result;
}

Bytes RaftPersistentState::serialize() const {
    RaftPersistentState canonical = *this;
    // Preserve source compatibility for callers constructing the pre-v1.2
    // state shape directly rather than recovering it through deserialize().
    if (canonical.snapshot_index == 0 && canonical.checkpoint_height > 0 &&
        canonical.snapshot_term == 0 && canonical.snapshot_size == 0 && canonical.snapshot_hash == Hash{}) {
        canonical.snapshot_index = canonical.checkpoint_height;
    }
    const Bytes body = state_body(canonical);
    const Hash checksum = crypto::sha256(body);
    ByteWriter writer;
    writer.fixed(kStateMagic);
    writer.u8(kStateEncodingVersion);
    writer.blob(body);
    writer.fixed(checksum);
    Bytes result = std::move(writer).take();
    if (result.size() > kRaftMaximumSnapshotBytes) {
        throw std::length_error("Raft state snapshot exceeds the 64 MiB total size limit");
    }
    return result;
}

RaftPersistentState RaftPersistentState::deserialize(const Bytes& bytes) {
    if (bytes.empty() || bytes.size() > kRaftMaximumSnapshotBytes) {
        throw std::length_error("Raft state snapshot is empty or exceeds 64 MiB");
    }
    ByteReader envelope(bytes);
    if (envelope.fixed<4>() != kStateMagic) throw std::runtime_error("unsupported Raft state snapshot encoding");
    const Byte version = envelope.u8();
    if (version < 1 || version > kStateEncodingVersion) {
        throw std::runtime_error("unsupported Raft state snapshot encoding");
    }
    const Bytes body = envelope.blob(kRaftMaximumSnapshotBytes - 64U);
    const Hash checksum = envelope.fixed<32>();
    require_empty(envelope, "state snapshot envelope");
    if (crypto::sha256(body) != checksum) throw std::runtime_error("Raft state snapshot checksum mismatch");

    ByteReader reader(body);
    RaftPersistentState result;
    result.checkpoint_hash = reader.fixed<32>();
    result.checkpoint_height = reader.u64();
    result.configuration_hash = reader.fixed<32>();
    result.current_term = reader.u64();
    result.voted_for = reader.string(kMaximumNodeIdBytes);
    result.commit_index = reader.u64();
    result.last_applied = reader.u64();
    if (version >= 2) {
        result.snapshot_index = reader.u64();
        result.snapshot_term = reader.u64();
        result.snapshot_hash = reader.fixed<32>();
        result.snapshot_size = reader.u64();
    } else {
        result.snapshot_index = result.checkpoint_height;
    }
    if (version >= 3) {
        const Bytes membership_bytes = reader.blob(8192U);
        if (!membership_bytes.empty()) result.membership = RaftMembershipState::deserialize(membership_bytes);
        const Bytes snapshot_membership_bytes = reader.blob(8192U);
        if (!snapshot_membership_bytes.empty()) {
            result.snapshot_membership = RaftMembershipState::deserialize(snapshot_membership_bytes);
        }
    }
    const auto count = reader.u32();
    if (count > kMaximumLogEntries) throw std::length_error("persisted Raft log exceeds entry-count limit");
    result.log.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RaftLogEntry entry;
        entry.index = reader.u64();
        entry.term = reader.u64();
        entry.command = reader.blob(kRaftMaximumCommandBytes);
        result.log.push_back(std::move(entry));
    }
    require_empty(reader, "state snapshot body");
    (void)state_body(result);
    return result;
}

RaftNode::RaftNode(RaftConfig config,
                   std::optional<RaftPersistentState> recovered_state,
                   RaftPersistCallback persist_callback,
                   RaftSnapshotReadCallback read_snapshot_callback,
                   RaftSnapshotSaveCallback save_snapshot_callback,
                   RaftSnapshotInstallCallback install_snapshot_callback,
                   RaftSnapshotStageLoadCallback load_snapshot_stage_callback,
                   RaftSnapshotStageSaveCallback save_snapshot_stage_callback,
                   RaftSnapshotStageClearCallback clear_snapshot_stage_callback)
    : config_(std::move(config)),
      persist_callback_(std::move(persist_callback)),
      read_snapshot_callback_(std::move(read_snapshot_callback)),
      save_snapshot_callback_(std::move(save_snapshot_callback)),
      install_snapshot_callback_(std::move(install_snapshot_callback)),
      load_snapshot_stage_callback_(std::move(load_snapshot_stage_callback)),
      save_snapshot_stage_callback_(std::move(save_snapshot_stage_callback)),
      clear_snapshot_stage_callback_(std::move(clear_snapshot_stage_callback)) {
    validate_id(config_.node_id, "node ID");
    validate_id(config_.cluster_id, "cluster ID");
    if (config_.members.size() < 3 || config_.members.size() > kRaftMaximumMembers ||
        (config_.members.size() % 2U) == 0U) {
        throw std::invalid_argument("Raft bootstrap voter set must contain 3, 5, 7, or 9 nodes");
    }
    for (const auto& member : config_.members) validate_id(member, "member ID");
    std::sort(config_.members.begin(), config_.members.end());
    if (std::adjacent_find(config_.members.begin(), config_.members.end()) != config_.members.end()) {
        throw std::invalid_argument("Raft membership contains duplicate node IDs");
    }
    for (const auto& learner : config_.learners) validate_id(learner, "learner ID");
    std::sort(config_.learners.begin(), config_.learners.end());
    if (std::adjacent_find(config_.learners.begin(), config_.learners.end()) != config_.learners.end()) {
        throw std::invalid_argument("Raft learner set contains duplicate node IDs");
    }
    for (const auto& learner : config_.learners) {
        if (std::binary_search(config_.members.begin(), config_.members.end(), learner)) {
            throw std::invalid_argument("Raft node cannot be both a voter and learner");
        }
    }
    if (config_.members.size() + config_.learners.size() > kRaftMaximumMembers) {
        throw std::length_error("Raft voter and learner roster exceeds nine nodes");
    }
    if (config_.election_timeout_min < std::chrono::milliseconds(150) ||
        config_.election_timeout_max <= config_.election_timeout_min) {
        throw std::invalid_argument("Raft election timeout range is invalid");
    }
    const auto minimum_timeout = config_.election_timeout_min.count(); // Positive after the range check.
    const auto heartbeat_interval = config_.heartbeat_interval.count();
    // Avoid multiplying the caller-provided heartbeat duration: hostile or
    // accidental extreme values must not overflow chrono's signed rep type.
    const bool heartbeat_too_slow = heartbeat_interval <= 0 ||
        heartbeat_interval > (minimum_timeout - 1) / 3;
    if (heartbeat_too_slow) {
        throw std::invalid_argument("Raft timing requires heartbeat < one third of minimum election timeout");
    }
    if (config_.checkpoint_height > static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft checkpoint height exceeds storage range");
    }

    const Hash config_hash = calculate_configuration_hash();
    if (config_.random_seed == 0) {
        std::random_device device;
        rng_state_ = (static_cast<std::uint64_t>(device()) << 32U) ^ device();
    } else {
        rng_state_ = config_.random_seed;
    }
    if (rng_state_ == 0) rng_state_ = 0x9e3779b97f4a7c15ULL;

    if (recovered_state.has_value()) {
        state_ = std::move(*recovered_state);
        if (state_.snapshot_index == 0 && state_.checkpoint_height > 0 &&
            state_.snapshot_term == 0 && state_.snapshot_size == 0 && state_.snapshot_hash == Hash{}) {
            state_.snapshot_index = state_.checkpoint_height;
        }
        if (state_.checkpoint_height != config_.checkpoint_height ||
            state_.checkpoint_hash != config_.checkpoint_hash ||
            state_.configuration_hash != config_hash) {
            throw std::runtime_error("persisted Raft state belongs to a different bootstrap configuration or Ledger checkpoint");
        }
        if (!state_.membership) state_.membership.emplace(config_.members, config_.learners);
        if (!state_.snapshot_membership) state_.snapshot_membership = state_.membership;
        const auto configured_roster = [&] {
            std::set<std::string> roster(config_.members.begin(), config_.members.end());
            roster.insert(config_.learners.begin(), config_.learners.end());
            return roster;
        }();
        for (const auto& member : state_.membership->replication_members()) {
            if (!configured_roster.contains(member)) {
                throw std::runtime_error("persisted dynamic membership references a peer without a pre-provisioned endpoint/trust entry");
            }
        }
        (void)state_body(state_);
        if (!state_.voted_for.empty() && !is_replication_member(state_.voted_for)) {
            throw std::runtime_error("persisted Raft vote references a peer outside the pre-provisioned roster");
        }
    } else {
        state_.checkpoint_height = config_.checkpoint_height;
        state_.snapshot_index = config_.checkpoint_height;
        state_.checkpoint_hash = config_.checkpoint_hash;
        state_.configuration_hash = config_hash;
        state_.membership.emplace(config_.members, config_.learners);
        state_.snapshot_membership = state_.membership;
        state_.commit_index = config_.checkpoint_height;
        state_.last_applied = config_.checkpoint_height;
        persist();
    }
    if (!is_replication_member(config_.node_id)) {
        throw std::invalid_argument("local node is absent from Raft voter and learner roster");
    }
    reset_election_timeout();
    if (load_snapshot_stage_callback_) {
        const auto staged = load_snapshot_stage_callback_();
        if (staged) {
            if (staged->source_id.empty() || !is_member(staged->source_id) ||
                staged->snapshot_index == 0 || staged->snapshot_term == 0 ||
                staged->snapshot_term > state_.current_term || staged->total_size == 0 ||
                staged->total_size > kRaftMaximumSnapshotBytes || staged->payload.size() > staged->total_size ||
                staged->membership.size() > 8192U) {
                throw std::runtime_error("durable incoming Raft snapshot staging metadata is invalid");
            }
            if (staged->snapshot_index <= state_.commit_index) {
                if (clear_snapshot_stage_callback_) clear_snapshot_stage_callback_();
            } else {
                incoming_snapshot_index_ = staged->snapshot_index;
                incoming_snapshot_from_ = staged->source_id;
                incoming_snapshot_term_ = staged->snapshot_term;
                incoming_snapshot_total_size_ = staged->total_size;
                incoming_snapshot_hash_ = staged->checksum;
                incoming_snapshot_membership_ = staged->membership;
                incoming_snapshot_buffer_ = staged->payload;
                if (incoming_snapshot_buffer_.size() == incoming_snapshot_total_size_) {
                    if (crypto::sha256(incoming_snapshot_buffer_) != incoming_snapshot_hash_) {
                        throw std::runtime_error("completed durable incoming Raft snapshot failed its checksum");
                    }
                    complete_incoming_snapshot();
                }
            }
        }
    }
}

Hash RaftNode::calculate_configuration_hash() const {
    ByteWriter writer;
    // Preserve the v1.2 fingerprint for old clusters with no pre-provisioned
    // learners. Clusters that introduce a learner roster bind that exact roster
    // into a distinct v2 fingerprint so nodes cannot silently disagree about
    // which authenticated endpoints are eligible for promotion.
    if (config_.learners.empty()) {
        writer.string("FeinStatic Raft static configuration v1");
    } else {
        writer.string("FeinStatic Raft configuration with provisioned learners v2");
    }
    writer.string(config_.cluster_id);
    writer.u64(config_.checkpoint_height);
    writer.fixed(config_.checkpoint_hash);
    writer.u32(static_cast<std::uint32_t>(config_.members.size()));
    for (const auto& member : config_.members) writer.string(member);
    if (!config_.learners.empty()) {
        writer.u32(static_cast<std::uint32_t>(config_.learners.size()));
        for (const auto& learner : config_.learners) writer.string(learner);
    }
    return crypto::sha256(writer.data());
}


bool RaftNode::is_membership_command(const Bytes& command) noexcept {
    return command.size() >= kMembershipCommandMagic.size() &&
        std::equal(kMembershipCommandMagic.begin(), kMembershipCommandMagic.end(), command.begin());
}

bool RaftNode::is_member(const std::string& peer_id) const noexcept {
    return state_.membership ? state_.membership->is_voter(peer_id)
                             : std::binary_search(config_.members.begin(), config_.members.end(), peer_id);
}

bool RaftNode::is_replication_member(const std::string& peer_id) const noexcept {
    return state_.membership ? state_.membership->is_replication_member(peer_id)
        : (std::binary_search(config_.members.begin(), config_.members.end(), peer_id) ||
           std::binary_search(config_.learners.begin(), config_.learners.end(), peer_id));
}

void RaftNode::persist() {
    ensure_healthy();
    if (!persist_callback_) return;
    try {
        persist_callback_(state_);
    } catch (...) {
        // A term, vote, log or commit-index write may already have changed the
        // in-memory candidate state. Continuing after a failed durable write
        // could violate Raft safety, so this instance becomes permanently
        // unusable and must be reconstructed from durable storage.
        persistence_failed_ = true;
        throw;
    }
}

void RaftNode::ensure_healthy() const {
    if (persistence_failed_) {
        throw std::runtime_error("Raft node disabled after a persistence failure; reopen from durable state");
    }
}

void RaftNode::reset_election_timeout() {
    // xorshift64*: deterministic when a test seed is supplied, randomized per
    // process otherwise. This is timer jitter, not cryptographic randomness.
    rng_state_ ^= rng_state_ >> 12U;
    rng_state_ ^= rng_state_ << 25U;
    rng_state_ ^= rng_state_ >> 27U;
    const std::uint64_t random_value = rng_state_ * 0x2545F4914F6CDD1DULL;
    const auto span = static_cast<std::uint64_t>(
        (config_.election_timeout_max - config_.election_timeout_min).count());
    const auto offset = span == 0 ? 0 : random_value % (span + 1U);
    election_timeout_current_ = config_.election_timeout_min + std::chrono::milliseconds(offset);
    election_elapsed_ = std::chrono::milliseconds::zero();
}

std::uint64_t RaftNode::last_log_index() const noexcept {
    return state_.snapshot_index + static_cast<std::uint64_t>(state_.log.size());
}

std::uint64_t RaftNode::last_log_term() const noexcept {
    return state_.log.empty() ? state_.snapshot_term : state_.log.back().term;
}

std::uint64_t RaftNode::quorum_size() const noexcept {
    if (!state_.membership) return RaftMembershipState::majority(config_.members.size());
    const auto old_majority = RaftMembershipState::majority(state_.membership->voters().size());
    if (!state_.membership->joint()) return old_majority;
    return std::max(old_majority, RaftMembershipState::majority(state_.membership->new_voters().size()));
}

bool RaftNode::leader_ready() const noexcept {
    if (role_ != RaftRole::Leader) return false;
    if (state_.snapshot_index > config_.checkpoint_height &&
        state_.snapshot_index <= state_.commit_index && state_.snapshot_term == state_.current_term) {
        return true;
    }
    return std::any_of(state_.log.begin(), state_.log.end(), [&](const RaftLogEntry& entry) {
        return entry.index <= state_.commit_index && entry.term == state_.current_term;
    });
}

std::uint64_t RaftNode::term_at(std::uint64_t index) const {
    if (index == state_.snapshot_index) return state_.snapshot_term;
    if (index < state_.snapshot_index || index > last_log_index()) {
        throw std::out_of_range("Raft log index is outside the retained log");
    }
    const auto offset = static_cast<std::size_t>(index - state_.snapshot_index - 1U);
    return state_.log.at(offset).term;
}

std::uint64_t RaftNode::first_index_of_term(std::uint64_t term) const {
    if (term == state_.snapshot_term) return state_.snapshot_index;
    for (const auto& entry : state_.log) {
        if (entry.term == term) return entry.index;
    }
    return last_log_index() + 1U;
}

void RaftNode::become_follower(std::uint64_t new_term) {
    if (new_term < state_.current_term) return;
    const bool changed = new_term > state_.current_term || role_ != RaftRole::Follower;
    if (new_term > state_.current_term) {
        state_.current_term = new_term;
        state_.voted_for.clear();
    }
    role_ = RaftRole::Follower;
    leader_id_.clear();
    votes_received_.clear();
    next_index_.clear();
    match_index_.clear();
    snapshot_send_offset_.clear();
    clear_incoming_snapshot(true);
    outgoing_snapshot_buffer_.clear();
    outgoing_snapshot_index_ = 0;
    heartbeat_elapsed_ = std::chrono::milliseconds::zero();
    reset_election_timeout();
    if (changed) persist();
}

std::vector<RaftMessage> RaftNode::start_election() {
    if (!is_member(config_.node_id)) return {};
    if (state_.current_term == UINT64_MAX) throw std::overflow_error("Raft term exhausted");
    ++state_.current_term;
    role_ = RaftRole::Candidate;
    leader_id_.clear();
    state_.voted_for = config_.node_id;
    votes_received_.clear();
    for (const auto& member : state_.membership->replication_members()) {
        if (state_.membership->is_voter(member)) votes_received_[member] = false;
    }
    votes_received_[config_.node_id] = true;
    reset_election_timeout();
    persist();

    std::vector<RaftMessage> outbound;
    for (const auto& member : state_.membership->replication_members()) {
        if (member == config_.node_id || !state_.membership->is_voter(member)) continue;
        RaftMessage request;
        request.type = RaftMessageType::RequestVote;
        request.term = state_.current_term;
        request.from = config_.node_id;
        request.to = member;
        request.last_log_index = last_log_index();
        request.last_log_term = last_log_term();
        outbound.push_back(std::move(request));
    }
    return outbound;
}

std::vector<RaftMessage> RaftNode::become_leader() {
    if (!is_member(config_.node_id)) throw std::logic_error("a Raft learner cannot become leader");
    if (last_log_index() == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
    RaftPersistentState candidate = state_;
    candidate.log.push_back({last_log_index() + 1U, state_.current_term, {}});
    (void)state_body(candidate); // Enforce durable snapshot/log bounds before publishing leadership.

    role_ = RaftRole::Leader;
    leader_id_ = config_.node_id;
    heartbeat_elapsed_ = std::chrono::milliseconds::zero();
    const std::uint64_t next = last_log_index() + 1U;
    next_index_.clear();
    match_index_.clear();
    snapshot_send_offset_.clear();
    for (const auto& member : state_.membership->replication_members()) {
        if (member == config_.node_id) continue;
        next_index_[member] = next;
        match_index_[member] = config_.checkpoint_height;
    }
    state_ = std::move(candidate);
    match_index_[config_.node_id] = last_log_index();
    persist();
    if (state_.membership->phase() == MembershipPhase::JointCommitted) {
        append_final_membership_entry();
    }
    return broadcast_append_entries();
}

RaftMessage RaftNode::make_vote_response(const RaftMessage& request, bool granted) const {
    RaftMessage response;
    response.type = RaftMessageType::VoteResponse;
    response.term = state_.current_term;
    response.from = config_.node_id;
    response.to = request.from;
    response.vote_granted = granted;
    return response;
}

RaftMessage RaftNode::make_append_response(const RaftMessage& request, bool success,
                                           std::uint64_t match_index,
                                           std::uint64_t conflict_index,
                                           std::uint64_t conflict_term) const {
    RaftMessage response;
    response.type = RaftMessageType::AppendResponse;
    response.term = state_.current_term;
    response.from = config_.node_id;
    response.to = request.from;
    response.success = success;
    response.match_index = match_index;
    response.conflict_index = conflict_index;
    response.conflict_term = conflict_term;
    return response;
}

RaftMessage RaftNode::make_snapshot_request(const std::string& peer_id,
                                                    std::uint64_t offset) const {
    if (!read_snapshot_callback_ || state_.snapshot_index <= config_.checkpoint_height) {
        throw std::logic_error("Raft peer requires a snapshot but no durable snapshot reader is configured");
    }
    if (outgoing_snapshot_index_ != state_.snapshot_index || outgoing_snapshot_buffer_.empty()) {
        outgoing_snapshot_buffer_ = read_snapshot_callback_(state_.snapshot_index);
        if (outgoing_snapshot_buffer_.size() != state_.snapshot_size ||
            crypto::sha256(outgoing_snapshot_buffer_) != state_.snapshot_hash) {
            outgoing_snapshot_buffer_.clear();
            outgoing_snapshot_index_ = 0;
            throw std::runtime_error("durable Raft application snapshot is missing or failed its checksum");
        }
        outgoing_snapshot_index_ = state_.snapshot_index;
    }
    const Bytes& payload = outgoing_snapshot_buffer_;
    if (offset >= payload.size()) throw std::out_of_range("Raft snapshot transfer offset is outside the payload");
    const std::size_t begin = static_cast<std::size_t>(offset);
    const std::size_t count = std::min(kRaftSnapshotChunkBytes, payload.size() - begin);

    RaftMessage request;
    request.type = RaftMessageType::InstallSnapshot;
    request.term = state_.current_term;
    request.from = config_.node_id;
    request.to = peer_id;
    request.snapshot_index = state_.snapshot_index;
    request.snapshot_term = state_.snapshot_term;
    request.snapshot_offset = offset;
    request.snapshot_total_size = state_.snapshot_size;
    request.snapshot_hash = state_.snapshot_hash;
    if (state_.snapshot_membership) request.snapshot_membership = state_.snapshot_membership->serialize();
    if (!request.snapshot_membership.empty() &&
        request.snapshot_membership.size() > 8192U) {
        throw std::length_error("Raft snapshot membership metadata exceeds transfer limit");
    }
    request.snapshot_chunk.assign(payload.begin() + static_cast<std::ptrdiff_t>(begin),
                                  payload.begin() + static_cast<std::ptrdiff_t>(begin + count));
    return request;
}

RaftMessage RaftNode::make_snapshot_response(const RaftMessage& request,
                                              bool success,
                                              std::uint64_t next_offset) const {
    RaftMessage response;
    response.type = RaftMessageType::SnapshotResponse;
    response.term = state_.current_term;
    response.from = config_.node_id;
    response.to = request.from;
    response.success = success;
    response.snapshot_index = request.snapshot_index;
    response.snapshot_offset = next_offset;
    return response;
}

RaftMessage RaftNode::make_append_request(const std::string& peer_id) const {
    const auto next_it = next_index_.find(peer_id);
    if (next_it == next_index_.end()) throw std::logic_error("Raft peer next-index is not initialized");
    const std::uint64_t next = std::max(config_.checkpoint_height + 1U, next_it->second);
    if (next <= state_.snapshot_index) {
        const auto offset_it = snapshot_send_offset_.find(peer_id);
        const std::uint64_t offset = offset_it == snapshot_send_offset_.end() ? 0 : offset_it->second;
        return make_snapshot_request(peer_id, offset);
    }
    RaftMessage request;
    request.type = RaftMessageType::AppendEntries;
    request.term = state_.current_term;
    request.from = config_.node_id;
    request.to = peer_id;
    request.previous_log_index = next - 1U;
    request.previous_log_term = term_at(request.previous_log_index);
    request.leader_commit = state_.commit_index;
    const std::uint64_t last = last_log_index();
    if (next <= last) {
        const auto begin = static_cast<std::size_t>(next - state_.snapshot_index - 1U);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            kRaftMaximumEntriesPerMessage, last - next + 1U));
        request.entries.insert(request.entries.end(), state_.log.begin() + static_cast<std::ptrdiff_t>(begin),
                               state_.log.begin() + static_cast<std::ptrdiff_t>(begin + count));
    }
    return request;
}

std::vector<RaftMessage> RaftNode::receive_snapshot_response(const RaftMessage& message) {
    if (message.term > state_.current_term) {
        become_follower(message.term);
        return {};
    }
    if (message.term < state_.current_term || role_ != RaftRole::Leader ||
        message.snapshot_index != state_.snapshot_index || !next_index_.contains(message.from)) {
        return {};
    }
    if (message.snapshot_offset > state_.snapshot_size) {
        throw std::runtime_error("peer acknowledged an invalid Raft snapshot offset");
    }
    if (!message.success) {
        snapshot_send_offset_[message.from] = message.snapshot_offset;
        return {make_snapshot_request(message.from, message.snapshot_offset)};
    }
    snapshot_send_offset_[message.from] = message.snapshot_offset;
    if (message.snapshot_offset < state_.snapshot_size) {
        return {make_snapshot_request(message.from, message.snapshot_offset)};
    }
    snapshot_send_offset_.erase(message.from);
    match_index_[message.from] = std::max(match_index_[message.from], state_.snapshot_index);
    next_index_[message.from] = state_.snapshot_index + 1U;
    const auto old_commit = state_.commit_index;
    advance_commit_index();
    auto outbound = broadcast_append_entries();
    if (state_.commit_index != old_commit && outbound.empty()) {
        outbound = broadcast_append_entries();
    }
    // A leader removed by C_new must first advertise the committed final
    // configuration to the peers. Stepping down before this heartbeat would
    // strand every follower in FinalPending.
    if (state_.membership->stable() && !is_member(config_.node_id)) {
        role_ = RaftRole::Follower;
        leader_id_.clear();
    }
    return outbound;
}

void RaftNode::clear_incoming_snapshot(bool clear_durable_stage) {
    const bool had_transfer = incoming_snapshot_index_ != 0 || !incoming_snapshot_buffer_.empty();
    if (clear_durable_stage && had_transfer && clear_snapshot_stage_callback_) {
        clear_snapshot_stage_callback_();
    }
    incoming_snapshot_buffer_.clear();
    incoming_snapshot_membership_.clear();
    incoming_snapshot_index_ = 0;
    incoming_snapshot_from_.clear();
    incoming_snapshot_total_size_ = 0;
    incoming_snapshot_term_ = 0;
    incoming_snapshot_hash_ = {};
}

void RaftNode::complete_incoming_snapshot() {
    if (incoming_snapshot_index_ == 0 || incoming_snapshot_buffer_.empty() ||
        incoming_snapshot_buffer_.size() != incoming_snapshot_total_size_ ||
        crypto::sha256(incoming_snapshot_buffer_) != incoming_snapshot_hash_) {
        throw std::runtime_error("incoming Raft snapshot is incomplete or failed its checksum");
    }
    if (!install_snapshot_callback_) {
        throw std::logic_error("Raft snapshot receiver has no atomic application-install callback");
    }

    RaftPersistentState candidate = state_;
    bool retain_suffix = false;
    if (incoming_snapshot_index_ <= last_log_index()) {
        try { retain_suffix = term_at(incoming_snapshot_index_) == incoming_snapshot_term_; }
        catch (const std::out_of_range&) { retain_suffix = false; }
    }
    if (retain_suffix) {
        const auto remove_count = static_cast<std::size_t>(incoming_snapshot_index_ - state_.snapshot_index);
        candidate.log.erase(candidate.log.begin(), candidate.log.begin() + static_cast<std::ptrdiff_t>(remove_count));
    } else {
        candidate.log.clear();
    }
    candidate.snapshot_index = incoming_snapshot_index_;
    candidate.snapshot_term = incoming_snapshot_term_;
    candidate.snapshot_hash = incoming_snapshot_hash_;
    candidate.snapshot_size = incoming_snapshot_total_size_;
    if (!incoming_snapshot_membership_.empty()) {
        candidate.snapshot_membership = RaftMembershipState::deserialize(incoming_snapshot_membership_);
        candidate.membership = candidate.snapshot_membership;
    }
    candidate.commit_index = std::max(candidate.commit_index, incoming_snapshot_index_);
    candidate.last_applied = incoming_snapshot_index_;
    if (candidate.membership && !candidate.snapshot_membership) {
        candidate.snapshot_membership = candidate.membership;
    }
    refresh_membership_from_log(candidate);
    (void)state_body(candidate);
    try {
        install_snapshot_callback_(candidate, incoming_snapshot_buffer_);
        state_ = std::move(candidate);
        if (clear_snapshot_stage_callback_) clear_snapshot_stage_callback_();
    } catch (...) {
        // The complete transfer remains staged if atomic installation fails.
        // Startup will retry installation from the durable full payload.
        persistence_failed_ = true;
        throw;
    }
    clear_incoming_snapshot(false);
}

std::vector<RaftMessage> RaftNode::receive_install_snapshot(const RaftMessage& message) {
    if (!is_member(message.from)) return {make_snapshot_response(message, false, 0)};
    if (message.term < state_.current_term) {
        return {make_snapshot_response(message, false, 0)};
    }
    if (message.term > state_.current_term || role_ != RaftRole::Follower) {
        become_follower(message.term);
    } else {
        leader_id_ = message.from;
        reset_election_timeout();
    }
    leader_id_ = message.from;

    if (message.snapshot_index <= state_.commit_index) {
        if (incoming_snapshot_index_ != 0) clear_incoming_snapshot(true);
        return {make_snapshot_response(message, true, message.snapshot_total_size)};
    }

    const bool active_transfer = incoming_snapshot_index_ != 0;
    const bool same_transfer = active_transfer && incoming_snapshot_from_ == message.from &&
        incoming_snapshot_index_ == message.snapshot_index &&
        incoming_snapshot_term_ == message.snapshot_term &&
        incoming_snapshot_total_size_ == message.snapshot_total_size &&
        incoming_snapshot_hash_ == message.snapshot_hash &&
        incoming_snapshot_membership_ == message.snapshot_membership;

    if (!same_transfer) {
        if (message.snapshot_offset != 0) {
            const std::uint64_t expected = active_transfer
                ? static_cast<std::uint64_t>(incoming_snapshot_buffer_.size()) : 0;
            return {make_snapshot_response(message, false, expected)};
        }
        if (active_transfer) clear_incoming_snapshot(true);
        incoming_snapshot_index_ = message.snapshot_index;
        incoming_snapshot_from_ = message.from;
        incoming_snapshot_term_ = message.snapshot_term;
        incoming_snapshot_total_size_ = message.snapshot_total_size;
        incoming_snapshot_hash_ = message.snapshot_hash;
        incoming_snapshot_membership_ = message.snapshot_membership;
        incoming_snapshot_buffer_.clear();
        incoming_snapshot_buffer_.reserve(static_cast<std::size_t>(message.snapshot_total_size));
    }

    const std::uint64_t expected_offset = static_cast<std::uint64_t>(incoming_snapshot_buffer_.size());
    if (message.snapshot_offset != expected_offset) {
        return {make_snapshot_response(message, false, expected_offset)};
    }
    if (message.snapshot_chunk.empty() || message.snapshot_chunk.size() > kRaftSnapshotChunkBytes ||
        expected_offset > incoming_snapshot_total_size_ ||
        message.snapshot_chunk.size() > incoming_snapshot_total_size_ - expected_offset) {
        return {make_snapshot_response(message, false, expected_offset)};
    }

    if (save_snapshot_stage_callback_) {
        RaftSnapshotTransfer transfer;
        transfer.source_id = incoming_snapshot_from_;
        transfer.snapshot_index = incoming_snapshot_index_;
        transfer.snapshot_term = incoming_snapshot_term_;
        transfer.total_size = incoming_snapshot_total_size_;
        transfer.checksum = incoming_snapshot_hash_;
        transfer.membership = incoming_snapshot_membership_;
        try {
            // The chunk is durable before the response acknowledges its offset.
            save_snapshot_stage_callback_(transfer, expected_offset, message.snapshot_chunk);
        } catch (...) {
            persistence_failed_ = true;
            throw;
        }
    }
    incoming_snapshot_buffer_.insert(incoming_snapshot_buffer_.end(),
                                     message.snapshot_chunk.begin(), message.snapshot_chunk.end());
    const std::uint64_t next_offset = static_cast<std::uint64_t>(incoming_snapshot_buffer_.size());
    if (next_offset < incoming_snapshot_total_size_) {
        return {make_snapshot_response(message, true, next_offset)};
    }
    if (crypto::sha256(incoming_snapshot_buffer_) != incoming_snapshot_hash_) {
        clear_incoming_snapshot(true);
        return {make_snapshot_response(message, false, 0)};
    }
    complete_incoming_snapshot();
    return {make_snapshot_response(message, true, next_offset)};
}

std::vector<RaftMessage> RaftNode::broadcast_append_entries() const {
    std::vector<RaftMessage> outbound;
    if (role_ != RaftRole::Leader) return outbound;
    const auto peers = state_.membership->replication_members();
    outbound.reserve(peers.size() - 1U);
    for (const auto& member : peers) {
        if (member != config_.node_id) outbound.push_back(make_append_request(member));
    }
    return outbound;
}

void RaftNode::refresh_membership_from_log(RaftPersistentState& candidate) {
    if (!candidate.membership) throw std::logic_error("Raft persistent state has no membership state");
    auto& membership = *candidate.membership;

    std::optional<MembershipCommand> joint_command;
    std::optional<MembershipCommand> final_command;
    std::uint64_t joint_entry_index = 0;
    std::uint64_t final_entry_index = 0;
    for (const auto& entry : candidate.log) {
        if (entry.index <= membership.configuration_index() || entry.command.empty()) continue;
        const auto decoded = decode_membership_command(entry.command);
        if (!decoded) continue;
        if (decoded->kind == MembershipCommandKind::Joint) {
            if (joint_command) throw std::runtime_error("Raft log contains overlapping joint membership commands");
            joint_command = *decoded;
            joint_entry_index = entry.index;
        } else {
            if (final_command) throw std::runtime_error("Raft log contains overlapping final membership commands");
            final_command = *decoded;
            final_entry_index = entry.index;
        }
    }

    if (membership.phase() == MembershipPhase::JointPending &&
        membership.joint_index() > candidate.snapshot_index &&
        (!joint_command || joint_entry_index != membership.joint_index())) {
        membership.cancel_uncommitted_joint();
    }
    if (membership.phase() == MembershipPhase::FinalPending &&
        membership.final_index() > candidate.snapshot_index &&
        (!final_command || final_entry_index != membership.final_index())) {
        membership.cancel_uncommitted_final();
    }

    if (joint_command) {
        if (membership.stable()) {
            membership.begin_joint(joint_command->members, joint_entry_index);
        } else if (membership.joint_index() != joint_entry_index ||
                   membership.new_voters() != joint_command->members) {
            throw std::runtime_error("Raft log joint configuration conflicts with persisted membership transition");
        }
    }

    if (membership.phase() == MembershipPhase::JointPending &&
        candidate.commit_index >= membership.joint_index()) {
        membership.mark_joint_committed(candidate.commit_index);
    }

    if (final_command) {
        if (membership.phase() == MembershipPhase::JointCommitted) {
            if (membership.new_voters() != final_command->members) {
                throw std::runtime_error("Raft final configuration does not match the committed joint configuration");
            }
            membership.begin_final(final_entry_index);
        } else if (membership.phase() == MembershipPhase::FinalPending) {
            if (membership.final_index() != final_entry_index ||
                membership.new_voters() != final_command->members) {
                throw std::runtime_error("Raft log final configuration conflicts with persisted transition");
            }
        } else if (!membership.stable()) {
            throw std::runtime_error("Raft final configuration appeared before joint configuration commitment");
        } else {
            throw std::runtime_error("Raft log contains a final membership entry without a joint transition");
        }
    }

    if (membership.phase() == MembershipPhase::FinalPending &&
        candidate.commit_index >= membership.final_index()) {
        membership.mark_final_committed(candidate.commit_index);
    }
}

void RaftNode::append_final_membership_entry() {
    if (role_ != RaftRole::Leader || !state_.membership ||
        state_.membership->phase() != MembershipPhase::JointCommitted) return;
    if (last_log_index() == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
    const std::uint64_t index = last_log_index() + 1U;
    RaftPersistentState candidate = state_;
    candidate.membership->begin_final(index);
    candidate.log.push_back({index, state_.current_term,
        encode_membership_command(MembershipCommandKind::Final, candidate.membership->new_voters())});
    (void)state_body(candidate);
    state_ = std::move(candidate);
    match_index_[config_.node_id] = index;
    persist();
}

void RaftNode::advance_commit_index() {
    for (std::uint64_t candidate = last_log_index(); candidate > state_.commit_index; --candidate) {
        if (term_at(candidate) != state_.current_term) continue;
        std::set<std::string> acknowledgements{config_.node_id};
        for (const auto& member : state_.membership->replication_members()) {
            if (member == config_.node_id) continue;
            const auto found = match_index_.find(member);
            if (found != match_index_.end() && found->second >= candidate) acknowledgements.insert(member);
        }
        if (state_.membership->has_quorum(acknowledgements)) {
            state_.commit_index = candidate;
            refresh_membership_from_log(state_);
            persist();
            if (state_.membership->phase() == MembershipPhase::JointCommitted) {
                append_final_membership_entry();
            }
            break;
        }
    }
}

std::vector<RaftMessage> RaftNode::tick(std::chrono::milliseconds elapsed) {
    ensure_healthy();
    if (elapsed <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Raft tick duration must be positive");
    }
    if (role_ == RaftRole::Leader) {
        saturating_add(heartbeat_elapsed_, elapsed);
        if (heartbeat_elapsed_ >= config_.heartbeat_interval) {
            heartbeat_elapsed_ %= config_.heartbeat_interval;
            return broadcast_append_entries();
        }
        return {};
    }
    if (!is_member(config_.node_id)) {
        election_elapsed_ = std::chrono::milliseconds::zero();
        return {};
    }
    saturating_add(election_elapsed_, elapsed);
    if (election_elapsed_ >= election_timeout_current_) return start_election();
    return {};
}

std::vector<RaftMessage> RaftNode::receive(const RaftMessage& message) {
    ensure_healthy();
    validate_id(message.from, "message sender");
    validate_message_shape(message);
    if (message.term == 0) throw std::invalid_argument("Raft protocol messages must use a positive term");
    if (message.to != config_.node_id) throw std::invalid_argument("Raft message is addressed to another node");
    if (!is_replication_member(message.from) || message.from == config_.node_id) {
        throw std::invalid_argument("Raft message sender is not in the pre-provisioned voter/learner roster");
    }

    switch (message.type) {
    case RaftMessageType::RequestVote: {
        if (message.term < state_.current_term) return {make_vote_response(message, false)};
        if (message.term > state_.current_term) become_follower(message.term);
        const bool up_to_date = message.last_log_term > last_log_term() ||
            (message.last_log_term == last_log_term() && message.last_log_index >= last_log_index());
        const bool can_vote = state_.voted_for.empty() || state_.voted_for == message.from;
        const bool grant = is_member(config_.node_id) && is_member(message.from) && up_to_date && can_vote;
        if (grant) {
            if (state_.voted_for != message.from) {
                state_.voted_for = message.from;
                persist(); // Durable vote before sending a positive response.
            }
            reset_election_timeout();
        }
        return {make_vote_response(message, grant)};
    }
    case RaftMessageType::VoteResponse: {
        if (message.term > state_.current_term) {
            become_follower(message.term);
            return {};
        }
        if (message.term < state_.current_term || role_ != RaftRole::Candidate) return {};
        if (!is_member(message.from)) return {};
        if (message.vote_granted) votes_received_[message.from] = true;
        std::set<std::string> granted;
        for (const auto& [peer, yes] : votes_received_) if (yes) granted.insert(peer);
        if (state_.membership->has_quorum(granted)) return become_leader();
        return {};
    }
    case RaftMessageType::AppendEntries: {
        if (!is_member(message.from)) return {make_append_response(message, false, 0, last_log_index() + 1U)};
        if (message.term < state_.current_term) {
            return {make_append_response(message, false, 0, last_log_index() + 1U)};
        }
        if (message.term > state_.current_term || role_ != RaftRole::Follower) {
            become_follower(message.term);
        } else {
            leader_id_ = message.from;
            reset_election_timeout();
        }
        leader_id_ = message.from;

        if (message.previous_log_index < state_.snapshot_index) {
            return {make_append_response(message, false, 0, state_.snapshot_index + 1U)};
        }
        if (message.previous_log_index > last_log_index()) {
            return {make_append_response(message, false, 0, last_log_index() + 1U)};
        }
        const auto local_previous_term = term_at(message.previous_log_index);
        if (local_previous_term != message.previous_log_term) {
            return {make_append_response(message, false, 0,
                first_index_of_term(local_previous_term), local_previous_term)};
        }
        if (message.entries.size() > kRaftMaximumEntriesPerMessage) {
            throw std::length_error("Raft AppendEntries exceeds maximum entry count");
        }

        // Validate and stage the entire batch before changing the live log.
        // A malformed later entry must not leave earlier entries partially
        // applied in memory when the call throws.
        std::vector<RaftLogEntry> candidate_log = state_.log;
        std::uint64_t expected = message.previous_log_index;
        for (const auto& incoming : message.entries) {
            if (expected == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
            ++expected;
            if (incoming.index != expected || incoming.term == 0 || incoming.term > message.term ||
                incoming.command.size() > kRaftMaximumCommandBytes) {
                throw std::invalid_argument("Raft AppendEntries contains a non-canonical log entry");
            }
            const std::uint64_t candidate_last = state_.snapshot_index +
                static_cast<std::uint64_t>(candidate_log.size());
            if (incoming.index <= candidate_last) {
                const auto offset = static_cast<std::size_t>(incoming.index - state_.snapshot_index - 1U);
                const auto& existing = candidate_log.at(offset);
                if (existing.term != incoming.term || existing.command != incoming.command) {
                    if (incoming.index <= state_.commit_index) {
                        return {make_append_response(message, false, 0, state_.commit_index + 1U)};
                    }
                    candidate_log.resize(offset);
                    candidate_log.push_back(incoming);
                }
            } else {
                candidate_log.push_back(incoming);
            }
        }

        const std::uint64_t candidate_last = state_.snapshot_index +
            static_cast<std::uint64_t>(candidate_log.size());
        const std::uint64_t new_commit = std::min(message.leader_commit, candidate_last);
        const bool log_changed = candidate_log != state_.log;
        const bool commit_changed = new_commit > state_.commit_index;
        if (log_changed || commit_changed) {
            RaftPersistentState candidate_state = state_;
            candidate_state.log = std::move(candidate_log);
            candidate_state.commit_index = new_commit;
            refresh_membership_from_log(candidate_state);
            (void)state_body(candidate_state); // Check total snapshot bounds before publishing a staged append.
            state_ = std::move(candidate_state);
            persist();
        }
        return {make_append_response(message, true, expected, 0)};
    }
    case RaftMessageType::AppendResponse: {
        if (message.term > state_.current_term) {
            become_follower(message.term);
            return {};
        }
        if (message.term < state_.current_term || role_ != RaftRole::Leader) return {};
        if (!next_index_.contains(message.from)) return {};
        if (message.success) {
            const auto matched = std::min(message.match_index, last_log_index());
            match_index_[message.from] = std::max(match_index_[message.from], matched);
            next_index_[message.from] = match_index_[message.from] + 1U;
            const auto old_commit = state_.commit_index;
            advance_commit_index();
            if (state_.commit_index != old_commit) {
                auto outbound = broadcast_append_entries();
                if (state_.membership->stable() && !is_member(config_.node_id)) {
                    role_ = RaftRole::Follower;
                    leader_id_.clear();
                }
                return outbound;
            }
            return {};
        }
        const auto old_next = next_index_[message.from];
        std::uint64_t new_next = message.conflict_index == 0 ? (old_next > 1U ? old_next - 1U : 1U) : message.conflict_index;
        if (message.conflict_term != 0) {
            for (auto it = state_.log.rbegin(); it != state_.log.rend(); ++it) {
                if (it->term == message.conflict_term) {
                    new_next = it->index + 1U;
                    break;
                }
            }
        }
        new_next = std::clamp(new_next, config_.checkpoint_height + 1U, last_log_index() + 1U);
        next_index_[message.from] = new_next;
        return {make_append_request(message.from)};
    }
    case RaftMessageType::InstallSnapshot:
        return receive_install_snapshot(message);
    case RaftMessageType::SnapshotResponse:
        return receive_snapshot_response(message);
    }
    throw std::runtime_error("unsupported Raft message type");
}

std::vector<RaftMessage> RaftNode::propose(const Bytes& command) {
    ensure_healthy();
    if (role_ != RaftRole::Leader) throw std::logic_error("only the current Raft leader can propose a command");
    if (!leader_ready()) throw std::logic_error("Raft leader is not ready until its current-term no-op is committed");
    if (command.empty() || command.size() > kRaftMaximumCommandBytes) {
        throw std::invalid_argument("Raft application command is empty or exceeds 1 MiB");
    }
    if (is_membership_command(command)) {
        throw std::invalid_argument("reserved Raft membership commands cannot be proposed as application data");
    }
    if (last_log_index() == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
    const std::uint64_t index = last_log_index() + 1U;
    RaftPersistentState candidate = state_;
    candidate.log.push_back({index, state_.current_term, command});
    (void)state_body(candidate); // Reject capacity exhaustion before changing the live log.
    state_ = std::move(candidate);
    match_index_[config_.node_id] = index;
    persist();
    return broadcast_append_entries();
}

std::vector<RaftMessage> RaftNode::propose_membership_change(
    const std::vector<std::string>& new_members) {
    ensure_healthy();
    if (role_ != RaftRole::Leader || !leader_ready()) {
        throw std::logic_error("only a ready Raft leader can propose a membership change");
    }
    if (!state_.membership || !state_.membership->stable()) {
        throw std::logic_error("another Raft membership transition is already in progress");
    }
    if (last_log_index() == UINT64_MAX) throw std::overflow_error("Raft log index exhausted");
    std::vector<std::string> canonical = new_members;
    std::sort(canonical.begin(), canonical.end());
    // A new voter must first be caught up as a learner. Otherwise the cluster
    // would enter a joint quorum that includes a node missing committed history.
    for (const auto& voter : canonical) {
        if (state_.membership->is_voter(voter)) continue;
        const auto matched = match_index_.find(voter);
        if (matched == match_index_.end() || matched->second < state_.commit_index) {
            throw std::logic_error("new Raft voter is not caught up to the leader's committed index: " + voter);
        }
    }
    const std::uint64_t index = last_log_index() + 1U;
    RaftPersistentState candidate = state_;
    candidate.membership->begin_joint(canonical, index);
    candidate.log.push_back({index, state_.current_term,
        encode_membership_command(MembershipCommandKind::Joint, canonical)});
    (void)state_body(candidate);
    state_ = std::move(candidate);
    match_index_[config_.node_id] = index;
    persist();
    return broadcast_append_entries();
}

std::vector<RaftLogEntry> RaftNode::committed_entries() const {
    std::vector<RaftLogEntry> result;
    if (state_.last_applied >= state_.commit_index) return result;
    const auto first = static_cast<std::size_t>(state_.last_applied - state_.snapshot_index);
    const auto last = static_cast<std::size_t>(state_.commit_index - state_.snapshot_index);
    result.insert(result.end(), state_.log.begin() + static_cast<std::ptrdiff_t>(first),
                  state_.log.begin() + static_cast<std::ptrdiff_t>(last));
    return result;
}

void RaftNode::mark_applied(std::uint64_t index) {
    ensure_healthy();
    if (state_.last_applied == UINT64_MAX || index != state_.last_applied + 1U ||
        index > state_.commit_index || index <= state_.snapshot_index) {
        throw std::invalid_argument("Raft applied index must advance exactly one committed entry");
    }
    state_.last_applied = index;
    persist();
}

void RaftNode::mark_applied_atomically(
    std::uint64_t index, const RaftPersistCallback& atomic_commit) {
    ensure_healthy();
    if (!atomic_commit) throw std::invalid_argument("atomic Raft application requires a commit callback");
    if (state_.last_applied == UINT64_MAX || index != state_.last_applied + 1U ||
        index > state_.commit_index || index <= state_.snapshot_index) {
        throw std::invalid_argument("Raft applied index must advance exactly one committed entry");
    }

    RaftPersistentState candidate = state_;
    candidate.last_applied = index;
    (void)state_body(candidate);
    try {
        // The callback must atomically persist all application effects and the
        // supplied Raft snapshot. Publish the new in-memory state only afterward.
        atomic_commit(candidate);
    } catch (...) {
        persistence_failed_ = true;
        throw;
    }
    state_ = std::move(candidate);
}


bool RaftNode::compact_applied(const Bytes& application_snapshot) {
    ensure_healthy();
    if (state_.last_applied <= state_.snapshot_index) return false;
    if (state_.membership && (state_.membership->joint() ||
        state_.membership->configuration_index() > state_.last_applied)) {
        return false; // Snapshot configuration must describe exactly the included applied prefix.
    }
    if (!save_snapshot_callback_) {
        throw std::logic_error("Raft compaction requires a durable application snapshot writer");
    }
    if (application_snapshot.empty() || application_snapshot.size() > kRaftMaximumSnapshotBytes) {
        throw std::length_error("Raft application snapshot is empty or exceeds the 64 MiB limit");
    }

    const std::uint64_t included_index = state_.last_applied;
    const std::uint64_t included_term = term_at(included_index);
    const Hash snapshot_hash = crypto::sha256(application_snapshot);
    // Write the immutable snapshot object first. A crash before the Raft state
    // update leaves only an unreachable orphan; the old log remains authoritative.
    save_snapshot_callback_(included_index, application_snapshot);

    RaftPersistentState candidate = state_;
    const auto remove_count = static_cast<std::size_t>(included_index - state_.snapshot_index);
    if (remove_count > candidate.log.size()) {
        throw std::logic_error("Raft compaction boundary exceeds retained log");
    }
    candidate.log.erase(candidate.log.begin(),
                        candidate.log.begin() + static_cast<std::ptrdiff_t>(remove_count));
    candidate.snapshot_index = included_index;
    candidate.snapshot_term = included_term;
    candidate.snapshot_hash = snapshot_hash;
    candidate.snapshot_size = static_cast<std::uint64_t>(application_snapshot.size());
    candidate.snapshot_membership = candidate.membership;
    (void)state_body(candidate);
    state_ = std::move(candidate);
    snapshot_send_offset_.clear();
    outgoing_snapshot_buffer_.clear();
    outgoing_snapshot_index_ = 0;
    persist();
    return true;
}

} // namespace feinstat::consensus
