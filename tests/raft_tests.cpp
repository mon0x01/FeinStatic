#include "test.hpp"

#include "feinstat/consensus/raft.hpp"
#include "feinstat/common/bytes.hpp"
#include "feinstat/consensus/raft_storage.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <map>
#include <queue>
#include <sqlite3.h>
#include <string>
#include <vector>

using namespace feinstat;
using namespace feinstat::consensus;
using namespace feinstat::ledger;

namespace {

std::string raft_test_db(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        (std::string("feinstatic-raft-") + suffix + "-" + std::to_string(stamp) + ".db")).string();
}

void remove_raft_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

RaftConfig raft_config(const std::string& node, std::uint64_t seed,
                       std::uint64_t checkpoint = 0, Hash checkpoint_hash = {}) {
    RaftConfig config;
    config.node_id = node;
    config.cluster_id = "feinstatic-test-cluster";
    config.members = {"node-a", "node-b", "node-c"};
    config.checkpoint_height = checkpoint;
    config.checkpoint_hash = checkpoint_hash;
    config.election_timeout_min = std::chrono::milliseconds(180);
    config.election_timeout_max = std::chrono::milliseconds(360);
    config.heartbeat_interval = std::chrono::milliseconds(40);
    config.random_seed = seed;
    return config;
}

class SimulatedRaftCluster {
public:
    SimulatedRaftCluster() {
        const std::vector<std::string> ids{"node-a", "node-b", "node-c"};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            persisted_.emplace_back();
            nodes_.push_back(std::make_unique<RaftNode>(raft_config(ids[i], i + 1U), std::nullopt,
                [this, i](const RaftPersistentState& state) { persisted_[i] = state; }));
        }
        links_.assign(3, std::vector<bool>(3, true));
        for (std::size_t i = 0; i < 3; ++i) links_[i][i] = false;
    }

    RaftNode& node(std::size_t index) { return *nodes_.at(index); }
    const RaftNode& node(std::size_t index) const { return *nodes_.at(index); }

    void set_link(std::size_t a, std::size_t b, bool enabled) {
        links_.at(a).at(b) = enabled;
        links_.at(b).at(a) = enabled;
    }

    void partition_leader(std::size_t leader) {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (i != leader) set_link(leader, i, false);
        }
    }

    void heal() {
        for (std::size_t i = 0; i < nodes_.size(); ++i)
            for (std::size_t j = 0; j < nodes_.size(); ++j)
                links_[i][j] = i != j;
    }

    std::size_t propose(std::size_t index, const Bytes& command) {
        auto messages = nodes_.at(index)->propose(command);
        const std::size_t count = messages.size();
        enqueue(std::move(messages));
        return count;
    }

    void advance(std::chrono::milliseconds step = std::chrono::milliseconds(10)) {
        for (auto& node_ptr : nodes_) enqueue(node_ptr->tick(step));
        std::size_t delivered = 0;
        while (!queue_.empty()) {
            if (++delivered > 10000) throw std::runtime_error("Raft simulator exceeded message delivery bound");
            RaftMessage message = std::move(queue_.front());
            queue_.pop();
            const std::size_t from = index_of(message.from);
            const std::size_t to = index_of(message.to);
            if (!links_[from][to]) continue;
            enqueue(nodes_[to]->receive(message));
        }
    }

    std::size_t elect_leader() {
        for (int i = 0; i < 200; ++i) {
            advance();
            const auto found = leader_index();
            if (found.has_value() && nodes_[*found]->leader_ready()) return *found;
        }
        throw std::runtime_error("Raft simulator failed to elect a ready leader");
    }

    std::optional<std::size_t> leader_index() const {
        std::optional<std::size_t> result;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i]->role() != RaftRole::Leader) continue;
            if (result.has_value()) throw std::runtime_error("Raft simulator observed two leaders in one active term");
            result = i;
        }
        return result;
    }

    void restart(std::size_t index) {
        const std::string id = index == 0 ? "node-a" : index == 1 ? "node-b" : "node-c";
        const std::uint64_t seed = index + 1U;
        nodes_[index] = std::make_unique<RaftNode>(raft_config(id, seed), persisted_[index],
            [this, index](const RaftPersistentState& state) { persisted_[index] = state; });
    }

private:
    void enqueue(std::vector<RaftMessage> messages) {
        for (auto& message : messages) queue_.push(std::move(message));
    }

    std::size_t index_of(const std::string& id) const {
        if (id == "node-a") return 0;
        if (id == "node-b") return 1;
        if (id == "node-c") return 2;
        throw std::runtime_error("unknown Raft node in simulated message");
    }

    std::vector<RaftPersistentState> persisted_;
    std::vector<std::unique_ptr<RaftNode>> nodes_;
    std::vector<std::vector<bool>> links_;
    std::queue<RaftMessage> queue_;
};

} // namespace

FS_TEST(raft_requires_static_odd_membership_and_valid_timing) {
    auto config = raft_config("node-a", 1);
    config.members = {"node-a", "node-b"};
    FS_REQUIRE_THROWS(RaftNode(config));
    config = raft_config("node-a", 1);
    config.members = {"node-a", "node-b", "node-b"};
    FS_REQUIRE_THROWS(RaftNode(config));
    config = raft_config("node-a", 1);
    config.heartbeat_interval = std::chrono::milliseconds(100);
    FS_REQUIRE_THROWS(RaftNode(config));
    config = raft_config("node-x", 1);
    FS_REQUIRE_THROWS(RaftNode(config));
}

FS_TEST(raft_message_round_trip_preserves_vote_and_append_payloads) {
    RaftMessage message;
    message.type = RaftMessageType::AppendEntries;
    message.term = 12;
    message.from = "node-a";
    message.to = "node-b";
    message.previous_log_index = 90;
    message.previous_log_term = 8;
    message.leader_commit = 89;
    message.entries = {{91, 9, Bytes{1, 2, 3}}, {92, 9, Bytes{4, 5}}};
    const auto decoded = RaftMessage::deserialize(message.serialize());
    FS_REQUIRE(decoded.type == message.type);
    FS_REQUIRE(decoded.term == 12);
    FS_REQUIRE(decoded.from == "node-a");
    FS_REQUIRE(decoded.to == "node-b");
    FS_REQUIRE(decoded.previous_log_index == 90);
    FS_REQUIRE(decoded.leader_commit == 89);
    FS_REQUIRE(decoded.entries == message.entries);
    Bytes corrupted = message.serialize();
    const std::size_t entry_payload_offset = corrupted.size() - 72U - message.entries.back().command.size();
    corrupted.at(entry_payload_offset) ^= 0x01U;
    FS_REQUIRE(RaftMessage::deserialize(corrupted).entries != message.entries);
}

FS_TEST(raft_compacted_snapshot_transfers_in_bounded_chunks_and_installs_atomically) {
    auto leader_config = raft_config("node-a", 1);
    auto follower_config = raft_config("node-b", 2);
    auto lagging_config = raft_config("node-c", 3);

    RaftPersistentState leader_disk;
    RaftPersistentState follower_disk;
    RaftPersistentState lagging_disk;
    Bytes durable_snapshot;
    Bytes installed_snapshot;
    bool install_committed = false;

    RaftNode leader(leader_config, std::nullopt,
        [&](const RaftPersistentState& state) { leader_disk = state; },
        [&](std::uint64_t index) {
            if (index != leader_disk.snapshot_index || durable_snapshot.empty()) {
                throw std::runtime_error("missing test snapshot");
            }
            return durable_snapshot;
        },
        [&](std::uint64_t index, const Bytes& payload) {
            if (index != 2) throw std::runtime_error("unexpected test snapshot index");
            durable_snapshot = payload;
        });
    RaftNode follower(follower_config, std::nullopt,
        [&](const RaftPersistentState& state) { follower_disk = state; });
    auto make_lagging_node = [&]() {
        return std::make_unique<RaftNode>(lagging_config, lagging_disk,
            [&](const RaftPersistentState& state) { lagging_disk = state; },
            RaftSnapshotReadCallback{}, RaftSnapshotSaveCallback{},
            [&](const RaftPersistentState& state, const Bytes& payload) {
                lagging_disk = state;
                installed_snapshot = payload;
                install_committed = true;
            });
    };
    auto lagging = std::make_unique<RaftNode>(lagging_config, std::nullopt,
        [&](const RaftPersistentState& state) { lagging_disk = state; },
        RaftSnapshotReadCallback{}, RaftSnapshotSaveCallback{},
        [&](const RaftPersistentState& state, const Bytes& payload) {
            lagging_disk = state;
            installed_snapshot = payload;
            install_committed = true;
        });

    auto vote_requests = leader.tick(std::chrono::milliseconds(500));
    const auto vote_request = std::find_if(vote_requests.begin(), vote_requests.end(),
        [](const RaftMessage& message) { return message.to == "node-b"; });
    FS_REQUIRE(vote_request != vote_requests.end());
    auto vote_responses = follower.receive(*vote_request);
    FS_REQUIRE(vote_responses.size() == 1);
    auto leader_messages = leader.receive(vote_responses.front());
    FS_REQUIRE(leader.role() == RaftRole::Leader);

    auto deliver_to_follower = [&](const std::vector<RaftMessage>& messages) {
        for (const auto& message : messages) {
            if (message.to != "node-b") continue;
            for (const auto& response : follower.receive(message)) {
                (void)leader.receive(response);
            }
        }
    };
    deliver_to_follower(leader_messages);
    // Commit the leader's current-term no-op on a majority.
    auto heartbeat = leader.tick(std::chrono::milliseconds(100));
    deliver_to_follower(heartbeat);
    FS_REQUIRE(leader.leader_ready());

    auto proposal_messages = leader.propose(Bytes{0x41, 0x42, 0x43});
    deliver_to_follower(proposal_messages);
    auto commit_messages = leader.tick(std::chrono::milliseconds(100));
    deliver_to_follower(commit_messages);
    FS_REQUIRE(leader.persistent_state().commit_index == 2);
    leader.mark_applied(1);
    leader.mark_applied(2);

    Bytes application_snapshot(1U * 1024U * 1024U + 12345U);
    for (std::size_t i = 0; i < application_snapshot.size(); ++i) {
        application_snapshot[i] = static_cast<Byte>((i * 31U) & 0xffU);
    }
    FS_REQUIRE(leader.compact_applied(application_snapshot));
    FS_REQUIRE(leader.snapshot_index() == 2);
    FS_REQUIRE(leader.persistent_state().log.empty());
    FS_REQUIRE(leader.leader_ready());
    FS_REQUIRE(durable_snapshot == application_snapshot);

    std::queue<RaftMessage> to_lagging;
    for (auto& message : leader.tick(std::chrono::milliseconds(100))) {
        if (message.to == "node-c") to_lagging.push(std::move(message));
    }
    std::size_t chunks_received = 0;
    std::size_t transfer_restarts = 0;
    bool receiver_restarted = false;
    while (!to_lagging.empty()) {
        RaftMessage message = std::move(to_lagging.front());
        to_lagging.pop();
        FS_REQUIRE(message.type == RaftMessageType::InstallSnapshot ||
                   message.type == RaftMessageType::AppendEntries);
        if (message.type == RaftMessageType::InstallSnapshot) {
            FS_REQUIRE(message.snapshot_chunk.size() <= kRaftSnapshotChunkBytes);
            ++chunks_received;
        }
        const auto response_messages = lagging->receive(message);
        for (const auto& response : response_messages) {
            FS_REQUIRE(response.type == RaftMessageType::SnapshotResponse ||
                       response.type == RaftMessageType::AppendResponse);
            for (auto& next : leader.receive(response)) {
                if (next.to == "node-c") to_lagging.push(std::move(next));
            }
            // Lose the partially received snapshot between chunks. The new
            // process has only its durable pre-snapshot Raft state, so the
            // leader must observe offset zero and restart the bounded transfer.
            if (!receiver_restarted && chunks_received == 1 &&
                message.type == RaftMessageType::InstallSnapshot) {
                lagging = make_lagging_node();
                receiver_restarted = true;
                ++transfer_restarts;
            }
        }
    }

    FS_REQUIRE(chunks_received == 5); // One discarded suffix chunk plus a full three-chunk retry.
    FS_REQUIRE(transfer_restarts == 1);
    FS_REQUIRE(install_committed);
    FS_REQUIRE(installed_snapshot == application_snapshot);
    FS_REQUIRE(lagging->snapshot_index() == 2);
    FS_REQUIRE(lagging->persistent_state().last_applied == 2);
    FS_REQUIRE(lagging->persistent_state().commit_index == 2);
}

FS_TEST(raft_persistent_snapshot_round_trip_and_checksum_tamper_detection) {
    RaftPersistentState state;
    state.checkpoint_height = 10;
    state.checkpoint_hash[0] = 1;
    state.configuration_hash[0] = 2;
    state.current_term = 5;
    state.voted_for = "node-a";
    state.snapshot_index = 11;
    state.snapshot_term = 4;
    state.snapshot_hash[0] = 0x5a;
    state.snapshot_size = 1234;
    state.log = {{12, 4, Bytes{1}}, {13, 5, Bytes{2, 3}}};
    state.commit_index = 12;
    state.last_applied = 11;
    const Bytes encoded = state.serialize();
    const auto decoded = RaftPersistentState::deserialize(encoded);
    FS_REQUIRE(decoded.checkpoint_height == 10);
    FS_REQUIRE(decoded.current_term == 5);
    FS_REQUIRE(decoded.voted_for == "node-a");
    FS_REQUIRE(decoded.snapshot_index == 11);
    FS_REQUIRE(decoded.snapshot_term == 4);
    FS_REQUIRE(decoded.snapshot_hash == state.snapshot_hash);
    FS_REQUIRE(decoded.snapshot_size == 1234);
    FS_REQUIRE(decoded.log == state.log);
    FS_REQUIRE(decoded.commit_index == 12);
    Bytes corrupt = encoded;
    corrupt[corrupt.size() / 2U] ^= 0x01U;
    FS_REQUIRE_THROWS(RaftPersistentState::deserialize(corrupt));
}

FS_TEST(raft_v1_persistent_state_and_message_encodings_remain_readable) {
    ByteWriter state_body_writer;
    Hash checkpoint_hash{};
    checkpoint_hash[0] = 0x21;
    Hash configuration_hash{};
    configuration_hash[0] = 0x34;
    state_body_writer.fixed(checkpoint_hash);
    state_body_writer.u64(10);
    state_body_writer.fixed(configuration_hash);
    state_body_writer.u64(4);
    state_body_writer.string("node-a");
    state_body_writer.u64(11);
    state_body_writer.u64(10);
    state_body_writer.u32(1);
    state_body_writer.u64(11);
    state_body_writer.u64(4);
    state_body_writer.blob(Bytes{0xaa, 0xbb});
    const Bytes legacy_state_body = std::move(state_body_writer).take();
    ByteWriter state_envelope;
    state_envelope.fixed(std::array<Byte, 4>{'F', 'S', 'R', 'S'});
    state_envelope.u8(1);
    state_envelope.blob(legacy_state_body);
    state_envelope.fixed(crypto::sha256(legacy_state_body));
    const auto legacy_state = RaftPersistentState::deserialize(std::move(state_envelope).take());
    FS_REQUIRE(legacy_state.checkpoint_height == 10);
    FS_REQUIRE(legacy_state.snapshot_index == 10);
    FS_REQUIRE(legacy_state.snapshot_size == 0);
    FS_REQUIRE(legacy_state.log.size() == 1);
    FS_REQUIRE(legacy_state.log.front().index == 11);

    ByteWriter message_writer;
    message_writer.fixed(std::array<Byte, 4>{'F', 'S', 'R', 'M'});
    message_writer.u8(1);
    message_writer.u8(static_cast<Byte>(RaftMessageType::RequestVote));
    message_writer.u64(4);
    message_writer.string("node-a");
    message_writer.string("node-b");
    message_writer.u64(11);
    message_writer.u64(4);
    message_writer.u8(0);
    message_writer.u64(0);
    message_writer.u64(0);
    message_writer.u64(0);
    message_writer.u8(0);
    message_writer.u64(0);
    message_writer.u64(0);
    message_writer.u64(0);
    message_writer.u32(0);
    const auto legacy_message = RaftMessage::deserialize(std::move(message_writer).take());
    FS_REQUIRE(legacy_message.type == RaftMessageType::RequestVote);
    FS_REQUIRE(legacy_message.term == 4);
    FS_REQUIRE(legacy_message.last_log_index == 11);
    FS_REQUIRE(legacy_message.last_log_term == 4);
}

FS_TEST(raft_elects_exactly_one_leader_and_commits_current_term_noop) {
    SimulatedRaftCluster cluster;
    const auto leader = cluster.elect_leader();
    FS_REQUIRE(cluster.node(leader).role() == RaftRole::Leader);
    FS_REQUIRE(cluster.node(leader).leader_ready());
    FS_REQUIRE(cluster.node(leader).persistent_state().commit_index == 1);
    for (std::size_t i = 0; i < 3; ++i) {
        FS_REQUIRE(cluster.node(i).persistent_state().commit_index == 1);
        FS_REQUIRE(cluster.node(i).persistent_state().last_applied == 0);
    }
    for (int i = 0; i < 20; ++i) cluster.advance();
    FS_REQUIRE(cluster.leader_index().has_value());
}

FS_TEST(raft_replicates_and_commits_command_only_after_majority_acknowledges) {
    SimulatedRaftCluster cluster;
    const auto leader = cluster.elect_leader();
    const Bytes command{0x42, 0x46, 0x53};
    const auto outbound_count = cluster.propose(leader, command);
    FS_REQUIRE(outbound_count == 2);
    // Delivering the outbound requests and responses is the deterministic network.
    // A single advance drives heartbeats too; the command is committed only after both followers ack.
    for (int i = 0; i < 3; ++i) cluster.advance();
    const auto committed = cluster.node(leader).committed_entries();
    FS_REQUIRE(committed.size() == 2); // Current-term no-op, followed by the command.
    FS_REQUIRE(committed.back().command == command);
    FS_REQUIRE(committed.back().index == 2);
    for (std::size_t i = 0; i < 3; ++i) {
        FS_REQUIRE(cluster.node(i).persistent_state().commit_index == 2);
    }
    cluster.node(leader).mark_applied(1);
    cluster.node(leader).mark_applied(2);
    FS_REQUIRE(cluster.node(leader).persistent_state().last_applied == 2);
    FS_REQUIRE_THROWS(cluster.node(leader).mark_applied(4));
}

FS_TEST(raft_three_node_minority_cannot_commit_and_majority_partition_recovers) {
    SimulatedRaftCluster cluster;
    const auto old_leader = cluster.elect_leader();
    const auto committed_before = cluster.node(old_leader).persistent_state().commit_index;
    cluster.partition_leader(old_leader);

    const Bytes minority_command{0xaa, 0xbb};
    (void)cluster.propose(old_leader, minority_command);
    for (int i = 0; i < 60; ++i) cluster.advance();
    FS_REQUIRE(cluster.node(old_leader).persistent_state().commit_index == committed_before);

    std::optional<std::size_t> replacement;
    for (int i = 0; i < 100; ++i) {
        cluster.advance();
        for (std::size_t j = 0; j < 3; ++j) {
            if (j != old_leader && cluster.node(j).role() == RaftRole::Leader && cluster.node(j).leader_ready()) {
                replacement = j;
            }
        }
        if (replacement.has_value()) break;
    }
    FS_REQUIRE(replacement.has_value());
    const Bytes majority_command{0xcc, 0xdd};
    (void)cluster.propose(*replacement, majority_command);
    for (int i = 0; i < 5; ++i) cluster.advance();
    FS_REQUIRE(cluster.node(*replacement).persistent_state().commit_index >= committed_before + 1U);

    cluster.heal();
    for (int i = 0; i < 80; ++i) cluster.advance();
    const auto final_leader = cluster.leader_index();
    FS_REQUIRE(final_leader.has_value());
    const auto final_index = cluster.node(*final_leader).persistent_state().commit_index;
    for (std::size_t i = 0; i < 3; ++i) {
        FS_REQUIRE(cluster.node(i).persistent_state().commit_index == final_index);
        FS_REQUIRE(cluster.node(i).persistent_state().log == cluster.node(*final_leader).persistent_state().log);
    }
    const auto& log = cluster.node(*final_leader).persistent_state().log;
    FS_REQUIRE(std::any_of(log.begin(), log.end(), [&](const RaftLogEntry& entry) {
        return entry.command == majority_command;
    }));
    FS_REQUIRE(std::none_of(log.begin(), log.end(), [&](const RaftLogEntry& entry) {
        return entry.command == minority_command;
    }));
}

FS_TEST(raft_vote_is_persisted_before_grant_and_prevents_double_vote_after_restart) {
    auto config = raft_config("node-b", 22);
    RaftPersistentState disk;
    bool saved = false;
    RaftNode node(config, std::nullopt, [&](const RaftPersistentState& state) { disk = state; saved = true; });

    RaftMessage first;
    first.type = RaftMessageType::RequestVote;
    first.term = 1;
    first.from = "node-a";
    first.to = "node-b";
    first.last_log_index = 0;
    first.last_log_term = 0;
    const auto first_response = node.receive(first);
    FS_REQUIRE(first_response.size() == 1);
    FS_REQUIRE(first_response.front().vote_granted);
    FS_REQUIRE(saved);
    FS_REQUIRE(disk.voted_for == "node-a");

    RaftNode recovered(config, disk);
    RaftMessage second = first;
    second.from = "node-c";
    const auto second_response = recovered.receive(second);
    FS_REQUIRE(second_response.size() == 1);
    FS_REQUIRE(!second_response.front().vote_granted);
    FS_REQUIRE(recovered.persistent_state().voted_for == "node-a");
}

FS_TEST(raft_candidate_with_stale_log_is_denied_vote) {
    auto config = raft_config("node-b", 3);
    RaftPersistentState state;
    state.checkpoint_height = config.checkpoint_height;
    state.checkpoint_hash = config.checkpoint_hash;
    // A valid local log from a more recent term.
    RaftNode local(config, std::nullopt);
    // Rebuild a state with one durable entry, simulating a node that has seen a newer log.
    state = local.persistent_state();
    state.current_term = 2;
    state.log.push_back({1, 2, Bytes{9}});
    state.commit_index = 0;
    state.last_applied = 0;
    RaftNode recovered(config, state);
    RaftMessage stale;
    stale.type = RaftMessageType::RequestVote;
    stale.term = 3;
    stale.from = "node-a";
    stale.to = "node-b";
    stale.last_log_index = 0;
    stale.last_log_term = 0;
    const auto response = recovered.receive(stale);
    FS_REQUIRE(response.size() == 1);
    FS_REQUIRE(!response.front().vote_granted);
}

FS_TEST(raft_restart_recovers_term_vote_and_uncommitted_log) {
    auto config = raft_config("node-a", 1);
    RaftPersistentState disk;
    RaftNode node(config, std::nullopt, [&](const RaftPersistentState& state) { disk = state; });
    // A peer's higher term forces durable step-down; the vote is then persisted.
    RaftMessage request;
    request.type = RaftMessageType::RequestVote;
    request.term = 4;
    request.from = "node-b";
    request.to = "node-a";
    request.last_log_index = 0;
    request.last_log_term = 0;
    const auto response = node.receive(request);
    FS_REQUIRE(response.front().vote_granted);
    FS_REQUIRE(disk.current_term == 4);
    FS_REQUIRE(disk.voted_for == "node-b");
    RaftNode recovered(config, disk);
    FS_REQUIRE(recovered.persistent_state().current_term == 4);
    FS_REQUIRE(recovered.persistent_state().voted_for == "node-b");
    FS_REQUIRE(recovered.role() == RaftRole::Follower);
}

FS_TEST(raft_checkpoint_binds_state_to_exact_ledger_prefix) {
    Hash first_hash{};
    first_hash[0] = 1;
    Hash second_hash{};
    second_hash[0] = 2;
    auto config = raft_config("node-a", 1, 55, first_hash);
    RaftNode node(config);
    const auto state = node.persistent_state();
    auto wrong_hash_config = raft_config("node-a", 1, 55, second_hash);
    FS_REQUIRE_THROWS(RaftNode(wrong_hash_config, state));
    auto wrong_membership = raft_config("node-a", 1, 55, first_hash);
    wrong_membership.members = {"node-a", "node-b", "node-x"};
    FS_REQUIRE_THROWS(RaftNode(wrong_membership, state));
}

FS_TEST(raft_sqlite_snapshot_is_durable_revisioned_and_migrates_schema_v3_to_v6) {
    const std::string path = raft_test_db("snapshot");
    const Wallet wallet = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{wallet.address(), 10 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    }
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int migration_setup = sqlite3_exec(db,
        "DROP TABLE raft_state; PRAGMA user_version=3;", nullptr, nullptr, &error);
    const std::string migration_error = error ? error : "";
    sqlite3_free(error);
    sqlite3_close(db);
    FS_REQUIRE(migration_setup == SQLITE_OK);
    (void)migration_error;

    {
        RaftStateStore store(path);
        auto config = raft_config("node-a", 10, 0, Ledger::open(path, 0, kAtomsPerCoin).chain().blocks().front().hash);
        RaftNode node(config, store.load(), [&](const RaftPersistentState& state) { store.save(state); });
        FS_REQUIRE(store.revision() == 1);
        FS_REQUIRE(node.persistent_state().commit_index == 0);
        FS_REQUIRE(node.tick(std::chrono::milliseconds(500)).size() == 2);
        FS_REQUIRE(store.revision() >= 2);
        const auto loaded = store.load();
        FS_REQUIRE(loaded.has_value());
        FS_REQUIRE(loaded->current_term == 1);
        FS_REQUIRE(loaded->voted_for == "node-a");
    }
    db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int(statement, 0) == 6);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    auto recovered = Ledger::open(path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 0);
    remove_raft_db(path);
}

FS_TEST(raft_sqlite_schema_v5_to_v6_adds_staging_tables_without_rewriting_existing_rows) {
    const std::string path = raft_test_db("schema-v5-to-v6");
    const Wallet wallet = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{wallet.address(), 10 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    }

    RaftPersistentState initial_state;
    initial_state.configuration_hash[0] = 0x5a;
    const Bytes completed_snapshot{0x46, 0x53, 0x4c, 0x53, 0x01, 0x02, 0x03};
    {
        RaftStateStore store(path);
        store.save(initial_state);
        store.save_snapshot(1, completed_snapshot);
        FS_REQUIRE(store.load_snapshot(1) == completed_snapshot);
    }

    // Emulate the exact v1.9.0 on-disk schema: all existing v5 tables/data are
    // retained, while the two v2.0-only staging tables are absent.
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int downgrade = sqlite3_exec(db,
        "DROP TABLE raft_snapshot_staging_chunks;"
        "DROP TABLE raft_snapshot_staging;"
        "PRAGMA user_version=5;",
        nullptr, nullptr, &error);
    const std::string downgrade_error = error ? error : "";
    sqlite3_free(error);
    sqlite3_close(db);
    FS_REQUIRE(downgrade == SQLITE_OK);
    (void)downgrade_error;

    // Opening through the v2.0 state store performs only the additive v5 -> v6
    // migration. Previously persisted Raft state and completed snapshots survive.
    {
        RaftStateStore migrated(path);
        const auto state = migrated.load();
        FS_REQUIRE(state.has_value());
        FS_REQUIRE(state->serialize() == initial_state.serialize());
        FS_REQUIRE(migrated.load_snapshot(1) == completed_snapshot);
    }

    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int(statement, 0) == 6);
    sqlite3_finalize(statement);
    statement = nullptr;
    FS_REQUIRE(sqlite3_prepare_v2(db,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
        "('raft_snapshot_staging','raft_snapshot_staging_chunks');",
        -1, &statement, nullptr) == SQLITE_OK);
    FS_REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    FS_REQUIRE(sqlite3_column_int(statement, 0) == 2);
    sqlite3_finalize(statement);
    sqlite3_close(db);

    auto recovered = Ledger::open(path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 0);
    FS_REQUIRE(recovered.chain().validate());
    remove_raft_db(path);
}

FS_TEST(raft_sqlite_state_rejects_stale_writer_revision) {
    const std::string path = raft_test_db("stale-writer");
    const Wallet wallet = Wallet::generate();
    { auto ledger = Ledger::initialize(path, {{wallet.address(), 1}}, 0, kAtomsPerCoin); }
    RaftStateStore first(path);
    RaftStateStore stale(path);
    RaftPersistentState state;
    state.configuration_hash[0] = 1;
    first.save(state);
    FS_REQUIRE_THROWS(stale.save(state));
    remove_raft_db(path);
}

FS_TEST(raft_sqlite_snapshot_update_failure_keeps_previous_revision_and_payload) {
    const std::string path = raft_test_db("sqlite-update-failure");
    const Wallet wallet = Wallet::generate();
    { auto ledger = Ledger::initialize(path, {{wallet.address(), 1}}, 0, kAtomsPerCoin); }
    {
        RaftStateStore store(path);
        RaftPersistentState initial;
        initial.configuration_hash[0] = 0x11;
        store.save(initial);
        const auto before = store.load();
        FS_REQUIRE(before.has_value());
        const auto before_revision = store.revision();
        const auto before_payload = before->serialize();

        sqlite3* db = nullptr;
        FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        char* error = nullptr;
        const int trigger_result = sqlite3_exec(db,
            "CREATE TRIGGER reject_raft_update BEFORE UPDATE ON raft_state "
            "BEGIN SELECT RAISE(ABORT, 'injected Raft snapshot write failure'); END;",
            nullptr, nullptr, &error);
        sqlite3_free(error);
        sqlite3_close(db);
        FS_REQUIRE(trigger_result == SQLITE_OK);

        RaftPersistentState changed = *before;
        changed.current_term = 1;
        FS_REQUIRE_THROWS(store.save(changed));
        const auto after = store.load();
        FS_REQUIRE(after.has_value());
        FS_REQUIRE(store.revision() == before_revision);
        FS_REQUIRE(after->serialize() == before_payload);

        FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
        error = nullptr;
        const int drop_result = sqlite3_exec(db, "DROP TRIGGER reject_raft_update;", nullptr, nullptr, &error);
        sqlite3_free(error);
        sqlite3_close(db);
        FS_REQUIRE(drop_result == SQLITE_OK);
    }
    remove_raft_db(path);
}

FS_TEST(raft_sqlite_state_detects_checksum_corruption) {
    const std::string path = raft_test_db("checksum");
    const Wallet wallet = Wallet::generate();
    { auto ledger = Ledger::initialize(path, {{wallet.address(), 1}}, 0, kAtomsPerCoin); }
    {
        RaftStateStore store(path);
        RaftPersistentState state;
        state.configuration_hash[0] = 7;
        store.save(state);
    }
    sqlite3* db = nullptr;
    FS_REQUIRE(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    char* error = nullptr;
    const int result = sqlite3_exec(db,
        "UPDATE raft_state SET payload=zeroblob(length(payload));", nullptr, nullptr, &error);
    const std::string detail = error ? error : "";
    sqlite3_free(error);
    sqlite3_close(db);
    FS_REQUIRE(result == SQLITE_OK);
    (void)detail;
    FS_REQUIRE_THROWS(([&] { RaftStateStore corrupted(path); }()));
    remove_raft_db(path);
}

FS_TEST(raft_applied_index_advances_strictly_one_committed_entry_at_a_time) {
    SimulatedRaftCluster cluster;
    const auto leader = cluster.elect_leader();
    (void)cluster.propose(leader, Bytes{1});
    for (int i = 0; i < 3; ++i) cluster.advance();
    FS_REQUIRE_THROWS(cluster.node(leader).mark_applied(2)); // Must apply index 1 first.
    cluster.node(leader).mark_applied(1);
    cluster.node(leader).mark_applied(2);
    FS_REQUIRE(cluster.node(leader).persistent_state().last_applied == 2);
}


FS_TEST(raft_old_term_entry_is_not_committed_by_majority_until_current_term_entry_is_replicated) {
    auto config = raft_config("node-a", 91);
    RaftNode initial(config);
    RaftPersistentState recovered = initial.persistent_state();
    recovered.current_term = 1;
    recovered.voted_for.clear();
    recovered.log = {{1, 1, Bytes{0x51}}};
    recovered.commit_index = 0;
    recovered.last_applied = 0;

    RaftNode node(config, recovered);
    const auto vote_requests = node.tick(std::chrono::milliseconds(500));
    FS_REQUIRE(vote_requests.size() == 2);
    FS_REQUIRE(node.role() == RaftRole::Candidate);
    RaftMessage vote;
    vote.type = RaftMessageType::VoteResponse;
    vote.term = 2;
    vote.from = "node-b";
    vote.to = "node-a";
    vote.vote_granted = true;
    const auto append_requests = node.receive(vote);
    FS_REQUIRE(node.role() == RaftRole::Leader);
    FS_REQUIRE(!node.leader_ready());
    FS_REQUIRE(append_requests.size() == 2);
    FS_REQUIRE(node.persistent_state().commit_index == 0);

    RaftMessage old_prefix_ack;
    old_prefix_ack.type = RaftMessageType::AppendResponse;
    old_prefix_ack.term = 2;
    old_prefix_ack.to = "node-a";
    old_prefix_ack.success = true;
    old_prefix_ack.match_index = 1;
    old_prefix_ack.from = "node-b";
    (void)node.receive(old_prefix_ack);
    old_prefix_ack.from = "node-c";
    (void)node.receive(old_prefix_ack);
    FS_REQUIRE(node.persistent_state().commit_index == 0);
    FS_REQUIRE(!node.leader_ready());

    old_prefix_ack.from = "node-b";
    old_prefix_ack.match_index = 2; // The current-term no-op is now replicated on a majority.
    (void)node.receive(old_prefix_ack);
    FS_REQUIRE(node.persistent_state().commit_index == 2);
    FS_REQUIRE(node.leader_ready());
}

FS_TEST(raft_candidate_cannot_propose_before_current_term_noop_is_committed) {
    auto config = raft_config("node-a", 2);
    RaftNode node(config);
    const auto requests = node.tick(std::chrono::milliseconds(500));
    FS_REQUIRE(requests.size() == 2);
    FS_REQUIRE(node.role() == RaftRole::Candidate);
    FS_REQUIRE_THROWS(node.propose(Bytes{1, 2, 3}));
}

FS_TEST(raft_node_is_disabled_after_durable_state_write_failure) {
    auto config = raft_config("node-a", 8);
    std::size_t writes = 0;
    RaftNode node(config, std::nullopt, [&](const RaftPersistentState&) {
        ++writes;
        if (writes > 1) throw std::runtime_error("injected Raft storage failure");
    });
    FS_REQUIRE(node.healthy());
    FS_REQUIRE_THROWS(node.tick(std::chrono::milliseconds(500)));
    FS_REQUIRE(!node.healthy());
    FS_REQUIRE_THROWS(node.tick(std::chrono::milliseconds(10)));
    RaftMessage request;
    request.type = RaftMessageType::RequestVote;
    request.term = 2;
    request.from = "node-b";
    request.to = "node-a";
    FS_REQUIRE_THROWS(node.receive(request));
}

FS_TEST(raft_rejects_zero_term_and_malformed_message_shapes) {
    auto config = raft_config("node-a", 2);
    RaftNode node(config);
    RaftMessage zero_term;
    zero_term.type = RaftMessageType::RequestVote;
    zero_term.from = "node-b";
    zero_term.to = "node-a";
    FS_REQUIRE_THROWS(node.receive(zero_term));

    RaftMessage malformed;
    malformed.type = RaftMessageType::AppendEntries;
    malformed.term = 1;
    malformed.from = "node-b";
    malformed.to = "node-a";
    malformed.vote_granted = true;
    FS_REQUIRE_THROWS(malformed.serialize());
}

FS_TEST(raft_append_entries_malformed_later_entry_does_not_partially_mutate_log) {
    auto config = raft_config("node-a", 103);
    RaftNode follower(config);
    RaftMessage append;
    append.type = RaftMessageType::AppendEntries;
    append.term = 1;
    append.from = "node-b";
    append.to = "node-a";
    append.entries = {
        RaftLogEntry{1, 1, Bytes{0xA1}},
        RaftLogEntry{3, 1, Bytes{0xA2}} // Index 2 is missing: reject the batch.
    };
    FS_REQUIRE_THROWS(follower.receive(append));
    FS_REQUIRE(follower.persistent_state().log.empty());
    FS_REQUIRE(follower.persistent_state().commit_index == 0);
}

FS_TEST(raft_direct_receive_enforces_aggregate_append_message_size_limit) {
    auto config = raft_config("node-a", 104);
    RaftNode follower(config);
    RaftMessage append;
    append.type = RaftMessageType::AppendEntries;
    append.term = 1;
    append.from = "node-b";
    append.to = "node-a";
    append.entries.reserve(15);
    for (std::uint64_t index = 1; index <= 15; ++index) {
        append.entries.push_back({index, 1, Bytes(1024U * 1024U, static_cast<Byte>(index))});
    }
    FS_REQUIRE_THROWS(follower.receive(append));
    FS_REQUIRE(follower.persistent_state().log.empty());
}

FS_TEST(raft_timing_validation_and_tick_saturate_without_integer_overflow) {
    auto extreme_heartbeat = raft_config("node-a", 101);
    extreme_heartbeat.heartbeat_interval = std::chrono::milliseconds::max();
    FS_REQUIRE_THROWS(RaftNode(extreme_heartbeat));

    auto minimum_timeout = raft_config("node-a", 105);
    minimum_timeout.election_timeout_min = std::chrono::milliseconds::min();
    FS_REQUIRE_THROWS(RaftNode(minimum_timeout));

    auto config = raft_config("node-a", 102);
    RaftNode follower(config);
    const auto election = follower.tick(std::chrono::milliseconds::max());
    FS_REQUIRE(election.size() == 2);
    FS_REQUIRE(follower.persistent_state().current_term == 1);
    FS_REQUIRE(follower.role() == RaftRole::Candidate);

    SimulatedRaftCluster cluster;
    const auto leader_index = cluster.elect_leader();
    const auto heartbeat = cluster.node(leader_index).tick(std::chrono::milliseconds::max());
    FS_REQUIRE(heartbeat.size() == 2);
    FS_REQUIRE(std::all_of(heartbeat.begin(), heartbeat.end(), [](const RaftMessage& message) {
        return message.type == RaftMessageType::AppendEntries;
    }));
}

FS_TEST(raft_state_store_refuses_to_create_a_missing_ledger_database) {
    const std::string path = raft_test_db("missing-store");
    remove_raft_db(path);
    FS_REQUIRE_THROWS(([&] { RaftStateStore missing(path); }()));
    FS_REQUIRE(!std::filesystem::exists(path));
}

FS_TEST(raft_partition_can_have_old_and_new_term_leaders_but_never_two_in_one_term) {
    SimulatedRaftCluster cluster;
    const std::size_t old_leader = cluster.elect_leader();
    const std::uint64_t old_term = cluster.node(old_leader).persistent_state().current_term;
    cluster.partition_leader(old_leader);

    std::optional<std::size_t> majority_leader;
    for (int step = 0; step < 200 && !majority_leader.has_value(); ++step) {
        cluster.advance();
        for (std::size_t i = 0; i < 3; ++i) {
            if (i != old_leader && cluster.node(i).role() == RaftRole::Leader &&
                cluster.node(i).leader_ready()) {
                majority_leader = i;
                break;
            }
        }
    }
    FS_REQUIRE(majority_leader.has_value());
    FS_REQUIRE(cluster.node(old_leader).role() == RaftRole::Leader);
    const std::uint64_t new_term = cluster.node(*majority_leader).persistent_state().current_term;
    FS_REQUIRE(new_term > old_term);

    std::map<std::uint64_t, std::size_t> leaders_per_term;
    for (std::size_t i = 0; i < 3; ++i) {
        if (cluster.node(i).role() == RaftRole::Leader) {
            ++leaders_per_term[cluster.node(i).persistent_state().current_term];
        }
    }
    for (const auto& [term, count] : leaders_per_term) {
        (void)term;
        FS_REQUIRE(count <= 1);
    }

    cluster.heal();
    for (int step = 0; step < 30; ++step) cluster.advance();
    FS_REQUIRE(cluster.node(old_leader).role() == RaftRole::Follower);
    FS_REQUIRE(cluster.node(*majority_leader).role() == RaftRole::Leader);
    leaders_per_term.clear();
    for (std::size_t i = 0; i < 3; ++i) {
        if (cluster.node(i).role() == RaftRole::Leader) {
            ++leaders_per_term[cluster.node(i).persistent_state().current_term];
        }
    }
    for (const auto& [term, count] : leaders_per_term) {
        (void)term;
        FS_REQUIRE(count <= 1);
    }
}

FS_TEST(raft_committed_log_converges_after_leader_isolation_and_majority_re_election) {
    SimulatedRaftCluster cluster;
    const std::size_t original_leader = cluster.elect_leader();
    FS_REQUIRE(cluster.propose(original_leader, Bytes{0xA1, 0x01}) == 2);
    for (int step = 0; step < 8; ++step) cluster.advance();
    FS_REQUIRE(cluster.node(original_leader).persistent_state().commit_index >= 2);

    cluster.partition_leader(original_leader);
    std::optional<std::size_t> replacement_leader;
    for (int step = 0; step < 200 && !replacement_leader.has_value(); ++step) {
        cluster.advance();
        for (std::size_t i = 0; i < 3; ++i) {
            if (i != original_leader && cluster.node(i).role() == RaftRole::Leader &&
                cluster.node(i).leader_ready()) {
                replacement_leader = i;
                break;
            }
        }
    }
    FS_REQUIRE(replacement_leader.has_value());
    FS_REQUIRE(cluster.propose(*replacement_leader, Bytes{0xB2, 0x02}) == 2);
    for (int step = 0; step < 8; ++step) cluster.advance();
    FS_REQUIRE(cluster.node(*replacement_leader).persistent_state().commit_index >= 4);

    cluster.heal();
    for (int step = 0; step < 60; ++step) cluster.advance();

    const auto& reference = cluster.node(*replacement_leader).persistent_state();
    FS_REQUIRE(reference.commit_index >= 4);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& state = cluster.node(i).persistent_state();
        FS_REQUIRE(state.commit_index == reference.commit_index);
        FS_REQUIRE(state.log == reference.log);
    }
}

FS_TEST(raft_three_node_cluster_cannot_elect_leader_without_majority) {
    SimulatedRaftCluster cluster;
    cluster.set_link(0, 1, false);
    cluster.set_link(0, 2, false);
    cluster.set_link(1, 2, false);
    for (int i = 0; i < 100; ++i) cluster.advance();
    FS_REQUIRE(!cluster.leader_index().has_value());
    for (std::size_t i = 0; i < 3; ++i) {
        FS_REQUIRE(cluster.node(i).role() != RaftRole::Leader);
        FS_REQUIRE(!cluster.node(i).leader_ready());
    }
}
