#include "test.hpp"

#include "feinstat/network/node.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <boost/asio.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <sys/stat.h>

using namespace feinstat;
using namespace feinstat::ledger;
using namespace feinstat::network;

namespace {

std::string network_test_db(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            (std::string("feinstatic-network-") + suffix + "-" + std::to_string(stamp) + ".db")).string();
}

void remove_network_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
    std::filesystem::remove(path + ".endpoints", ignored);
}

std::uint16_t reserve_loopback_port() {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor acceptor(
        io, {boost::asio::ip::make_address("127.0.0.1"), 0});
    return acceptor.local_endpoint().port();
}

struct RunningNode {
    explicit RunningNode(Ledger& ledger)
        : server(ledger, "127.0.0.1", 0), worker([this] { server.run(); }) {}

    ~RunningNode() {
        server.stop();
        if (worker.joinable()) worker.join();
    }

    NodeServer server;
    std::thread worker;
};

struct Pair {
    Wallet alice{Wallet::generate()};
    Wallet bob{Wallet::generate()};
    std::string source_path{network_test_db("source")};
    std::string target_path{network_test_db("target")};
    Ledger source{Ledger::initialize(source_path,
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}}, 0, kAtomsPerCoin)};
    Ledger target{Ledger::initialize(target_path,
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}}, 0, kAtomsPerCoin)};

    Pair() {
        source.register_wallet(alice, "alice-network-test-secret");
        source.register_wallet(bob, "bob-network-test-secret");
        target.register_wallet(alice, "alice-network-test-secret");
        target.register_wallet(bob, "bob-network-test-secret");
    }

    ~Pair() {
        remove_network_db(source_path);
        remove_network_db(target_path);
    }
};

class RunningRaftCluster {
public:
    explicit RunningRaftCluster(const Address& miner_address)
        : paths_{network_test_db("raft-tcp-a"), network_test_db("raft-tcp-b"),
                 network_test_db("raft-tcp-c"), network_test_db("raft-tcp-observer")},
          trust_paths_{network_test_db("raft-trust-a"), network_test_db("raft-trust-b"), network_test_db("raft-trust-c")},
          miner_address_(miner_address) {
        std::set<std::uint16_t> reserved;
        for (auto& port : ports_) {
            do { port = reserve_loopback_port(); } while (!reserved.insert(port).second);
        }
        for (const auto& path : paths_) remove_network_db(path);
        for (auto& identity : identities_) {
            identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
            member_ids_.push_back(identity->peer_id());
        }
        observer_identity_ = std::make_shared<NodeIdentity>(NodeIdentity::generate());
        admin_identity_ = std::make_shared<NodeIdentity>(NodeIdentity::generate());
        for (const auto& identity : identities_) {
            observer_trust_.trust(identity->public_key());
            admin_trust_.trust(identity->public_key());
        }
        admin_trust_.trust(admin_identity_->public_key(), PeerRole::Operator);

        for (std::size_t i = 0; i < identities_.size(); ++i) {
            ledgers_[i] = std::make_unique<Ledger>(Ledger::initialize(
                paths_[i], {{miner_address_, 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin));
            network::PeerAuthConfig authentication;
            authentication.identity = identities_[i];
            authentication.trusted_peers.trust(observer_identity_->public_key(), PeerRole::Peer);
            for (std::size_t j = 0; j < identities_.size(); ++j) {
                if (i != j) authentication.trusted_peers.trust(identities_[j]->public_key());
            }
            authentication.trusted_peers.trust(admin_identity_->public_key(), PeerRole::Operator);
            authentication.trusted_peers.save_atomic(trust_paths_[i]);

            consensus::RaftConfig raft;
            raft.node_id = identities_[i]->peer_id();
            raft.cluster_id = "feinstatic-three-node-authenticated-tcp-test";
            raft.members = member_ids_;
            raft.election_timeout_min = std::chrono::milliseconds(900);
            raft.election_timeout_max = std::chrono::milliseconds(1500);
            raft.heartbeat_interval = std::chrono::milliseconds(180);
            raft.random_seed = static_cast<std::uint64_t>(i + 41U);

            network::RaftNetworkConfig raft_network;
            raft_network.database_path = paths_[i];
            raft_network.trust_store_path = trust_paths_[i];
            raft_network.consensus = std::move(raft);
            raft_network.outbound_timeout = std::chrono::milliseconds(400);
            for (std::size_t j = 0; j < identities_.size(); ++j) {
                if (i == j) continue;
                raft_network.peers.push_back({identities_[j]->peer_id(), "127.0.0.1", ports_[j]});
            }
            servers_[i] = std::make_unique<NodeServer>(
                *ledgers_[i], "127.0.0.1", ports_[i], std::move(authentication),
                std::move(raft_network), std::chrono::milliseconds(150));
        }

        monitor_ledger_ = std::make_unique<Ledger>(Ledger::initialize(
            paths_[3], {{miner_address_, 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin));
        for (std::size_t i = 0; i < servers_.size(); ++i) start(i);
    }

    ~RunningRaftCluster() {
        for (std::size_t i = 0; i < servers_.size(); ++i) stop(i);
        monitor_ledger_.reset();
        for (auto& ledger : ledgers_) ledger.reset();
        for (const auto& path : paths_) remove_network_db(path);
        for (const auto& path : trust_paths_) remove_network_db(path);
    }

    void start(std::size_t index) {
        if (workers_[index].joinable()) throw std::logic_error("Raft test server is already running");
        workers_[index] = std::thread([this, index] { servers_[index]->run(); });
    }

    void stop(std::size_t index) {
        if (servers_[index]) servers_[index]->stop();
        if (workers_[index].joinable()) {
            workers_[index].join();
        }
    }

    void restart(std::size_t index) {
        stop(index);
        servers_[index].reset();
        ledgers_[index].reset();
        ledgers_[index] = std::make_unique<Ledger>(Ledger::open(paths_[index], 0, kAtomsPerCoin));
        network::PeerAuthConfig authentication;
        authentication.identity = identities_[index];
        authentication.trusted_peers = PeerTrustStore::load(trust_paths_[index]);
        consensus::RaftConfig raft;
        raft.node_id = identities_[index]->peer_id();
        raft.cluster_id = "feinstatic-three-node-authenticated-tcp-test";
        raft.members = member_ids_;
        raft.election_timeout_min = std::chrono::milliseconds(900);
        raft.election_timeout_max = std::chrono::milliseconds(1500);
        raft.heartbeat_interval = std::chrono::milliseconds(180);
        raft.random_seed = static_cast<std::uint64_t>(index + 41U);
        network::RaftNetworkConfig raft_network;
        raft_network.database_path = paths_[index];
        raft_network.trust_store_path = trust_paths_[index];
        raft_network.consensus = std::move(raft);
        raft_network.outbound_timeout = std::chrono::milliseconds(400);
        for (std::size_t j = 0; j < identities_.size(); ++j) {
            if (index == j) continue;
            raft_network.peers.push_back({identities_[j]->peer_id(), "127.0.0.1", ports_[j]});
        }
        servers_[index] = std::make_unique<NodeServer>(
            *ledgers_[index], "127.0.0.1", ports_[index], std::move(authentication),
            std::move(raft_network), std::chrono::milliseconds(150));
        start(index);
    }

    RaftPeerStatus status(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        return peer.raft_status(*monitor_ledger_);
    }

    RaftPeerHealthReport peer_health(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = admin_identity_;
        authentication.trusted_peers = admin_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        return peer.raft_peer_health(*monitor_ledger_);
    }

    RaftPeerEndpointUpdateResult update_endpoint(std::size_t index, std::size_t target,
                                                  const std::string& host, std::uint16_t port,
                                                  bool enabled) const {
        network::PeerAuthConfig authentication;
        authentication.identity = admin_identity_;
        authentication.trusted_peers = admin_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        return peer.update_raft_peer_endpoint(*monitor_ledger_, identities_[target]->peer_id(),
                                              host, port, enabled);
    }

    void attempt_update_endpoint_as_observer(std::size_t index, std::size_t target,
                                             std::uint16_t port, bool enabled) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        (void)peer.update_raft_peer_endpoint(*monitor_ledger_, identities_[target]->peer_id(),
                                             "127.0.0.1", port, enabled);
    }

    void attempt_disable_voter_endpoint(std::size_t index, std::size_t target,
                                        std::uint16_t port) const {
        network::PeerAuthConfig authentication;
        authentication.identity = admin_identity_;
        authentication.trusted_peers = admin_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        (void)peer.update_raft_peer_endpoint(*monitor_ledger_, identities_[target]->peer_id(),
                                             "127.0.0.1", port, false);
    }

    void attempt_peer_health_as_observer(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        (void)peer.raft_peer_health(*monitor_ledger_);
    }

    RaftBlockProposalResult propose(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        return peer.propose_raft_block(*monitor_ledger_, miner_address_);
    }

    void attempt_direct_block_submission(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        const Block candidate = monitor_ledger_->chain().make_next_block({}, miner_address_);
        peer.submit_block(*monitor_ledger_, candidate);
    }

    void attempt_membership_change_as_observer(std::size_t index) const {
        network::PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        (void)peer.change_raft_membership(*monitor_ledger_, member_ids_);
    }

    TrustPolicyApplyResult push_policy_revoking_observer(std::size_t index, std::uint64_t epoch) {
        // A policy bundle represents the target node's trust state, not the
        // sender's inbound trust list. The client uses a separate trust store
        // to authenticate the target endpoint.
        auto policy_store = PeerTrustStore::load(trust_paths_[index]);
        policy_store.revoke(observer_identity_->peer_id());
        const std::string cluster_id = "feinstatic-three-node-authenticated-tcp-test";
        const Bytes bundle = policy_store.create_signed_policy(cluster_id, epoch, *admin_identity_);
        PeerAuthConfig client_auth;
        client_auth.identity = admin_identity_;
        client_auth.trusted_peers = PeerTrustStore::load(trust_paths_[1]);
        client_auth.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(client_auth));
        return peer.push_trust_policy(*monitor_ledger_, bundle);
    }

    void attempt_policy_revoking_cluster_member(std::size_t index, std::uint64_t epoch,
                                                std::size_t member_to_revoke) {
        auto policy_store = PeerTrustStore::load(trust_paths_[index]);
        policy_store.revoke(member_ids_.at(member_to_revoke));
        const std::string cluster_id = "feinstatic-three-node-authenticated-tcp-test";
        const Bytes bundle = policy_store.create_signed_policy(cluster_id, epoch, *admin_identity_);
        PeerAuthConfig client_auth;
        client_auth.identity = admin_identity_;
        client_auth.trusted_peers = PeerTrustStore::load(trust_paths_[1]);
        client_auth.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(client_auth));
        (void)peer.push_trust_policy(*monitor_ledger_, bundle);
    }

    void attempt_observer_status(std::size_t index) const {
        PeerAuthConfig authentication;
        authentication.identity = observer_identity_;
        authentication.trusted_peers = observer_trust_;
        authentication.expected_peer_id = identities_[index]->peer_id();
        PeerClient peer("127.0.0.1", ports_[index], std::move(authentication));
        (void)peer.raft_status(*monitor_ledger_);
    }

    PeerTrustStore trust_store(std::size_t index) const {
        return PeerTrustStore::load(trust_paths_.at(index));
    }

    const std::string& member_id(std::size_t index) const { return member_ids_.at(index); }

    const std::array<std::string, 4>& paths() const noexcept { return paths_; }

private:
    std::array<std::string, 4> paths_;
    std::array<std::string, 3> trust_paths_;
    std::array<std::uint16_t, 3> ports_;
    Address miner_address_;
    std::array<std::shared_ptr<NodeIdentity>, 3> identities_;
    std::shared_ptr<NodeIdentity> observer_identity_;
    std::shared_ptr<NodeIdentity> admin_identity_;
    PeerTrustStore observer_trust_;
    PeerTrustStore admin_trust_;
    std::vector<std::string> member_ids_;
    std::array<std::unique_ptr<Ledger>, 3> ledgers_;
    std::unique_ptr<Ledger> monitor_ledger_;
    std::array<std::unique_ptr<NodeServer>, 3> servers_;
    std::array<std::thread, 3> workers_;
};

} // namespace

FS_TEST(network_node_identity_encrypted_vault_round_trip_and_tamper_rejection) {
    const auto directory = std::filesystem::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto vault_path = (directory / ("feinstatic-node-vault-" + std::to_string(stamp) + ".vault")).string();
    const auto tampered_path = vault_path + ".tampered";
    {
        auto identity = NodeIdentity::generate();
        const auto expected_id = identity.peer_id();
        const auto expected_public = identity.public_key();
        identity.save_vault_new(vault_path, "correct node vault passphrase", 100'000);
        FS_REQUIRE(NodeIdentity::is_encrypted_vault_file(vault_path));
        struct stat metadata {};
        FS_REQUIRE(::stat(vault_path.c_str(), &metadata) == 0);
        FS_REQUIRE((metadata.st_mode & 0777) == 0600);
        const auto recovered = NodeIdentity::load_vault(vault_path, "correct node vault passphrase");
        FS_REQUIRE(recovered.peer_id() == expected_id);
        FS_REQUIRE(recovered.public_key() == expected_public);
        FS_REQUIRE_THROWS(NodeIdentity::load_vault(vault_path, "incorrect node vault passphrase"));

        std::ifstream input(vault_path, std::ios::binary);
        Bytes bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        FS_REQUIRE(bytes.size() > 20);
        bytes.back() ^= 0x01U;
        {
            std::ofstream output(tampered_path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        (void)::chmod(tampered_path.c_str(), 0600);
        FS_REQUIRE_THROWS(NodeIdentity::load_vault(tampered_path, "correct node vault passphrase"));
    }
    std::error_code ignored;
    std::filesystem::remove(vault_path, ignored);
    std::filesystem::remove(tampered_path, ignored);
}

FS_TEST(network_signed_trust_policy_is_cluster_bound_monotonic_and_tombstone_preserving) {
    const auto administrator = NodeIdentity::generate();
    const auto ordinary = NodeIdentity::generate();
    const auto target = NodeIdentity::generate();
    PeerTrustStore source;
    source.trust(administrator.public_key(), PeerRole::Operator);
    source.trust(ordinary.public_key());
    source.trust(target.public_key());

    PeerTrustStore receiver = source;
    source.revoke(ordinary.peer_id());
    const Bytes epoch_one = source.create_signed_policy("cluster-alpha", 1, administrator);
    receiver.apply_signed_policy(epoch_one, "cluster-alpha", administrator.peer_id());
    FS_REQUIRE(receiver.policy_epoch() == 1);
    FS_REQUIRE(receiver.policy_cluster_id() == "cluster-alpha");
    FS_REQUIRE(receiver.is_revoked(ordinary.peer_id()));
    FS_REQUIRE(receiver.is_operator(administrator.peer_id()));
    FS_REQUIRE_THROWS(receiver.apply_signed_policy(epoch_one, "cluster-alpha", administrator.peer_id()));
    FS_REQUIRE_THROWS(receiver.apply_signed_policy(epoch_one, "cluster-beta", administrator.peer_id()));

    PeerTrustStore resurrecting = source;
    // Simulate a stale/offline administrator policy that still lists the old ID
    // as active. Even a correctly signed newer epoch cannot undo a tombstone.
    PeerTrustStore old_policy;
    old_policy.trust(administrator.public_key(), PeerRole::Operator);
    old_policy.trust(ordinary.public_key());
    old_policy.trust(target.public_key());
    const Bytes epoch_two_resurrection = old_policy.create_signed_policy("cluster-alpha", 2, administrator);
    FS_REQUIRE_THROWS(receiver.apply_signed_policy(epoch_two_resurrection, "cluster-alpha", administrator.peer_id()));

    Bytes tampered = epoch_one;
    tampered.back() ^= 0x80U;
    FS_REQUIRE_THROWS(receiver.apply_signed_policy(tampered, "cluster-alpha", administrator.peer_id()));

    const auto path = network_test_db("trust-policy-persist");
    receiver.save_atomic(path);
    const auto recovered = PeerTrustStore::load(path);
    FS_REQUIRE(recovered.policy_epoch() == 1);
    FS_REQUIRE(recovered.policy_cluster_id() == "cluster-alpha");
    FS_REQUIRE(recovered.is_revoked(ordinary.peer_id()));
    remove_network_db(path);
}

FS_TEST(network_signed_trust_policy_push_persists_and_revokes_remote_handshake) {
    const auto miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());
    const auto applied = cluster.push_policy_revoking_observer(0, 1);
    FS_REQUIRE(applied.policy_epoch == 1);
    FS_REQUIRE(applied.active_peers >= 3);
    bool revoked_handshake_rejected = false;
    try {
        cluster.attempt_observer_status(0);
    } catch (const std::exception& error) {
        revoked_handshake_rejected = std::string(error.what()).find("not in the node trust store") != std::string::npos ||
                                     std::string(error.what()).find("trust store") != std::string::npos;
    }
    FS_REQUIRE(revoked_handshake_rejected);

    // A valid operator signature cannot remove an endpoint that remains in
    // the target's pre-provisioned Raft transport roster.
    bool cluster_member_revocation_rejected = false;
    try {
        cluster.attempt_policy_revoking_cluster_member(0, 2, 1);
    } catch (const std::exception& error) {
        cluster_member_revocation_rejected =
            std::string(error.what()).find("pre-provisioned Raft member") != std::string::npos;
    }
    FS_REQUIRE(cluster_member_revocation_rejected);
    const auto unchanged_policy = cluster.trust_store(0);
    FS_REQUIRE(unchanged_policy.policy_epoch() == 1);
    FS_REQUIRE(unchanged_policy.trusts(cluster.member_id(1)));
    // The remote trust policy must survive a real node restart, not merely
    // remain in the serving process's in-memory PeerAuthConfig.
    cluster.restart(0);
    revoked_handshake_rejected = false;
    try {
        cluster.attempt_observer_status(0);
    } catch (const std::exception& error) {
        revoked_handshake_rejected = std::string(error.what()).find("not in the node trust store") != std::string::npos ||
                                     std::string(error.what()).find("trust store") != std::string::npos;
    }
    FS_REQUIRE(revoked_handshake_rejected);
}

FS_TEST(network_raft_membership_change_requires_operator_role) {
    const auto miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());
    bool rejected = false;
    try {
        cluster.attempt_membership_change_as_observer(0);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("lacks operator role") != std::string::npos;
    }
    FS_REQUIRE(rejected);
}

FS_TEST(network_raft_endpoint_registry_is_operator_managed_and_survives_restart) {
    const auto miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());

    const auto initial = cluster.peer_health(0);
    FS_REQUIRE(initial.peers.size() == 2);
    FS_REQUIRE(initial.worker_count == 2);
    FS_REQUIRE(initial.endpoint_registry_generation >= 1);
    const auto target_id = cluster.member_id(1);
    const auto target = std::find_if(initial.peers.begin(), initial.peers.end(), [&](const RaftPeerHealth& peer) {
        return peer.peer_id == target_id;
    });
    FS_REQUIRE(target != initial.peers.end());
    const std::uint16_t original_port = target->port;

    bool observer_update_rejected = false;
    try {
        cluster.attempt_update_endpoint_as_observer(0, 1, original_port, true);
    } catch (const std::exception& error) {
        observer_update_rejected = std::string(error.what()).find("active operator") != std::string::npos;
    }
    FS_REQUIRE(observer_update_rejected);

    bool active_voter_disable_rejected = false;
    try {
        cluster.attempt_disable_voter_endpoint(0, 1, original_port);
    } catch (const std::exception& error) {
        active_voter_disable_rejected = std::string(error.what()).find("active Raft voter") != std::string::npos;
    }
    FS_REQUIRE(active_voter_disable_rejected);

    const std::uint16_t changed_port = original_port == 65535U
        ? static_cast<std::uint16_t>(original_port - 1U)
        : static_cast<std::uint16_t>(original_port + 1U);
    const auto changed = cluster.update_endpoint(0, 1, "127.0.0.1", changed_port, true);
    FS_REQUIRE(changed.enabled);
    FS_REQUIRE(changed.port == changed_port);
    FS_REQUIRE(changed.endpoint_registry_generation == initial.endpoint_registry_generation + 1U);
    const auto enabled = cluster.update_endpoint(0, 1, "127.0.0.1", original_port, true);
    FS_REQUIRE(enabled.enabled);
    FS_REQUIRE(enabled.port == original_port);
    FS_REQUIRE(enabled.endpoint_registry_generation == changed.endpoint_registry_generation + 1U);

    const std::string registry_path = cluster.paths()[0] + ".endpoints";
    struct stat metadata {};
    FS_REQUIRE(::stat(registry_path.c_str(), &metadata) == 0);
    FS_REQUIRE((metadata.st_mode & 0777) == 0600);

    cluster.restart(0);
    const auto recovered = cluster.peer_health(0);
    FS_REQUIRE(recovered.endpoint_registry_generation == enabled.endpoint_registry_generation);
    const auto recovered_peer = std::find_if(recovered.peers.begin(), recovered.peers.end(), [&](const RaftPeerHealth& peer) {
        return peer.peer_id == target_id;
    });
    FS_REQUIRE(recovered_peer != recovered.peers.end());
    FS_REQUIRE(recovered_peer->enabled);
    FS_REQUIRE(recovered_peer->host == "127.0.0.1");
    FS_REQUIRE(recovered_peer->port == original_port);
}

FS_TEST(network_raft_endpoint_registry_corruption_fails_closed_on_restart) {
    const auto miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());
    const std::string registry_path = cluster.paths()[0] + ".endpoints";
    cluster.stop(0);

    std::ifstream input(registry_path, std::ios::binary);
    FS_REQUIRE(static_cast<bool>(input));
    Bytes encoded((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    FS_REQUIRE(encoded.size() > 16);
    encoded.back() ^= 0x01U;
    {
        std::ofstream output(registry_path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    }
    FS_REQUIRE(::chmod(registry_path.c_str(), 0600) == 0);

    bool rejected = false;
    try {
        cluster.restart(0);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("checksum") != std::string::npos;
    }
    FS_REQUIRE(rejected);
}

FS_TEST(network_raft_peer_health_and_endpoint_management_require_operator_role) {
    const auto miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());
    bool health_rejected = false;
    try {
        cluster.attempt_peer_health_as_observer(0);
    } catch (const std::exception& error) {
        health_rejected = std::string(error.what()).find("active operator") != std::string::npos;
    }
    FS_REQUIRE(health_rejected);
}

FS_TEST(network_peer_handshake_and_status_report_chain_identity) {
    Pair pair;
    RunningNode node(pair.target);
    PeerClient peer("127.0.0.1", node.server.port());
    const PeerStatus status = peer.status(pair.source);
    FS_REQUIRE(status.height == 0);
    FS_REQUIRE(status.tip_hash == pair.source.chain().blocks().front().hash);
    FS_REQUIRE(status.pending_transactions == 0);
}

FS_TEST(network_transaction_relay_validates_and_rejects_duplicate) {
    Pair pair;
    RunningNode node(pair.target);
    PeerClient peer("127.0.0.1", node.server.port());
    const auto tx = pair.source.create_transfer(pair.alice, pair.bob.address(), 3 * kAtomsPerCoin, 1);
    std::string error;
    FS_REQUIRE(peer.submit_transaction(pair.source, tx, &error));
    FS_REQUIRE(pair.target.mempool().transactions().size() == 1);
    FS_REQUIRE(!peer.submit_transaction(pair.source, tx, &error));
    FS_REQUIRE(error.find("duplicate") != std::string::npos || error.find("replayed") != std::string::npos);
    FS_REQUIRE(pair.target.mempool().transactions().size() == 1);
}

FS_TEST(network_block_relay_persists_and_reconciles_mempool) {
    Pair pair;
    RunningNode node(pair.target);
    PeerClient peer("127.0.0.1", node.server.port());

    const auto source_tx = pair.source.create_transfer(pair.alice, pair.bob.address(), 3 * kAtomsPerCoin, 1);
    const auto pending_other = pair.target.create_transfer(pair.bob, pair.alice.address(), 2 * kAtomsPerCoin, 1);
    std::string error;
    FS_REQUIRE(pair.target.submit(pending_other, &error));
    FS_REQUIRE(pair.source.submit(source_tx, &error));
    const Block mined = pair.source.mine(pair.alice.address());

    peer.submit_block(pair.source, mined);
    FS_REQUIRE(pair.target.chain().height() == 1);
    FS_REQUIRE(pair.target.chain().balance_of(pair.alice.address()) == 98 * kAtomsPerCoin);
    FS_REQUIRE(pair.target.chain().balance_of(pair.bob.address()) == 53 * kAtomsPerCoin);
    FS_REQUIRE(pair.target.mempool().transactions().size() == 1);
    FS_REQUIRE(pair.target.mempool().transactions().front().id() == pending_other.id());

    auto recovered = Ledger::open(pair.target_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 1);
    FS_REQUIRE(recovered.chain().balance_of(pair.bob.address()) == 53 * kAtomsPerCoin);
}

FS_TEST(network_sync_downloads_batches_and_survives_restart) {
    const auto source_path = network_test_db("sync-source");
    const auto target_path = network_test_db("sync-target");
    const Wallet alice = Wallet::generate();
    auto source = Ledger::initialize(source_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto target = Ledger::initialize(target_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    source.register_wallet(alice, "alice-sync-test-secret");
    target.register_wallet(alice, "alice-sync-test-secret");
    for (int i = 0; i < 70; ++i) (void)source.mine(alice.address());

    {
        RunningNode node(source);
        PeerClient peer("127.0.0.1", node.server.port());
        const auto result = peer.synchronize(target, 8);
        FS_REQUIRE(result.blocks_received == 70);
        FS_REQUIRE(result.final_height == 70);
        FS_REQUIRE(result.peer_height == 70);
        FS_REQUIRE(target.chain().blocks().back().hash == source.chain().blocks().back().hash);
    }
    {
        auto recovered = Ledger::open(target_path, 0, kAtomsPerCoin);
        FS_REQUIRE(recovered.chain().height() == 70);
        FS_REQUIRE(recovered.chain().blocks().back().hash == source.chain().blocks().back().hash);
        FS_REQUIRE(recovered.chain().balance_of(alice.address()) == 170 * kAtomsPerCoin);
    }
    remove_network_db(source_path);
    remove_network_db(target_path);
}

FS_TEST(network_locator_sync_downloads_only_the_divergent_suffix_after_a_deep_common_prefix) {
    const auto shared_path = network_test_db("locator-shared");
    const auto local_path = network_test_db("locator-local");
    const auto remote_path = network_test_db("locator-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 500 * kAtomsPerCoin}, {bob.address(), 20 * kAtomsPerCoin}};
    auto shared = Ledger::initialize(shared_path, allocations, 0, kAtomsPerCoin);
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);

    for (int i = 0; i < 100; ++i) (void)shared.mine(alice.address());
    const auto shared_suffix = std::vector<Block>(shared.chain().blocks().begin() + 1,
                                                   shared.chain().blocks().end());
    local.accept_blocks(shared_suffix);
    remote.accept_blocks(shared_suffix);

    // Both nodes now share heights 0..100. Their tips diverge independently.
    (void)local.mine(alice.address());
    (void)local.mine(alice.address());
    for (int i = 0; i < 5; ++i) (void)remote.mine(bob.address());

    {
        RunningNode node(remote);
        PeerClient peer("127.0.0.1", node.server.port());
        const auto result = peer.synchronize(local);
        FS_REQUIRE(result.chain_adopted);
        FS_REQUIRE(result.chain_reorganized);
        FS_REQUIRE(result.common_ancestor_height == 100);
        FS_REQUIRE(result.blocks_received == 5);
        FS_REQUIRE(result.final_height == 105);
        FS_REQUIRE(result.requeued_transactions == 0);
    }

    FS_REQUIRE(local.chain().blocks().back().hash == remote.chain().blocks().back().hash);
    auto recovered = Ledger::open(local_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 105);
    FS_REQUIRE(recovered.chain().blocks().back().hash == remote.chain().blocks().back().hash);

    remove_network_db(shared_path);
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_sync_refuses_reorganization_that_orphans_external_bridge_reference) {
    const auto local_path = network_test_db("protected-local");
    const auto remote_path = network_test_db("protected-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "network-protected-alice-passphrase");
    const auto deposit_like = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(deposit_like));
    (void)local.mine(alice.address());
    (void)remote.mine(bob.address());
    (void)remote.mine(bob.address());
    const auto transaction_hash = deposit_like.id();
    const std::string transaction_id = crypto::hex(transaction_hash.data(), transaction_hash.size());

    {
        RunningNode node(remote);
        PeerClient peer("127.0.0.1", node.server.port());
        FS_REQUIRE_THROWS(peer.synchronize(local, 4096, {{transaction_id, 1}}));
    }
    FS_REQUIRE(local.chain().height() == 1);
    FS_REQUIRE(local.chain().find_transaction(transaction_id).has_value());

    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_sync_reorganizes_to_longer_peer_chain_and_requeues_detached_transactions) {
    const auto local_path = network_test_db("reorg-local");
    const auto remote_path = network_test_db("reorg-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 50 * kAtomsPerCoin}};
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);
    local.register_wallet(alice, "network-reorg-alice-passphrase");
    local.register_wallet(bob, "network-reorg-bob-passphrase");
    remote.register_wallet(alice, "network-reorg-alice-passphrase");
    remote.register_wallet(bob, "network-reorg-bob-passphrase");
    const auto detached_transaction = local.create_transfer(alice, bob.address(), 5 * kAtomsPerCoin, 0);
    FS_REQUIRE(local.submit(detached_transaction));
    (void)local.mine(bob.address());
    (void)remote.mine(alice.address());
    (void)remote.mine(alice.address());

    {
        RunningNode node(remote);
        PeerClient peer("127.0.0.1", node.server.port());
        const auto result = peer.synchronize(local);
        FS_REQUIRE(result.chain_adopted);
        FS_REQUIRE(result.chain_reorganized);
        FS_REQUIRE(result.common_ancestor_height == 0);
        FS_REQUIRE(result.blocks_received == 2);
        FS_REQUIRE(result.requeued_transactions == 1);
    }
    FS_REQUIRE(local.chain().blocks().back().hash == remote.chain().blocks().back().hash);
    FS_REQUIRE(local.mempool().transactions().size() == 1);
    FS_REQUIRE(local.mempool().transactions().front().id() == detached_transaction.id());
    auto recovered = Ledger::open(local_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 2);
    FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_wrong_genesis_is_rejected_during_handshake) {
    const auto local_path = network_test_db("wrong-genesis-local");
    const auto remote_path = network_test_db("wrong-genesis-remote");
    const Wallet alice = Wallet::generate();
    const Wallet other = Wallet::generate();
    auto local = Ledger::initialize(local_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, {{other.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    RunningNode node(remote);
    PeerClient peer("127.0.0.1", node.server.port());
    FS_REQUIRE_THROWS(peer.status(local));
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_sync_resolves_equal_height_forks_deterministically) {
    const auto local_path = network_test_db("fork-local");
    const auto remote_path = network_test_db("fork-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    auto local = Ledger::initialize(local_path,
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path,
        {{alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    (void)local.mine(alice.address());
    (void)remote.mine(bob.address());
    const Hash local_tip = local.chain().blocks().back().hash;
    const Hash remote_tip = remote.chain().blocks().back().hash;
    const bool remote_wins = std::lexicographical_compare(
        remote_tip.begin(), remote_tip.end(), local_tip.begin(), local_tip.end());
    {
        RunningNode node(remote);
        PeerClient peer("127.0.0.1", node.server.port());
        const auto result = peer.synchronize(local);
        FS_REQUIRE(result.chain_adopted == remote_wins);
        FS_REQUIRE(result.chain_reorganized == remote_wins);
        FS_REQUIRE(result.common_ancestor_height == 0);
        FS_REQUIRE(result.local_chain_preferred == !remote_wins);
    }
    {
        RunningNode node(local);
        PeerClient peer("127.0.0.1", node.server.port());
        const auto result = peer.synchronize(remote);
        FS_REQUIRE(result.already_current || result.chain_adopted);
    }
    FS_REQUIRE(local.chain().blocks().back().hash == remote.chain().blocks().back().hash);
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_block_batch_validation_is_atomic) {
    Pair pair;
    const Block valid = pair.source.mine(pair.alice.address());
    Block invalid = pair.source.chain().make_next_block({}, pair.alice.address());
    invalid.hash[0] ^= 0xffU;
    FS_REQUIRE_THROWS(pair.target.accept_blocks({valid, invalid}));
    FS_REQUIRE(pair.target.chain().height() == 0);
    auto recovered = Ledger::open(pair.target_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 0);
}

FS_TEST(network_rejects_invalid_block_without_mutating_chain) {
    Pair pair;
    RunningNode node(pair.target);
    PeerClient peer("127.0.0.1", node.server.port());
    Block invalid = pair.source.chain().make_next_block({}, pair.alice.address());
    invalid.hash[0] ^= 0x80U;
    FS_REQUIRE_THROWS(peer.submit_block(pair.source, invalid));
    FS_REQUIRE(pair.target.chain().height() == 0);
}

FS_TEST(network_sync_accepts_shorter_peer_only_when_tip_is_a_prefix) {
    const auto source_path = network_test_db("short-source");
    const auto target_path = network_test_db("short-target");
    const Wallet alice = Wallet::generate();
    auto source = Ledger::initialize(source_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto target = Ledger::initialize(target_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    (void)source.mine(alice.address());
    (void)source.mine(alice.address());
    (void)target.accept_blocks({source.chain().blocks()[1]});
    RunningNode node(target);
    PeerClient peer("127.0.0.1", node.server.port());
    const auto result = peer.synchronize(source);
    FS_REQUIRE(result.already_current);
    FS_REQUIRE(result.blocks_received == 0);
    FS_REQUIRE(source.chain().height() == 2);
    remove_network_db(source_path);
    remove_network_db(target_path);
}


FS_TEST(network_sync_keeps_longer_local_chain_when_shorter_peer_diverges) {
    const auto local_path = network_test_db("short-fork-local");
    const auto remote_path = network_test_db("short-fork-remote");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const std::vector<GenesisAllocation> allocations{
        {alice.address(), 100 * kAtomsPerCoin}, {bob.address(), 100 * kAtomsPerCoin}};
    auto local = Ledger::initialize(local_path, allocations, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, allocations, 0, kAtomsPerCoin);
    (void)local.mine(alice.address());
    (void)local.mine(alice.address());
    (void)remote.mine(bob.address());
    const Hash local_tip = local.chain().blocks().back().hash;
    RunningNode node(remote);
    PeerClient peer("127.0.0.1", node.server.port());
    const auto result = peer.synchronize(local);
    FS_REQUIRE(!result.chain_adopted);
    FS_REQUIRE(result.local_chain_preferred);
    FS_REQUIRE(!result.already_current);
    FS_REQUIRE(local.chain().height() == 2);
    FS_REQUIRE(local.chain().blocks().back().hash == local_tip);
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_server_stop_before_run_is_not_lost) {
    const auto path = network_test_db("stop-before-run");
    const Wallet alice = Wallet::generate();
    auto ledger = Ledger::initialize(path, {{alice.address(), 20 * kAtomsPerCoin}}, 0);
    NodeServer server(ledger, "127.0.0.1", 0, std::chrono::milliseconds(50));

    // This is the ordering that used to be lost: stop() ran before the
    // newly-created run thread got scheduled. The run thread must return
    // without requiring a second stop() call.
    server.stop();
    std::atomic_bool returned{false};
    std::thread worker([&] {
        server.run();
        returned.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!returned.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const bool returned_without_second_stop = returned.load(std::memory_order_acquire);
    server.stop(); // Cleanup even if a regression makes run() ignore the first stop.
    worker.join();

    FS_REQUIRE(returned_without_second_stop);
    remove_network_db(path);
}

FS_TEST(network_server_run_rejects_concurrent_entry) {
    const auto path = network_test_db("run-once");
    const Wallet alice = Wallet::generate();
    auto ledger = Ledger::initialize(path, {{alice.address(), 20 * kAtomsPerCoin}}, 0);
    NodeServer server(ledger, "127.0.0.1", 0, std::chrono::milliseconds(100));
    std::thread worker([&] { server.run(); });

    PeerClient peer("127.0.0.1", server.port(), std::chrono::milliseconds(100));
    bool serving = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            (void)peer.status(ledger);
            serving = true;
            break;
        } catch (const std::exception&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    bool rejected = false;
    if (serving) {
        try {
            server.run();
        } catch (const std::logic_error&) {
            rejected = true;
        }
    }
    server.stop();
    worker.join();
    FS_REQUIRE(serving);
    FS_REQUIRE(rejected);
    remove_network_db(path);
}

FS_TEST(network_server_survives_bad_handshake_and_rejects_wrong_genesis_clearly) {
    const auto local_path = network_test_db("handshake-local");
    const auto remote_path = network_test_db("handshake-remote");
    const Wallet alice = Wallet::generate();
    const Wallet other = Wallet::generate();
    auto local = Ledger::initialize(local_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto remote = Ledger::initialize(remote_path, {{other.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    RunningNode node(remote);
    PeerClient peer("127.0.0.1", node.server.port());
    bool descriptive_rejection = false;
    try {
        (void)peer.status(local);
    } catch (const std::exception& exception) {
        descriptive_rejection = std::string(exception.what()).find("different genesis") != std::string::npos;
    }
    FS_REQUIRE(descriptive_rejection);
    // A rejected peer must not terminate the listener or mutate the chain.
    PeerClient valid_peer("127.0.0.1", node.server.port());
    const PeerStatus status = valid_peer.status(remote);
    FS_REQUIRE(status.height == 0);
    FS_REQUIRE(remote.chain().height() == 0);
    remove_network_db(local_path);
    remove_network_db(remote_path);
}

FS_TEST(network_client_times_out_when_peer_does_not_service_connection) {
    Pair pair;
    NodeServer idle_server(pair.target, "127.0.0.1", 0, std::chrono::milliseconds(150));
    PeerClient peer("127.0.0.1", idle_server.port(), std::chrono::milliseconds(150));
    const auto started = std::chrono::steady_clock::now();
    bool timed_out = false;
    try {
        (void)peer.status(pair.source);
    } catch (const std::exception& exception) {
        timed_out = std::string(exception.what()).find("timed out") != std::string::npos;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    FS_REQUIRE(timed_out);
    FS_REQUIRE(elapsed < std::chrono::seconds(2));
}

FS_TEST(network_relayed_transaction_survives_receiver_restart) {
    const auto source_path = network_test_db("durable-relay-source");
    const auto target_path = network_test_db("durable-relay-target");
    const Wallet alice = Wallet::generate();
    const Wallet bob = Wallet::generate();
    const auto transaction = [&]() {
        auto source = Ledger::initialize(source_path,
            {{alice.address(), 40 * kAtomsPerCoin}}, 0);
        source.register_wallet(alice, "durable-relay-alice-passphrase");
        source.register_wallet(bob, "durable-relay-bob-passphrase");
        return source.create_transfer(alice, bob.address(), 6 * kAtomsPerCoin, 0);
    }();

    {
        auto source = Ledger::open(source_path, 0);
        auto target = Ledger::initialize(target_path,
            {{alice.address(), 40 * kAtomsPerCoin}}, 0);
        target.register_wallet(alice, "durable-relay-alice-passphrase");
        target.register_wallet(bob, "durable-relay-bob-passphrase");
        RunningNode node(target);
        PeerClient peer("127.0.0.1", node.server.port());
        std::string error;
        FS_REQUIRE(peer.submit_transaction(source, transaction, &error));
        FS_REQUIRE(target.mempool().transactions().size() == 1);
    }

    {
        auto recovered = Ledger::open(target_path, 0);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
        FS_REQUIRE(recovered.mempool().transactions().front().id() == transaction.id());
        auto source = Ledger::open(source_path, 0);
        RunningNode node(recovered);
        PeerClient peer("127.0.0.1", node.server.port());
        std::string error;
        FS_REQUIRE(!peer.submit_transaction(source, transaction, &error));
        FS_REQUIRE(error.find("duplicate") != std::string::npos || error.find("replayed") != std::string::npos);
        FS_REQUIRE(recovered.mempool().transactions().size() == 1);
    }
    remove_network_db(source_path);
    remove_network_db(target_path);
}

FS_TEST(network_authenticated_frame_signature_is_bound_to_handshake_session) {
    const auto identity = NodeIdentity::generate();
    const std::string client_id = identity.peer_id();
    const auto server_identity = NodeIdentity::generate();
    const std::string server_id = server_identity.peer_id();
    const Bytes chain_parameters{0x01, 0x02, 0x03, 0x04};
    Hash client_nonce{};
    Hash server_nonce_a{};
    Hash server_nonce_b{};
    client_nonce.fill(0x11);
    server_nonce_a.fill(0x22);
    server_nonce_b.fill(0x23);

    const Hash session_a = detail::authenticated_peer_session_binding(
        chain_parameters, client_id, server_id, client_nonce, server_nonce_a);
    const Hash session_b = detail::authenticated_peer_session_binding(
        chain_parameters, client_id, server_id, client_nonce, server_nonce_b);
    FS_REQUIRE(session_a != session_b);

    const Bytes payload{0x10, 0x20, 0x30};
    const Bytes message_a = detail::authenticated_frame_signing_message(session_a, 7, 0, payload);
    const Bytes message_b = detail::authenticated_frame_signing_message(session_b, 7, 0, payload);
    const Signature signature = identity.sign(message_a);
    FS_REQUIRE(crypto::verify_ed25519(identity.public_key(), message_a, signature));
    FS_REQUIRE(!crypto::verify_ed25519(identity.public_key(), message_b, signature));
}

FS_TEST(network_authenticated_peer_handshake_and_transaction_relay) {
    Pair pair;
    auto client_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());

    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(client_identity->public_key());

    PeerAuthConfig client_auth;
    client_auth.identity = client_identity;
    client_auth.trusted_peers.trust(server_identity->public_key());
    client_auth.expected_peer_id = server_identity->peer_id();

    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        const auto status = peer.status(pair.source);
        FS_REQUIRE(status.height == 0);
        const auto tx = pair.source.create_transfer(pair.alice, pair.bob.address(), 2 * kAtomsPerCoin, 1);
        std::string error;
        FS_REQUIRE(peer.submit_transaction(pair.source, tx, &error));
        FS_REQUIRE(pair.target.mempool().transactions().size() == 1);
    } catch (...) {
        server.stop();
        worker.join();
        throw;
    }
    server.stop();
    worker.join();
}

FS_TEST(network_authenticated_server_rejects_untrusted_client_identity) {
    Pair pair;
    auto allowed_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto untrusted_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());

    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(allowed_identity->public_key());
    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });

    PeerAuthConfig client_auth;
    client_auth.identity = untrusted_identity;
    client_auth.trusted_peers.trust(server_identity->public_key());
    client_auth.expected_peer_id = server_identity->peer_id();
    bool rejected = false;
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        (void)peer.status(pair.source);
    } catch (const std::exception& exception) {
        rejected = std::string(exception.what()).find("trust store") != std::string::npos;
    }
    server.stop();
    worker.join();
    FS_REQUIRE(rejected);
    FS_REQUIRE(pair.target.mempool().transactions().empty());
    FS_REQUIRE(pair.target.chain().height() == 0);
}

FS_TEST(network_authenticated_server_rejects_revoked_client_identity) {
    Pair pair;
    auto client_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());

    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(client_identity->public_key());
    server_auth.trusted_peers.revoke(client_identity->peer_id());
    FS_REQUIRE(server_auth.trusted_peers.is_revoked(client_identity->peer_id()));

    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });

    PeerAuthConfig client_auth;
    client_auth.identity = client_identity;
    client_auth.trusted_peers.trust(server_identity->public_key());
    client_auth.expected_peer_id = server_identity->peer_id();
    bool rejected = false;
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        (void)peer.status(pair.source);
    } catch (const std::exception& exception) {
        rejected = std::string(exception.what()).find("trust store") != std::string::npos;
    }
    server.stop();
    worker.join();
    FS_REQUIRE(rejected);
    FS_REQUIRE(pair.target.mempool().transactions().empty());
    FS_REQUIRE(pair.target.chain().height() == 0);
}

FS_TEST(network_authenticated_client_rejects_wrong_pinned_server) {
    Pair pair;
    auto client_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto other_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());

    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(client_identity->public_key());
    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });

    PeerAuthConfig client_auth;
    client_auth.identity = client_identity;
    client_auth.trusted_peers.trust(other_identity->public_key());
    client_auth.expected_peer_id = other_identity->peer_id();
    bool rejected = false;
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        (void)peer.status(pair.source);
    } catch (const std::exception& exception) {
        rejected = std::string(exception.what()).find("expected peer ID") != std::string::npos ||
                   std::string(exception.what()).find("trust store") != std::string::npos;
    }
    server.stop();
    worker.join();
    FS_REQUIRE(rejected);
    FS_REQUIRE(pair.target.chain().height() == 0);
}

FS_TEST(network_authenticated_peers_reject_plaintext_client) {
    Pair pair;
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto client_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(client_identity->public_key());
    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });
    bool rejected = false;
    try {
        PeerClient plaintext("127.0.0.1", server.port());
        (void)plaintext.status(pair.source);
    } catch (const std::exception&) {
        rejected = true;
    }
    server.stop();
    worker.join();
    FS_REQUIRE(rejected);
}

FS_TEST(network_identity_file_is_private_and_trust_store_is_canonical) {
    const auto directory = std::filesystem::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto identity_path = (directory / ("feinstatic-node-identity-" + std::to_string(stamp) + ".key")).string();
    const auto trust_path = (directory / ("feinstatic-node-trust-" + std::to_string(stamp) + ".txt")).string();
    {
        auto identity = NodeIdentity::generate();
        const auto expected_id = identity.peer_id();
        const auto expected_public_key = identity.public_key();
        identity.save_new(identity_path);
        struct stat metadata {};
        FS_REQUIRE(::stat(identity_path.c_str(), &metadata) == 0);
        FS_REQUIRE((metadata.st_mode & 0777) == 0600);
        auto recovered = NodeIdentity::load(identity_path);
        FS_REQUIRE(recovered.peer_id() == expected_id);
        FS_REQUIRE(recovered.public_key() == expected_public_key);

        PeerTrustStore trust;
        trust.trust(recovered.public_key());
        trust.save_atomic(trust_path);
        const auto loaded = PeerTrustStore::load(trust_path);
        FS_REQUIRE(loaded.size() == 1);
        FS_REQUIRE(loaded.trusts(expected_id));
    }
    std::error_code ignored;
    std::filesystem::remove(identity_path, ignored);
    std::filesystem::remove(trust_path, ignored);
}

FS_TEST(network_plaintext_listener_refuses_non_loopback_binding) {
    Pair pair;
    FS_REQUIRE_THROWS(NodeServer(pair.target, "0.0.0.0", 0));
}

FS_TEST(network_authenticated_peer_can_synchronize_and_import_blocks) {
    const auto source_path = network_test_db("auth-sync-source");
    const auto target_path = network_test_db("auth-sync-target");
    const Wallet alice = Wallet::generate();
    auto source = Ledger::initialize(source_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    auto target = Ledger::initialize(target_path, {{alice.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
    for (int i = 0; i < 5; ++i) (void)source.mine(alice.address());

    auto source_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto target_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    PeerAuthConfig server_auth;
    server_auth.identity = source_identity;
    server_auth.trusted_peers.trust(target_identity->public_key());
    NodeServer server(source, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });

    PeerAuthConfig client_auth;
    client_auth.identity = target_identity;
    client_auth.trusted_peers.trust(source_identity->public_key());
    client_auth.expected_peer_id = source_identity->peer_id();
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        const auto result = peer.synchronize(target, 4);
        FS_REQUIRE(result.blocks_received == 5);
        FS_REQUIRE(result.final_height == 5);
        FS_REQUIRE(target.chain().blocks().back().hash == source.chain().blocks().back().hash);
    } catch (...) {
        server.stop();
        worker.join();
        remove_network_db(source_path);
        remove_network_db(target_path);
        throw;
    }
    server.stop();
    worker.join();
    auto recovered = Ledger::open(target_path, 0, kAtomsPerCoin);
    FS_REQUIRE(recovered.chain().height() == 5);
    FS_REQUIRE(recovered.chain().blocks().back().hash == source.chain().blocks().back().hash);
    remove_network_db(source_path);
    remove_network_db(target_path);
}

FS_TEST(network_authenticated_peer_frame_sequence_and_error_responses_work) {
    Pair pair;
    auto client_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    auto server_identity = std::make_shared<NodeIdentity>(NodeIdentity::generate());
    PeerAuthConfig server_auth;
    server_auth.identity = server_identity;
    server_auth.trusted_peers.trust(client_identity->public_key());
    NodeServer server(pair.target, "127.0.0.1", 0, std::move(server_auth));
    std::thread worker([&] { server.run(); });

    PeerAuthConfig client_auth;
    client_auth.identity = client_identity;
    client_auth.trusted_peers.trust(server_identity->public_key());
    client_auth.expected_peer_id = server_identity->peer_id();
    bool rejected_duplicate = false;
    try {
        PeerClient peer("127.0.0.1", server.port(), std::move(client_auth));
        const auto tx = pair.source.create_transfer(pair.alice, pair.bob.address(), 3 * kAtomsPerCoin, 1);
        std::string error;
        FS_REQUIRE(peer.submit_transaction(pair.source, tx, &error));
        FS_REQUIRE(!peer.submit_transaction(pair.source, tx, &error));
        rejected_duplicate = error.find("duplicate") != std::string::npos ||
                            error.find("replayed") != std::string::npos;
    } catch (...) {
        server.stop();
        worker.join();
        throw;
    }
    server.stop();
    worker.join();
    FS_REQUIRE(rejected_duplicate);
    FS_REQUIRE(pair.target.mempool().transactions().size() == 1);
}

FS_TEST(network_authenticated_tcp_raft_cluster_commits_applies_and_recovers_ledger_blocks) {
    const Wallet miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());

    const auto election_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::size_t leader_index = 3;
    while (std::chrono::steady_clock::now() < election_deadline) {
        std::size_t ready_leaders = 0;
        std::size_t candidate_leader = 3;
        for (std::size_t i = 0; i < 3; ++i) {
            try {
                const auto status = cluster.status(i);
                if (status.role == consensus::RaftRole::Leader &&
                    status.commit_index >= 1 && status.last_applied >= 1) {
                    ++ready_leaders;
                    candidate_leader = i;
                }
            } catch (const std::exception&) {
                // The node may not yet have completed its first authenticated
                // handshake while all three listeners are starting.
            }
        }
        if (ready_leaders == 1) {
            leader_index = candidate_leader;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    FS_REQUIRE(leader_index < 3);

    const auto proposal = cluster.propose(leader_index);
    FS_REQUIRE(proposal.index == 2); // Index 1 is the leader's committed current-term no-op.

    const auto commit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    std::array<RaftPeerStatus, 3> converged{};
    bool all_applied = false;
    while (std::chrono::steady_clock::now() < commit_deadline) {
        all_applied = true;
        for (std::size_t i = 0; i < converged.size(); ++i) {
            try {
                converged[i] = cluster.status(i);
                if (converged[i].commit_index < proposal.index ||
                    converged[i].last_applied < proposal.index ||
                    converged[i].ledger_height != 1 ||
                    converged[i].ledger_tip_hash != proposal.block_hash) {
                    all_applied = false;
                }
            } catch (const std::exception&) {
                all_applied = false;
            }
        }
        if (all_applied) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }
    FS_REQUIRE(all_applied);

    // Raft mode must not allow the legacy peer block-import path to mutate a
    // member's canonical chain outside the replicated log.
    FS_REQUIRE_THROWS(cluster.attempt_direct_block_submission(leader_index));

    cluster.restart(1);
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool recovered = false;
    while (std::chrono::steady_clock::now() < recovery_deadline) {
        try {
            const auto status = cluster.status(1);
            recovered = status.last_applied == proposal.index &&
                status.commit_index >= proposal.index && status.ledger_height == 1 &&
                status.ledger_tip_hash == proposal.block_hash;
            if (recovered) break;
        } catch (const std::exception&) {
            // Listener may still be binding after restart.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    FS_REQUIRE(recovered);
}

FS_TEST(network_raft_tcp_cluster_reelects_after_leader_failure_and_catches_up_restarted_member) {
    const Wallet miner = Wallet::generate();
    RunningRaftCluster cluster(miner.address());

    const auto initial_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::size_t initial_leader = 3;
    while (std::chrono::steady_clock::now() < initial_deadline) {
        std::size_t count = 0;
        std::size_t candidate = 3;
        for (std::size_t i = 0; i < 3; ++i) {
            try {
                const auto status = cluster.status(i);
                if (status.role == consensus::RaftRole::Leader &&
                    status.commit_index >= 1 && status.last_applied >= 1) {
                    ++count;
                    candidate = i;
                }
            } catch (const std::exception&) {}
        }
        if (count == 1) {
            initial_leader = candidate;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    FS_REQUIRE(initial_leader < 3);
    const auto first = cluster.propose(initial_leader);
    FS_REQUIRE(first.index == 2);

    const auto first_commit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool first_committed = false;
    while (std::chrono::steady_clock::now() < first_commit_deadline) {
        first_committed = true;
        for (std::size_t i = 0; i < 3; ++i) {
            try {
                const auto status = cluster.status(i);
                if (status.last_applied < first.index || status.ledger_height != 1 ||
                    status.ledger_tip_hash != first.block_hash) first_committed = false;
            } catch (const std::exception&) { first_committed = false; }
        }
        if (first_committed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    FS_REQUIRE(first_committed);

    // Simulate a hard process stop of the current leader. The other two nodes
    // still form a majority and must elect a replacement leader.
    cluster.stop(initial_leader);
    const auto failover_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    std::size_t replacement_leader = 3;
    while (std::chrono::steady_clock::now() < failover_deadline) {
        std::size_t count = 0;
        std::size_t candidate = 3;
        for (std::size_t i = 0; i < 3; ++i) {
            if (i == initial_leader) continue;
            try {
                const auto status = cluster.status(i);
                if (status.role == consensus::RaftRole::Leader &&
                    status.commit_index >= 3 && status.last_applied >= 3) {
                    ++count;
                    candidate = i;
                }
            } catch (const std::exception&) {}
        }
        if (count == 1) {
            replacement_leader = candidate;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    FS_REQUIRE(replacement_leader < 3);

    const auto second = cluster.propose(replacement_leader);
    FS_REQUIRE(second.index == 4); // New leader's current-term no-op occupies index 3.
    const auto second_commit_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    bool majority_applied = false;
    while (std::chrono::steady_clock::now() < second_commit_deadline) {
        majority_applied = true;
        for (std::size_t i = 0; i < 3; ++i) {
            if (i == initial_leader) continue;
            try {
                const auto status = cluster.status(i);
                if (status.last_applied < second.index || status.ledger_height != 2 ||
                    status.ledger_tip_hash != second.block_hash) majority_applied = false;
            } catch (const std::exception&) { majority_applied = false; }
        }
        if (majority_applied) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    FS_REQUIRE(majority_applied);

    // Restart the former leader with its old durable state. Heartbeats must
    // repair its log and apply the missing command without peer chain-sync.
    cluster.restart(initial_leader);
    const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    bool recovered = false;
    while (std::chrono::steady_clock::now() < recovery_deadline) {
        try {
            const auto status = cluster.status(initial_leader);
            recovered = status.last_applied >= second.index && status.commit_index >= second.index &&
                status.ledger_height == 2 && status.ledger_tip_hash == second.block_hash;
            if (recovered) break;
        } catch (const std::exception&) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    FS_REQUIRE(recovered);
}

FS_TEST(peer_trust_roles_revocation_rotation_and_legacy_rows) {
    const auto peer = NodeIdentity::generate();
    const auto operator_identity = NodeIdentity::generate();
    const auto replacement = NodeIdentity::generate();
    PeerTrustStore store;
    store.trust(peer.public_key());
    store.trust(operator_identity.public_key(), PeerRole::Operator);
    FS_REQUIRE(store.trusts(peer.peer_id()));
    FS_REQUIRE(!store.is_operator(peer.peer_id()));
    FS_REQUIRE(store.is_operator(operator_identity.peer_id()));

    const auto path = network_test_db("trust-roles");
    store.save_atomic(path);
    auto loaded = PeerTrustStore::load(path);
    FS_REQUIRE(loaded.is_operator(operator_identity.peer_id()));
    loaded.rotate(operator_identity.peer_id(), replacement.public_key());
    FS_REQUIRE(loaded.is_revoked(operator_identity.peer_id()));
    FS_REQUIRE(!loaded.trusts(operator_identity.peer_id()));
    FS_REQUIRE(loaded.is_operator(replacement.peer_id()));
    loaded.revoke(peer.peer_id());
    FS_REQUIRE(!loaded.trusts(peer.peer_id()));
    loaded.save_atomic(path);
    auto recovered = PeerTrustStore::load(path);
    FS_REQUIRE(recovered.is_revoked(operator_identity.peer_id()));
    FS_REQUIRE(recovered.is_revoked(peer.peer_id()));
    FS_REQUIRE(recovered.is_operator(replacement.peer_id()));

    const auto legacy_path = path + ".legacy";
    {
        std::ofstream output(legacy_path);
        output << peer.peer_id() << " " << crypto::hex(peer.public_key().data(), peer.public_key().size()) << "\n";
    }
    const auto legacy = PeerTrustStore::load(legacy_path);
    FS_REQUIRE(legacy.trusts(peer.peer_id()));
    FS_REQUIRE(!legacy.is_operator(peer.peer_id()));
    remove_network_db(path);
    remove_network_db(legacy_path);
}
