#include "test.hpp"

#include "feinstat/consensus/raft_membership.hpp"
#include "feinstat/consensus/raft.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <vector>

using namespace feinstat::consensus;

FS_TEST(raft_membership_requires_canonical_odd_voter_sets_and_distinct_learners) {
    FS_REQUIRE_THROWS(RaftMembershipState({"a", "b"}));
    FS_REQUIRE_THROWS(RaftMembershipState({"a", "b", "b"}));
    FS_REQUIRE_THROWS(RaftMembershipState({"a", "b", "c"}, {"c"}));
    FS_REQUIRE_THROWS(RaftMembershipState({"a", "b", "c"}, {"bad id"}));
    RaftMembershipState state({"c", "a", "b"}, {"d", "e"});
    FS_REQUIRE(state.voters() == std::vector<std::string>({"a", "b", "c"}));
    FS_REQUIRE(state.replication_members() == std::vector<std::string>({"a", "b", "c", "d", "e"}));
}

FS_TEST(raft_joint_configuration_requires_both_old_and_new_majorities) {
    RaftMembershipState state({"a", "b", "c"}, {"d", "e"});
    state.begin_joint({"b", "c", "d"}, 17);
    FS_REQUIRE(state.phase() == MembershipPhase::JointPending);
    FS_REQUIRE(state.is_voter("d"));
    FS_REQUIRE(!state.is_voter("e"));
    FS_REQUIRE(!state.has_quorum({"a", "b"})); // old majority without new majority
    FS_REQUIRE(state.has_quorum({"b", "c"})); // intersects both majorities
    FS_REQUIRE(state.has_quorum({"a", "b", "c", "d"}));
    FS_REQUIRE(!state.has_quorum({"a", "d"}));
}

FS_TEST(raft_joint_configuration_enforces_ordered_commit_transitions) {
    RaftMembershipState state({"a", "b", "c"}, {"d"});
    state.begin_joint({"b", "c", "d"}, 20);
    FS_REQUIRE_THROWS(state.begin_final(21));
    FS_REQUIRE_THROWS(state.mark_joint_committed(19));
    state.mark_joint_committed(20);
    FS_REQUIRE(state.phase() == MembershipPhase::JointCommitted);
    state.begin_final(21);
    FS_REQUIRE_THROWS(state.mark_final_committed(20));
    state.mark_final_committed(21);
    FS_REQUIRE(state.stable());
    FS_REQUIRE(state.voters() == std::vector<std::string>({"b", "c", "d"}));
    FS_REQUIRE(state.learners() == std::vector<std::string>({"a"}));
    FS_REQUIRE(!state.is_voter("a"));
    FS_REQUIRE(state.is_replication_member("a"));
}

FS_TEST(raft_joint_configuration_round_trips_every_transition_phase) {
    RaftMembershipState state({"a", "b", "c"}, {"d"});
    auto stable = RaftMembershipState::deserialize(state.serialize());
    FS_REQUIRE(stable.stable());
    state.begin_joint({"b", "c", "d"}, 32);
    auto pending = RaftMembershipState::deserialize(state.serialize());
    FS_REQUIRE(pending.phase() == MembershipPhase::JointPending);
    FS_REQUIRE(pending.joint_index() == 32);
    pending.mark_joint_committed(32);
    auto committed = RaftMembershipState::deserialize(pending.serialize());
    FS_REQUIRE(committed.phase() == MembershipPhase::JointCommitted);
    committed.begin_final(33);
    auto final_pending = RaftMembershipState::deserialize(committed.serialize());
    FS_REQUIRE(final_pending.phase() == MembershipPhase::FinalPending);
    FS_REQUIRE(final_pending.final_index() == 33);
}

FS_TEST(raft_joint_quorums_intersect_in_each_configuration_for_three_to_five_replacement) {
    const std::vector<std::string> old_config{"a", "b", "c"};
    const std::vector<std::string> new_config{"b", "c", "d", "e", "f"};
    RaftMembershipState state(old_config, {"d", "e", "f"});
    state.begin_joint(new_config, 100);
    const auto all = state.replication_members();
    std::vector<std::set<std::string>> joint_quorums;
    const std::size_t n = all.size();
    for (std::size_t mask = 0; mask < (1U << n); ++mask) {
        std::set<std::string> acknowledgements;
        for (std::size_t bit = 0; bit < n; ++bit) {
            if ((mask & (1U << bit)) != 0U) acknowledgements.insert(all[bit]);
        }
        if (state.has_quorum(acknowledgements)) joint_quorums.push_back(std::move(acknowledgements));
    }
    FS_REQUIRE(!joint_quorums.empty());
    for (std::size_t i = 0; i < joint_quorums.size(); ++i) {
        for (std::size_t j = i + 1; j < joint_quorums.size(); ++j) {
            bool intersects_old = false;
            bool intersects_new = false;
            for (const auto& id : joint_quorums[i]) {
                if (!joint_quorums[j].contains(id)) continue;
                if (std::find(old_config.begin(), old_config.end(), id) != old_config.end()) intersects_old = true;
                if (std::find(new_config.begin(), new_config.end(), id) != new_config.end()) intersects_new = true;
            }
            FS_REQUIRE(intersects_old);
            FS_REQUIRE(intersects_new);
        }
    }
}

FS_TEST(raft_membership_serialization_rejects_corruption_and_invalid_transition_metadata) {
    RaftMembershipState state({"a", "b", "c"}, {"d"});
    state.begin_joint({"b", "c", "d"}, 7);
    auto bytes = state.serialize();
    bytes.back() ^= 0x80U;
    FS_REQUIRE_THROWS(RaftMembershipState::deserialize(bytes));
    FS_REQUIRE_THROWS(RaftMembershipState::deserialize({}));
}

FS_TEST(raft_membership_rejects_unknown_learner_promotion_and_overlapping_transition) {
    RaftMembershipState state({"a", "b", "c"}, {"d"});
    FS_REQUIRE_THROWS(state.begin_joint({"b", "c", "x"}, 9));
    state.begin_joint({"a", "b", "d"}, 9);
    FS_REQUIRE_THROWS(state.begin_joint({"a", "c", "d"}, 10));
}


namespace {

class FourNodeMembershipCluster {
public:
    FourNodeMembershipCluster() {
        const std::vector<std::string> ids{"node-a", "node-b", "node-c", "node-d"};
        nodes_.resize(ids.size());
        disk_.resize(ids.size());
        links_.assign(ids.size(), std::vector<bool>(ids.size(), true));
        for (std::size_t i = 0; i < ids.size(); ++i) {
            links_[i][i] = false;
            RaftConfig config;
            config.node_id = ids[i];
            config.cluster_id = "membership-integration-test";
            config.members = {"node-a", "node-b", "node-c"};
            config.learners = {"node-d"};
            config.election_timeout_min = std::chrono::milliseconds(180);
            config.election_timeout_max = std::chrono::milliseconds(360);
            config.heartbeat_interval = std::chrono::milliseconds(40);
            config.random_seed = i + 101U;
            nodes_[i] = std::make_unique<RaftNode>(config, std::nullopt,
                [this, i](const RaftPersistentState& state) { disk_[i] = state; });
        }
    }

    RaftNode& node(std::size_t index) { return *nodes_.at(index); }

    void enqueue(std::vector<RaftMessage> messages) {
        for (auto& message : messages) queue_.push(std::move(message));
    }

    void block_peer(std::size_t peer, bool blocked) {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (i == peer) continue;
            links_[peer][i] = !blocked;
            links_[i][peer] = !blocked;
        }
    }

    void drain() {
        std::size_t delivered = 0;
        while (!queue_.empty()) {
            if (++delivered > 10000U) throw std::runtime_error("membership simulator exceeded delivery bound");
            auto message = std::move(queue_.front());
            queue_.pop();
            const auto from = index_of(message.from);
            const auto to = index_of(message.to);
            if (!links_[from][to]) continue;
            enqueue(nodes_[to]->receive(message));
        }
    }

    void tick_all(std::chrono::milliseconds elapsed = std::chrono::milliseconds(10)) {
        for (auto& node_ptr : nodes_) enqueue(node_ptr->tick(elapsed));
        drain();
    }

    std::size_t force_node_a_leader() {
        enqueue(nodes_[0]->tick(std::chrono::milliseconds(500)));
        drain();
        for (int i = 0; i < 100 &&
             (nodes_[0]->role() != RaftRole::Leader || !nodes_[0]->leader_ready()); ++i) {
            for (std::size_t n = 1; n < nodes_.size(); ++n) enqueue(nodes_[n]->tick(std::chrono::milliseconds(10)));
            enqueue(nodes_[0]->tick(std::chrono::milliseconds(40)));
            drain();
        }
        if (nodes_[0]->role() != RaftRole::Leader || !nodes_[0]->leader_ready()) {
            throw std::runtime_error("could not elect the intended removed leader");
        }
        return 0;
    }

    std::size_t index_of(const std::string& id) const {
        if (id == "node-a") return 0;
        if (id == "node-b") return 1;
        if (id == "node-c") return 2;
        if (id == "node-d") return 3;
        throw std::runtime_error("unknown membership test node");
    }

    void recover(std::size_t i) {
        RaftConfig config;
        config.node_id = i == 0 ? "node-a" : i == 1 ? "node-b" : i == 2 ? "node-c" : "node-d";
        config.cluster_id = "membership-integration-test";
        config.members = {"node-a", "node-b", "node-c"};
        config.learners = {"node-d"};
        config.election_timeout_min = std::chrono::milliseconds(180);
        config.election_timeout_max = std::chrono::milliseconds(360);
        config.heartbeat_interval = std::chrono::milliseconds(40);
        config.random_seed = i + 101U;
        nodes_[i] = std::make_unique<RaftNode>(config, disk_[i],
            [this, i](const RaftPersistentState& state) { disk_[i] = state; });
    }

private:
    std::vector<RaftPersistentState> disk_;
    std::vector<std::unique_ptr<RaftNode>> nodes_;
    std::vector<std::vector<bool>> links_;
    std::queue<RaftMessage> queue_;
};

} // namespace

FS_TEST(raft_joint_consensus_reconfigures_live_cluster_and_steps_down_removed_leader) {
    FourNodeMembershipCluster cluster;
    cluster.block_peer(3, true); // The learner is provisioned but unavailable/stale.
    const std::size_t old_leader = cluster.force_node_a_leader();
    FS_REQUIRE(old_leader == 0);
    FS_REQUIRE(cluster.node(3).role() != RaftRole::Candidate);
    FS_REQUIRE_THROWS(cluster.node(old_leader).propose_membership_change({"node-b", "node-c", "node-d"}));

    cluster.block_peer(3, false);
    for (int i = 0; i < 40; ++i) cluster.tick_all();
    cluster.enqueue(cluster.node(old_leader).propose_membership_change({"node-b", "node-c", "node-d"}));
    bool converged = false;
    for (int i = 0; i < 300; ++i) {
        cluster.tick_all();
        bool all_stable = true;
        for (std::size_t n = 0; n < 4; ++n) {
            const auto& membership = cluster.node(n).membership();
            all_stable = all_stable && membership.stable() &&
                membership.voters() == std::vector<std::string>({"node-b", "node-c", "node-d"});
        }
        if (all_stable) { converged = true; break; }
    }
    FS_REQUIRE(converged);
    FS_REQUIRE(cluster.node(0).role() == RaftRole::Follower);
    FS_REQUIRE(cluster.node(0).membership().learners() == std::vector<std::string>({"node-a"}));
    FS_REQUIRE(cluster.node(3).membership().is_voter("node-d"));

    // A removed voter is retained as a learner in the pre-provisioned roster:
    // it can receive replication but cannot initiate another election.
    const auto removed_tick_messages = cluster.node(0).tick(std::chrono::milliseconds(2000));
    FS_REQUIRE(removed_tick_messages.empty());
    FS_REQUIRE(cluster.node(0).role() == RaftRole::Follower);

    // The final C_new configuration survives process reconstruction.
    cluster.recover(0);
    FS_REQUIRE(cluster.node(0).membership().stable());
    FS_REQUIRE(cluster.node(0).membership().voters() ==
        std::vector<std::string>({"node-b", "node-c", "node-d"}));
    FS_REQUIRE(cluster.node(0).membership().learners() == std::vector<std::string>({"node-a"}));
}
