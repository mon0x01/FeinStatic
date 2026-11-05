#include "test.hpp"

#include "feinstat/consensus/raft_ledger_adapter.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/ledger/ledger.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <queue>
#include <sqlite3.h>
#include <string>
#include <vector>

using namespace feinstat;
using namespace feinstat::consensus;
using namespace feinstat::ledger;

namespace {

std::string raft_ledger_test_db(const char* suffix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        (std::string("feinstatic-raft-ledger-") + suffix + "-" + std::to_string(stamp) + ".db")).string();
}

void remove_raft_ledger_db(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

RaftConfig adapter_config(const std::string& node_id, std::uint64_t seed,
                          std::uint64_t snapshot_threshold = 128) {
    RaftConfig config;
    config.node_id = node_id;
    config.cluster_id = "feinstatic-raft-ledger-integration-test";
    config.members = {"node-a", "node-b", "node-c"};
    config.election_timeout_min = std::chrono::milliseconds(180);
    config.election_timeout_max = std::chrono::milliseconds(360);
    config.heartbeat_interval = std::chrono::milliseconds(40);
    config.random_seed = seed;
    config.snapshot_threshold_entries = snapshot_threshold;
    return config;
}

class AdapterCluster {
public:
    explicit AdapterCluster(const std::vector<std::string>& paths, const Address& funded_address,
                            std::uint64_t snapshot_threshold = 128)
        : snapshot_threshold_(snapshot_threshold) {
        (void)funded_address;
        const std::vector<std::string> ids{"node-a", "node-b", "node-c"};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            ledgers_[i] = std::make_unique<Ledger>(Ledger::initialize(
                paths[i], {{funded_address, 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin));
            adapters_[i] = std::make_unique<RaftLedgerAdapter>(
                *ledgers_[i], paths[i], adapter_config(ids[i], i + 11U, snapshot_threshold_));
        }
        links_.assign(3, std::vector<bool>(3, true));
        for (std::size_t i = 0; i < 3; ++i) links_[i][i] = false;
        online_.fill(true);
    }

    RaftLedgerAdapter& adapter(std::size_t index) { return *adapters_.at(index); }
    Ledger& ledger(std::size_t index) { return *ledgers_.at(index); }

    void enqueue(std::vector<RaftMessage> messages) {
        for (auto& message : messages) queue_.push(std::move(message));
    }

    void set_online(std::size_t index, bool online) { online_.at(index) = online; }

    void set_link(std::size_t from, std::size_t to, bool enabled) {
        links_.at(from).at(to) = enabled;
    }

    void advance(std::chrono::milliseconds step = std::chrono::milliseconds(10)) {
        for (std::size_t i = 0; i < adapters_.size(); ++i) {
            if (online_[i]) enqueue(adapters_[i]->tick(step));
        }
        std::size_t delivered = 0;
        while (!queue_.empty()) {
            if (++delivered > 10000) throw std::runtime_error("Raft/Ledger simulator exceeded message delivery bound");
            RaftMessage message = std::move(queue_.front());
            queue_.pop();
            const std::size_t source = index_of(message.from);
            const std::size_t target = index_of(message.to);
            if (!online_[source] || !online_[target] || !links_[source][target]) continue;
            enqueue(adapters_[target]->receive(message, message.from));
        }
    }

    std::size_t elect_ready_leader() {
        for (int i = 0; i < 200; ++i) {
            advance();
            std::optional<std::size_t> leader;
            for (std::size_t node = 0; node < adapters_.size(); ++node) {
                if (adapters_[node]->node().role() != RaftRole::Leader) continue;
                if (leader) throw std::runtime_error("multiple active Raft leaders observed in one term");
                leader = node;
            }
            if (leader && adapters_[*leader]->node().leader_ready()) return *leader;
        }
        throw std::runtime_error("Raft/Ledger simulator failed to elect a ready leader");
    }

    void restart(std::size_t index, const std::string& path) {
        adapters_[index].reset();
        ledgers_[index].reset();
        ledgers_[index] = std::make_unique<Ledger>(Ledger::open(path, 0, kAtomsPerCoin));
        const std::string id = index == 0 ? "node-a" : index == 1 ? "node-b" : "node-c";
        adapters_[index] = std::make_unique<RaftLedgerAdapter>(
            *ledgers_[index], path, adapter_config(id, index + 11U, snapshot_threshold_));
        online_[index] = true;
    }

private:
    static std::size_t index_of(const std::string& id) {
        if (id == "node-a") return 0;
        if (id == "node-b") return 1;
        if (id == "node-c") return 2;
        throw std::runtime_error("unknown Raft member in integration simulator");
    }

    std::array<std::unique_ptr<Ledger>, 3> ledgers_;
    std::array<std::unique_ptr<RaftLedgerAdapter>, 3> adapters_;
    std::vector<std::vector<bool>> links_;
    std::array<bool, 3> online_{};
    std::uint64_t snapshot_threshold_{128};
    std::queue<RaftMessage> queue_;
};


FS_TEST(raft_snapshot_compaction_and_restarted_ledger_node_catch_up_atomically) {
    const std::array<std::string, 3> paths{
        raft_ledger_test_db("snapshot-node-a"), raft_ledger_test_db("snapshot-node-b"),
        raft_ledger_test_db("snapshot-node-c")};
    for (const auto& path : paths) remove_raft_ledger_db(path);
    const Wallet miner = Wallet::generate();
    {
        AdapterCluster cluster({paths[0], paths[1], paths[2]}, miner.address(), 4);
        const std::size_t leader = cluster.elect_ready_leader();
        const std::size_t lagger = (leader + 1U) % 3U;
        cluster.set_online(lagger, false);

        for (int i = 0; i < 7; ++i) {
            const RaftBlockProposal proposal = cluster.adapter(leader).propose_next_block(miner.address());
            cluster.enqueue(proposal.outbound);
            for (int step = 0; step < 5; ++step) cluster.advance();
        }

        const auto& leader_state = cluster.adapter(leader).persistent_state();
        FS_REQUIRE(leader_state.snapshot_index >= 4);
        FS_REQUIRE(leader_state.log.size() < 4);
        const Hash expected_tip = cluster.ledger(leader).chain().blocks().back().hash;
        FS_REQUIRE(cluster.ledger(leader).chain().height() == 7);

        // Simulate a process restart while the node is still behind, then let
        // the leader discover that its peer needs the compacted snapshot.
        cluster.restart(lagger, paths[lagger]);
        FS_REQUIRE(cluster.adapter(lagger).persistent_state().snapshot_index == 0);
        for (int step = 0; step < 30; ++step) cluster.advance();

        FS_REQUIRE(cluster.adapter(lagger).persistent_state().snapshot_index == leader_state.snapshot_index);
        FS_REQUIRE(cluster.adapter(lagger).persistent_state().last_applied == leader_state.last_applied);
        FS_REQUIRE(cluster.ledger(lagger).chain().height() == 7);
        FS_REQUIRE(cluster.ledger(lagger).chain().blocks().back().hash == expected_tip);
        FS_REQUIRE(cluster.ledger(lagger).chain().validate());

        cluster.restart(lagger, paths[lagger]);
        FS_REQUIRE(cluster.adapter(lagger).persistent_state().snapshot_index == leader_state.snapshot_index);
        FS_REQUIRE(cluster.ledger(lagger).chain().blocks().back().hash == expected_tip);
    }
    for (const auto& path : paths) remove_raft_ledger_db(path);
}

} // namespace


FS_TEST(raft_snapshot_chunk_staging_survives_restart_and_recovers_after_install_crash) {
    const std::string path = raft_ledger_test_db("snapshot-resume-staging");
    remove_raft_ledger_db(path);
    const Wallet funded = Wallet::generate();
    auto ledger = Ledger::initialize(path, {{funded.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);

    RaftConfig config;
    config.node_id = "node-b";
    config.cluster_id = "feinstatic-snapshot-resume-test";
    config.members = {"node-a", "node-b", "node-c"};
    config.random_seed = 77;

    Bytes payload(700U * 1024U, 0);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<Byte>((i * 131U + 17U) & 0xffU);
    }
    const Hash checksum = crypto::sha256(payload);
    // The leader's send path must be able to read a bounded range directly
    // from the durable SQLite BLOB without materializing the whole snapshot.
    {
        RaftStateStore range_store(path);
        range_store.save_snapshot(42, payload);
        const Bytes first_range = range_store.read_snapshot_chunk(42, 0, 512U * 1024U);
        const Bytes second_range = range_store.read_snapshot_chunk(42, 512U * 1024U, 512U * 1024U);
        FS_REQUIRE(first_range.size() == 512U * 1024U);
        FS_REQUIRE(second_range.size() == payload.size() - first_range.size());
        Bytes reconstructed = first_range;
        reconstructed.insert(reconstructed.end(), second_range.begin(), second_range.end());
        FS_REQUIRE(reconstructed == payload);
        FS_REQUIRE_THROWS(range_store.read_snapshot_chunk(42, payload.size(), 1));
    }
    constexpr std::size_t first_chunk_size = 512U * 1024U;
    Bytes first_chunk(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(first_chunk_size));
    Bytes second_chunk(payload.begin() + static_cast<std::ptrdiff_t>(first_chunk_size), payload.end());

    bool fail_install_once = true;
    Bytes installed_payload;
    auto create_receiver = [&](RaftStateStore& store) {
        return std::make_unique<RaftNode>(
            config, store.load(),
            [&store](const RaftPersistentState& state) { store.save(state); },
            RaftSnapshotReadCallback{}, RaftSnapshotSaveCallback{},
            [&store, &fail_install_once, &installed_payload](const RaftPersistentState& state,
                                                              const Bytes& snapshot_payload) {
                if (fail_install_once) {
                    fail_install_once = false;
                    throw std::runtime_error("injected crash after final chunk was durably staged");
                }
                store.save_snapshot(state.snapshot_index, snapshot_payload);
                store.save(state);
                installed_payload = snapshot_payload;
            },
            [&store] { return store.load_snapshot_staging(); },
            [&store](const RaftSnapshotTransfer& transfer, std::uint64_t offset, const Bytes& chunk) {
                store.append_snapshot_staging_chunk(transfer, offset, chunk);
            },
            [&store] { store.clear_snapshot_staging(); },
            RaftSnapshotChunkReadCallback{},
            [&store](std::uint64_t offset, std::size_t maximum) {
                return store.read_snapshot_staging_chunk(offset, maximum);
            });
    };

    RaftMessage first;
    first.type = RaftMessageType::InstallSnapshot;
    first.term = 1;
    first.from = "node-a";
    first.to = "node-b";
    first.snapshot_index = 1;
    first.snapshot_term = 1;
    first.snapshot_offset = 0;
    first.snapshot_total_size = payload.size();
    first.snapshot_hash = checksum;
    first.snapshot_chunk = first_chunk;

    // First process durably accepts one 512 KiB chunk and acknowledges it.
    {
        RaftStateStore store(path);
        auto receiver = create_receiver(store);
        const auto response = receiver->receive(first);
        FS_REQUIRE(response.size() == 1);
        FS_REQUIRE(response.front().success);
        FS_REQUIRE(response.front().snapshot_offset == first_chunk_size);
        const auto staged = store.load_snapshot_staging();
        FS_REQUIRE(staged.has_value());
        FS_REQUIRE(staged->payload.empty());
        FS_REQUIRE(staged->next_offset == first_chunk_size);
        FS_REQUIRE(store.read_snapshot_staging_chunk(0, first_chunk_size) == first_chunk);
    }

    // Simulate a receiver restart. The sender also starts from offset zero;
    // the receiver must tell it the already durable offset instead of resetting.
    {
        RaftStateStore store(path);
        auto receiver = create_receiver(store);
        const auto resume = receiver->receive(first);
        FS_REQUIRE(resume.size() == 1);
        FS_REQUIRE(!resume.front().success);
        FS_REQUIRE(resume.front().snapshot_offset == first_chunk_size);

        RaftMessage final_chunk = first;
        final_chunk.snapshot_offset = first_chunk_size;
        final_chunk.snapshot_chunk = second_chunk;
        FS_REQUIRE_THROWS(receiver->receive(final_chunk));
        const auto staged = store.load_snapshot_staging();
        FS_REQUIRE(staged.has_value());
        FS_REQUIRE(staged->payload.empty());
        FS_REQUIRE(staged->next_offset == payload.size());
        Bytes recovered_payload = store.read_snapshot_staging_chunk(0, first_chunk_size);
        const Bytes recovered_second = store.read_snapshot_staging_chunk(first_chunk_size, second_chunk.size());
        recovered_payload.insert(recovered_payload.end(), recovered_second.begin(), recovered_second.end());
        FS_REQUIRE(recovered_payload == payload);
        FS_REQUIRE(recovered_payload.size() == staged->total_size);
    }

    // Crash after the final chunk was committed to SQLite but before the
    // application install completed. Startup must finish that installation.
    {
        RaftStateStore store(path);
        auto receiver = create_receiver(store);
        FS_REQUIRE(receiver->persistent_state().snapshot_index == 1);
        FS_REQUIRE(receiver->persistent_state().last_applied == 1);
        FS_REQUIRE(installed_payload == payload);
        FS_REQUIRE(!store.load_snapshot_staging().has_value());
        const auto persisted = store.load();
        FS_REQUIRE(persisted.has_value());
        FS_REQUIRE(persisted->snapshot_index == 1);
        FS_REQUIRE(persisted->snapshot_size == payload.size());
    }

    remove_raft_ledger_db(path);
}

FS_TEST(raft_snapshot_install_rolls_back_ledger_and_raft_state_on_sqlite_failure) {
    const std::string source_path = raft_ledger_test_db("atomic-snapshot-source");
    const std::string target_path = raft_ledger_test_db("atomic-snapshot-target");
    remove_raft_ledger_db(source_path);
    remove_raft_ledger_db(target_path);
    const Wallet funded = Wallet::generate();

    {
        auto source = Ledger::initialize(source_path, {{funded.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
        auto target = Ledger::initialize(target_path, {{funded.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
        const Hash genesis_hash = target.chain().blocks().front().hash;
        (void)source.mine(funded.address());

        const Bytes payload{0x46, 0x53, 0x4c, 0x53, 0x01, 0x02, 0x03};
        RaftPersistentState snapshot_state;
        snapshot_state.checkpoint_hash = genesis_hash;
        snapshot_state.configuration_hash[0] = 0x42;
        snapshot_state.current_term = 2;
        snapshot_state.commit_index = 1;
        snapshot_state.last_applied = 1;
        snapshot_state.snapshot_index = 1;
        snapshot_state.snapshot_term = 2;
        snapshot_state.snapshot_hash = crypto::sha256(payload);
        snapshot_state.snapshot_size = payload.size();

        RaftStateStore store(target_path);
        sqlite3* db = nullptr;
        FS_REQUIRE(sqlite3_open(target_path.c_str(), &db) == SQLITE_OK);
        char* error = nullptr;
        const int trigger_result = sqlite3_exec(db,
            "CREATE TRIGGER reject_snapshot_install BEFORE INSERT ON raft_snapshots "
            "BEGIN SELECT RAISE(ABORT, 'injected snapshot install failure'); END;",
            nullptr, nullptr, &error);
        sqlite3_free(error);
        sqlite3_close(db);
        FS_REQUIRE(trigger_result == SQLITE_OK);

        FS_REQUIRE_THROWS(store.install_snapshot(snapshot_state, payload, target, source.chain().blocks()));
        FS_REQUIRE(target.chain().height() == 0);
        FS_REQUIRE(target.chain().blocks().back().hash == genesis_hash);
        FS_REQUIRE(!store.load().has_value());
        FS_REQUIRE(store.revision() == 0);

        FS_REQUIRE(sqlite3_open(target_path.c_str(), &db) == SQLITE_OK);
        error = nullptr;
        const int drop_result = sqlite3_exec(db, "DROP TRIGGER reject_snapshot_install;", nullptr, nullptr, &error);
        sqlite3_free(error);
        sqlite3_close(db);
        FS_REQUIRE(drop_result == SQLITE_OK);

        store.install_snapshot(snapshot_state, payload, target, source.chain().blocks());
        FS_REQUIRE(target.chain().height() == 1);
        FS_REQUIRE(target.chain().blocks().back().hash == source.chain().blocks().back().hash);
        const auto recovered = store.load();
        FS_REQUIRE(recovered.has_value());
        FS_REQUIRE(recovered->snapshot_index == 1);
        FS_REQUIRE(store.load_snapshot(1) == payload);
    }
    remove_raft_ledger_db(source_path);
    remove_raft_ledger_db(target_path);
}

FS_TEST(raft_block_commands_are_canonical_and_bounded) {
    const Wallet miner = Wallet::generate();
    const std::string path = raft_ledger_test_db("codec");
    remove_raft_ledger_db(path);
    {
        auto ledger = Ledger::initialize(path, {{miner.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
        const Block block = ledger.chain().make_next_block({}, miner.address());
        const Bytes encoded = RaftLedgerAdapter::encode_block_command(block);
        const Block decoded = RaftLedgerAdapter::decode_block_command(encoded);
        FS_REQUIRE(decoded.serialize() == block.serialize());
        Bytes malformed = encoded;
        malformed.push_back(0);
        FS_REQUIRE_THROWS(RaftLedgerAdapter::decode_block_command(malformed));
        FS_REQUIRE_THROWS(RaftLedgerAdapter::decode_block_command(Bytes{1, 2, 3}));
    }
    remove_raft_ledger_db(path);
}

FS_TEST(raft_commit_applies_identical_blocks_to_all_ledgers_and_recovers_atomically) {
    const std::array<std::string, 3> paths{
        raft_ledger_test_db("node-a"), raft_ledger_test_db("node-b"), raft_ledger_test_db("node-c")};
    for (const auto& path : paths) remove_raft_ledger_db(path);
    const Wallet miner = Wallet::generate();

    {
        AdapterCluster cluster({paths[0], paths[1], paths[2]}, miner.address());
        const std::size_t leader = cluster.elect_ready_leader();
        const RaftBlockProposal proposal = cluster.adapter(leader).propose_next_block(miner.address());
        FS_REQUIRE(proposal.index == 2); // Index 1 is the leader's current-term no-op.
        cluster.enqueue(proposal.outbound);
        for (int i = 0; i < 5; ++i) cluster.advance();

        const Hash committed_hash = cluster.ledger(leader).chain().blocks().back().hash;
        for (std::size_t i = 0; i < 3; ++i) {
            FS_REQUIRE(cluster.ledger(i).chain().height() == 1);
            FS_REQUIRE(cluster.ledger(i).chain().blocks().back().hash == committed_hash);
            FS_REQUIRE(cluster.adapter(i).persistent_state().commit_index == 2);
            FS_REQUIRE(cluster.adapter(i).persistent_state().last_applied == 2);
            FS_REQUIRE(cluster.ledger(i).chain().validate());
        }
        cluster.restart(1, paths[1]);
        FS_REQUIRE(cluster.adapter(1).persistent_state().last_applied == 2);
        FS_REQUIRE(cluster.ledger(1).chain().blocks().back().hash == committed_hash);
    }

    for (const auto& path : paths) remove_raft_ledger_db(path);
}

FS_TEST(raft_ledger_and_applied_index_reject_stale_revision_without_partial_commit) {
    const std::string path = raft_ledger_test_db("revision-conflict");
    remove_raft_ledger_db(path);
    const Wallet miner = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{miner.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
        storage::SqliteStore store(path);
        const Bytes initial_snapshot{1, 2, 3};
        const auto revision = store.save_raft_state(initial_snapshot, 0);
        const Bytes competing_snapshot{4, 5, 6};
        (void)store.save_raft_state(competing_snapshot, revision);

        const Block block = ledger.chain().make_next_block({}, miner.address());
        FS_REQUIRE_THROWS(ledger.commit_raft_application({block}, Bytes{7, 8, 9}, revision));
        FS_REQUIRE(ledger.chain().height() == 0);

        auto recovered = Ledger::open(path, 0, kAtomsPerCoin);
        FS_REQUIRE(recovered.chain().height() == 0);
        const auto stored = store.load_raft_state();
        FS_REQUIRE(stored.has_value());
        FS_REQUIRE(stored->payload == competing_snapshot);
    }
    remove_raft_ledger_db(path);
}

FS_TEST(raft_ledger_atomic_commit_rejects_invalid_block_before_mutating_either_state) {
    const std::string path = raft_ledger_test_db("invalid-block");
    remove_raft_ledger_db(path);
    const Wallet miner = Wallet::generate();
    {
        auto ledger = Ledger::initialize(path, {{miner.address(), 100 * kAtomsPerCoin}}, 0, kAtomsPerCoin);
        Block invalid = ledger.chain().make_next_block({}, miner.address());
        // Difficulty zero makes any correctly rehashed nonce valid. Corrupt the
        // chain linkage instead so validation must reject the block regardless
        // of its proof-of-work target.
        invalid.previous_hash[0] ^= 0x01U;
        invalid.hash = invalid.calculate_hash();
        FS_REQUIRE_THROWS(ledger.commit_raft_application({invalid}, Bytes{1, 2, 3}, 0));
        FS_REQUIRE(ledger.chain().height() == 0);
        storage::SqliteStore store(path);
        FS_REQUIRE(!store.load_raft_state().has_value());
    }
    remove_raft_ledger_db(path);
}
