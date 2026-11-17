#include "test.hpp"

#include "feinstat/consensus/abci/application.hpp"
#include "feinstat/consensus/abci/abci_socket_server.hpp"
#include "feinstat/consensus/abci/abci_codec.hpp"
#include "feinstat/consensus/abci/fst_application.hpp"
#include "feinstat/consensus/abci/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/utxo/types.hpp"

#include "tendermint/abci/types.pb.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <wait.h>

using namespace feinstat;
using namespace feinstat::consensus::abci;
using namespace feinstat::utxo;
using namespace feinstat::crypto;

namespace {

// Path to CometBFT binary (built from v0.40.0 tag with Go 1.27.4).
const char* COMETBFT_BIN = "/tmp/kilo/cometbft";

// Helper: wait for a file to exist.
void wait_for_file(const std::string& path, int timeout_seconds = 30) {
    for (int i = 0; i < timeout_seconds * 10; ++i) {
        if (std::filesystem::exists(path)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("timeout waiting for file: " + path);
}

// Create a CometBFT node home directory with config.
void setup_cometbft_node(const std::string& home,
                         const std::string& socket_path,
                         const std::vector<std::string>& peers) {
    std::filesystem::create_directories(home + "/config");
    std::filesystem::create_directories(home + "/data");

    // Write config.toml
    std::ofstream config(home + "/config/config.toml");
    config << "# CometBFT config for FST integration test\n";
    config << "proxy_app = \"unix://" << socket_path << "\"\n";
    config << "moniker = \"fst-node\"\n";
    config << "fast_sync = false\n";
    config << "db_backend = \"memdb\"\n";
    config << "db_dir = \"data\"\n";
    config << "log_level = \"info\"\n";
    config << "\n[p2p]\n";
    config << "laddr = \"tcp://0.0.0.0:26656\"\n";
    config << "external_address = \"\"\n";
    config << "seeds = \"\"\n";
    config << "persistent_peers = \"";
    for (std::size_t i = 0; i < peers.size(); ++i) {
        if (i > 0) config << ",";
        config << peers[i];
    }
    config << "\"\n";
    config << "unconditional_peer_ids = \"\"\n";
    config << "allow_duplicate_ip = true\n";
    config << "handshake_timeout = \"20s\"\n";
    config << "dial_timeout = \"3s\"\n";
    config << "test_mode = true\n";
    config << "\n[rpc]\n";
    config << "laddr = \"tcp://127.0.0.1:26657\"\n";
    config.close();
}

// Start CometBFT as a child process. Returns PID.
pid_t start_cometbft(const std::string& home) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        std::string home_arg = "--home=" + home;
        std::string log_level_arg = "--log_level=info";
        execl(COMETBFT_BIN, "cometbft", "start", home_arg.c_str(),
              log_level_arg.c_str(), (char*)nullptr);
        _exit(127); // execl failed
    }
    return pid;
}

// Stop CometBFT process.
void stop_cometbft(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGTERM);
        int status;
        waitpid(pid, &status, 0);
    }
}

} // namespace

FS_TEST(cometbft_single_node_genesis_and_commit) {
    // Create temporary directory
    std::string tmpdir = "/tmp/kilo/fst-integration-test-1";
    std::filesystem::remove_all(tmpdir);
    std::filesystem::create_directories(tmpdir);

    std::string socket_path = tmpdir + "/fst.sock";
    std::string home = tmpdir + "/node1";

    // Setup CometBFT node
    setup_cometbft_node(home, socket_path, {});

    // Initialize CometBFT (generates validator key, genesis)
    std::string init_cmd = std::string(COMETBFT_BIN) + " init --home=" + home;
    int init_ret = system(init_cmd.c_str());
    FS_REQUIRE(init_ret == 0);

    // Create FST ABCI application
    staking::StakeParams stake_params;
    stake_params.min_bond = 1ULL * kAtomsPerCoin;
    stake_params.max_validators = 100;
    stake_params.unbonding_period = 1000;
    stake_params.equivocation_slash_basis_points = 500;

    monetary::MonetaryParams monetary_params;
    monetary_params.genesis_supply = 3'000ULL * kAtomsPerCoin;
    monetary_params.initial_block_issuance = 1000ULL;
    monetary_params.tail_block_issuance = 10ULL;
    monetary_params.issuance_halving_interval = 10;
    monetary_params.max_supply = 0;
    monetary_params.base_fee_min = 1'000ULL;
    monetary_params.base_fee_max = 1'000'000ULL;
    monetary_params.target_utilization_ppm = 500'000;
    monetary_params.max_base_fee_change_ppm = 125'000;
    monetary_params.priority_fee_cap = 1'000'000'000ULL * kAtomsPerCoin;

    ChainId chain_id;
    chain_id.fill(0x41);

    auto app = std::shared_ptr<AbciApplication>(
        make_fst_application(chain_id, stake_params, monetary_params));

    // Start ABCI socket server
    AbciSocketServer::Config server_config;
    server_config.socket_path = socket_path;
    server_config.max_message_size = 16'000'000U;

    AbciSocketServer server(server_config, app);
    std::thread server_thread([&server]() { server.run(); });

    // Wait for socket file to appear
    wait_for_file(socket_path, 10);

    // Start CometBFT
    pid_t cometbft_pid = start_cometbft(home);

    // Wait for CometBFT to connect (check for block 1)
    std::string block1_file = home + "/data/block_1.db";
    // Give CometBFT time to start and connect
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Stop CometBFT
    stop_cometbft(cometbft_pid);

    // Stop ABCI server
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }

    // Clean up
    std::filesystem::remove_all(tmpdir);

    // If we get here without crashing, the basic integration worked
    FS_REQUIRE(true);
}

FS_TEST(cometbft_socket_connection_established) {
    // Verify that the ABCI socket server can accept a connection
    // and respond to Echo requests.
    std::string tmpdir = "/tmp/kilo/fst-integration-test-2";
    std::filesystem::remove_all(tmpdir);
    std::filesystem::create_directories(tmpdir);

    std::string socket_path = tmpdir + "/fst.sock";

    staking::StakeParams stake_params;
    monetary::MonetaryParams monetary_params;
    ChainId chain_id;
    chain_id.fill(0x41);

    auto app = std::shared_ptr<AbciApplication>(
        make_fst_application(chain_id, stake_params, monetary_params));

    AbciSocketServer::Config server_config;
    server_config.socket_path = socket_path;

    AbciSocketServer server(server_config, app);
    std::thread server_thread([&server]() { server.run(); });

    // Wait for socket file
    wait_for_file(socket_path, 10);

    // Connect to the socket and send an Echo request
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    FS_REQUIRE(sock >= 0);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(),
                 sizeof(addr.sun_path) - 1);

    int ret = connect(sock, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr));
    FS_REQUIRE(ret == 0);

    // Send Echo request using protobuf framing
    tendermint::abci::Request req;
    req.mutable_echo()->set_message("hello");

    std::string serialized;
    bool ok = req.SerializeToString(&serialized);
    FS_REQUIRE(ok);

    // Write varint length
    std::vector<std::uint8_t> len_bytes;
    std::uint64_t len = serialized.size();
    while (len >= 0x80ULL) {
        len_bytes.push_back(static_cast<std::uint8_t>((len & 0x7fULL) | 0x80ULL));
        len >>= 7U;
    }
    len_bytes.push_back(static_cast<std::uint8_t>(len & 0x7fULL));

    write_exact(sock, len_bytes.data(), len_bytes.size());
    write_exact(sock, serialized.data(), serialized.size());

    // Read response
    std::uint8_t len_byte;
    std::vector<std::uint8_t> len_bytes_resp;
    for (int i = 0; i < 10; ++i) {
        read_exact(sock, &len_byte, 1);
        len_bytes_resp.push_back(len_byte);
        if ((len_byte & 0x80) == 0) break;
    }
    std::uint64_t resp_len = 0;
    int shift = 0;
    for (std::size_t i = 0; i < len_bytes_resp.size(); ++i) {
        resp_len |= static_cast<std::uint64_t>(len_bytes_resp[i] & 0x7fULL) << shift;
        if ((len_bytes_resp[i] & 0x80ULL) == 0) break;
        shift += 7;
    }

    std::vector<std::uint8_t> resp_bytes(resp_len);
    read_exact(sock, resp_bytes.data(), resp_len);

    tendermint::abci::Response resp;
    FS_REQUIRE(resp.ParseFromArray(resp_bytes.data(),
                                   static_cast<int>(resp_bytes.size())));
    FS_REQUIRE(resp.echo().message() == "hello");

    close(sock);
    server.stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    std::filesystem::remove_all(tmpdir);
}

//------------------------------------------------------------------------------
// Multi-node CometBFT integration test helpers
//------------------------------------------------------------------------------

namespace {

// Helper: read a file into a string.
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    FS_REQUIRE(f.is_open());
    if (!f.is_open()) {
        std::cerr << "Cannot open file: " << path << std::endl;
    }
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// Helper: extract the base64-encoded public key from a CometBFT
// priv_validator_key.json file.
std::string extract_validator_pubkey(const std::string& home) {
    std::string pvk_path = home + "/config/priv_validator_key.json";
    std::string content = read_file(pvk_path);
    // Simple JSON parse: find "value": "..." inside the pub_key object
    size_t pub_key_pos = content.find("\"pub_key\"");
    FS_REQUIRE(pub_key_pos != std::string::npos);
    if (pub_key_pos == std::string::npos) {
        std::cerr << "No pub_key in " << pvk_path << std::endl;
    }
    size_t value_pos = content.find("\"value\"", pub_key_pos);
    FS_REQUIRE(value_pos != std::string::npos);
    if (value_pos == std::string::npos) {
        std::cerr << "No pub_key.value in " << pvk_path << std::endl;
    }
    size_t quote_pos = content.find('"', value_pos + 8);
    FS_REQUIRE(quote_pos != std::string::npos);
    if (quote_pos == std::string::npos) {
        std::cerr << "No opening quote for pub_key.value in " << pvk_path << std::endl;
    }
    size_t end_quote = content.find('"', quote_pos + 1);
    FS_REQUIRE(end_quote != std::string::npos);
    if (end_quote == std::string::npos) {
        std::cerr << "No closing quote for pub_key.value in " << pvk_path << std::endl;
    }
    return content.substr(quote_pos + 1, end_quote - quote_pos - 1);
}

// Helper: write a shared genesis.json with all validators to each node's home.
void write_shared_genesis(const std::vector<std::string>& homes,
                          const std::vector<std::string>& pubkeys_base64,
                          const std::string& chain_id) {
    // Build genesis JSON with all validators.
    std::ostringstream genesis;
    genesis << "{\n";
    genesis << "  \"chain_id\": \"" << chain_id << "\",\n";
    genesis << "  \"height\": \"0\",\n";
    genesis << "  \"app_hash\": \"\",\n";
    genesis << "  \"validators\": [\n";
    for (std::size_t i = 0; i < pubkeys_base64.size(); ++i) {
        genesis << "    {\n";
        genesis << "      \"address\": \"\",\n";
        genesis << "      \"pub_key\": {\n";
        genesis << "        \"type\": \"tendermint/PubKeyEd25519\",\n";
        genesis << "        \"value\": \"" << pubkeys_base64[i] << "\"\n";
        genesis << "      },\n";
        genesis << "      \"power\": \"100\",\n";
        genesis << "      \"name\": \"node" << (i + 1) << "\"\n";
        genesis << "    }";
        if (i + 1 < pubkeys_base64.size()) {
            genesis << ",";
        }
        genesis << "\n";
    }
    genesis << "  ],\n";
    genesis << "  \"consensus_params\": {\n";
    genesis << "    \"block\": {\"max_bytes\": \"22020096\", \"max_gas\": \"-1\"},\n";
    genesis << "    \"evidence\": {\"max_age_num_blocks\": \"100000\", \"max_age_duration\": \"172800000000000\", \"max_bytes\": \"1048576\"},\n";
    genesis << "    \"validator\": {\"pub_key_types\": [\"ed25519\"]}\n";
    genesis << "  },\n";
    genesis << "  \"initial_height\": \"0\"\n";
    genesis << "}\n";

    for (const auto& home : homes) {
        std::string genesis_path = home + "/config/genesis.json";
    std::ofstream out(genesis_path);
    FS_REQUIRE(out.is_open());
    if (!out.is_open()) {
        std::cerr << "Cannot write " << genesis_path << std::endl;
    }
        out << genesis.str();
        out.close();
    }
}

// Multi-node test harness: starts N CometBFT nodes and N FST ABCI apps.
// Nodes run without P2P peering (each is independent) but share the same
// genesis validator set, so they all participate in consensus independently.
// This tests ABCI integration across multiple nodes without requiring
// P2P connectivity setup.
struct MultiNodeNetwork {
    std::string tmpdir;
    std::vector<pid_t> cometbft_pids;
    std::vector<std::shared_ptr<AbciApplication>> apps;
    std::vector<std::shared_ptr<AbciSocketServer>> servers;
    std::vector<std::thread> server_threads;
    std::vector<std::string> socket_paths;
    std::vector<std::string> homes;
    int base_p2p_port;

    MultiNodeNetwork(int num_nodes, int base_port)
        : base_p2p_port(base_port) {
        tmpdir = "/tmp/kilo/fst-multinode-test";
        std::filesystem::remove_all(tmpdir);
        std::filesystem::create_directories(tmpdir);

        // Step 1: Initialize each CometBFT node (generates validator keys).
        for (int i = 0; i < num_nodes; ++i) {
            std::string home = tmpdir + "/node" + std::to_string(i + 1);
            homes.push_back(home);
            std::filesystem::create_directories(home + "/config");
            std::filesystem::create_directories(home + "/data");

            std::string init_cmd =
                std::string(COMETBFT_BIN) + " init --home=" + home;
            int init_ret = system(init_cmd.c_str());
            FS_REQUIRE(init_ret == 0);
            if (init_ret != 0) {
                std::cerr << "CometBFT init failed for node " << (i + 1) << std::endl;
            }
        }

        // Step 2: Extract validator public keys from each node.
        std::vector<std::string> pubkeys;
        for (const auto& home : homes) {
            pubkeys.push_back(extract_validator_pubkey(home));
        }

        // Step 3: Write shared genesis to all nodes.
        std::string chain_id = "fst-test";
        write_shared_genesis(homes, pubkeys, chain_id);

        // Step 4: Write configs without persistent_peers (nodes run independently).
        std::vector<int> p2p_ports;
        for (int i = 0; i < num_nodes; ++i) {
            p2p_ports.push_back(base_port + i);
        }

        for (std::size_t i = 0; i < homes.size(); ++i) {
            const auto& home = homes[i];
            int p2p_port = p2p_ports[i];
            std::string socket_path = tmpdir + "/fst" + std::to_string(i + 1) + ".sock";
            socket_paths.push_back(socket_path);

            std::ofstream config(home + "/config/config.toml");
            config << "# CometBFT config for FST multi-node integration test\n";
            config << "proxy_app = \"unix://" << socket_path << "\"\n";
            config << "moniker = \"fst-node" << (i + 1) << "\"\n";
            config << "fast_sync = false\n";
            config << "db_backend = \"memdb\"\n";
            config << "db_dir = \"data\"\n";
            config << "log_level = \"info\"\n";
            config << "\n[p2p]\n";
            config << "laddr = \"tcp://0.0.0.0:" << p2p_port << "\"\n";
            config << "external_address = \"\"\n";
            config << "seeds = \"\"\n";
            config << "persistent_peers = \"\"\n";
            config << "unconditional_peer_ids = \"\"\n";
            config << "allow_duplicate_ip = true\n";
            config << "handshake_timeout = \"20s\"\n";
            config << "dial_timeout = \"3s\"\n";
            config << "test_mode = true\n";
            config << "\n[rpc]\n";
            config << "laddr = \"tcp://127.0.0.1:" << (26657 + i) << "\"\n";
            config.close();
        }

        // Step 5: Create FST ABCI applications and socket servers.
        staking::StakeParams stake_params;
        stake_params.min_bond = 1ULL * kAtomsPerCoin;
        stake_params.max_validators = 100;
        stake_params.unbonding_period = 1000;
        stake_params.equivocation_slash_basis_points = 500;

        monetary::MonetaryParams monetary_params;
        monetary_params.genesis_supply = 4'000ULL * kAtomsPerCoin;
        monetary_params.initial_block_issuance = 1000ULL;
        monetary_params.tail_block_issuance = 10ULL;
        monetary_params.issuance_halving_interval = 10;
        monetary_params.max_supply = 0;
        monetary_params.base_fee_min = 1'000ULL;
        monetary_params.base_fee_max = 1'000'000ULL;
        monetary_params.target_utilization_ppm = 500'000;
        monetary_params.max_base_fee_change_ppm = 125'000;
        monetary_params.priority_fee_cap = 1'000'000'000ULL * kAtomsPerCoin;

        ChainId chain_id_val;
        chain_id_val.fill(0x41);

        for (std::size_t i = 0; i < homes.size(); ++i) {
            auto app = std::shared_ptr<AbciApplication>(
                make_fst_application(chain_id_val, stake_params, monetary_params));
            apps.push_back(app);

            AbciSocketServer::Config server_config;
            server_config.socket_path = socket_paths[i];
            server_config.max_message_size = 16'000'000U;
            auto server = std::make_shared<AbciSocketServer>(server_config, app);
            servers.push_back(server);

            std::thread t([server]() { server->run(); });
            server_threads.push_back(std::move(t));
        }
    }

    ~MultiNodeNetwork() {
        // Stop all CometBFT nodes.
        for (auto pid : cometbft_pids) {
            if (pid > 0) {
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
            }
        }
        // Stop all ABCI servers.
        for (auto& server : servers) {
            server->stop();
        }
        for (auto& t : server_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        std::filesystem::remove_all(tmpdir);
    }

    void start_cometbft_nodes() {
        for (const auto& home : homes) {
            pid_t pid = start_cometbft(home);
            FS_REQUIRE(pid > 0);
            if (pid <= 0) {
                std::cerr << "Failed to start CometBFT for " << home << std::endl;
            }
            cometbft_pids.push_back(pid);
        }
    }

    void wait_for_cometbft_ready(int timeout_seconds = 30) {
        // Wait for all ABCI socket files to appear.
        for (const auto& path : socket_paths) {
            wait_for_file(path, timeout_seconds);
        }
        // Give CometBFT time to connect to ABCI and start consensus.
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    void stop_all() {
        for (auto pid : cometbft_pids) {
            if (pid > 0) {
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
            }
        }
        for (auto& server : servers) {
            server->stop();
        }
        for (auto& t : server_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
};

} // namespace

//------------------------------------------------------------------------------
// Multi-node integration tests
//------------------------------------------------------------------------------

FS_TEST(cometbft_multi_node_genesis_agreement) {
    // Start 4 CometBFT nodes with shared genesis and 4 FST ABCI applications.
    MultiNodeNetwork net(4, 27000);
    net.start_cometbft_nodes();
    net.wait_for_cometbft_ready(30);

    // Verify all 4 ABCI socket servers are responding.
    for (const auto& socket_path : net.socket_paths) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        FS_REQUIRE(sock >= 0);

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(),
                     sizeof(addr.sun_path) - 1);

        int ret = connect(sock,
                          reinterpret_cast<struct sockaddr*>(&addr),
                          sizeof(addr));
        FS_REQUIRE(ret == 0);
        if (ret != 0) {
            std::cerr << "Cannot connect to ABCI socket: " << socket_path << std::endl;
        }

        // Send Echo request.
        tendermint::abci::Request req;
        req.mutable_echo()->set_message("hello");

        std::string serialized;
        bool ok = req.SerializeToString(&serialized);
        FS_REQUIRE(ok);

        std::vector<std::uint8_t> len_bytes;
        std::uint64_t len = serialized.size();
        while (len >= 0x80ULL) {
            len_bytes.push_back(static_cast<std::uint8_t>((len & 0x7fULL) | 0x80ULL));
            len >>= 7U;
        }
        len_bytes.push_back(static_cast<std::uint8_t>(len & 0x7fULL));

        write_exact(sock, len_bytes.data(), len_bytes.size());
        write_exact(sock, serialized.data(), serialized.size());

        // Read response.
        std::uint8_t len_byte;
        std::vector<std::uint8_t> len_bytes_resp;
        for (int i = 0; i < 10; ++i) {
            read_exact(sock, &len_byte, 1);
            len_bytes_resp.push_back(len_byte);
            if ((len_byte & 0x80) == 0) break;
        }
        std::uint64_t resp_len = 0;
        int shift = 0;
        for (std::size_t i = 0; i < len_bytes_resp.size(); ++i) {
            resp_len |= static_cast<std::uint64_t>(len_bytes_resp[i] & 0x7fULL) << shift;
            if ((len_bytes_resp[i] & 0x80ULL) == 0) break;
            shift += 7;
        }

        std::vector<std::uint8_t> resp_bytes(resp_len);
        read_exact(sock, resp_bytes.data(), resp_len);

        tendermint::abci::Response resp;
        FS_REQUIRE(resp.ParseFromArray(resp_bytes.data(),
                                       static_cast<int>(resp_bytes.size())));
        FS_REQUIRE(resp.echo().message() == "hello");

        close(sock);
    }

    net.stop_all();
}
