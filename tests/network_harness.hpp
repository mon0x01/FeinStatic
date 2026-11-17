#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/message_dispatch.hpp"
#include "feinstat/network/network_runtime.hpp"
#include "feinstat/network/peer_identity.hpp"

#include <boost/system/error_code.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace feinstat::network::test {

// Test-only application message type (not part of the production control plane).
// Used to exercise end-to-end application message dispatch.
inline constexpr PeerMessageType kTestAppMessage = static_cast<PeerMessageType>(1000U);

enum class WaitResult { Success, Timeout, Error };

struct ReceivedMessage {
    NodeId peer_node_id;
    PeerMessageType type;
    Bytes payload;
};

// Configuration shared by every node in a harness topology.
struct TopologyConfig {
    Bytes network_identifier;
    std::string listen_address{"127.0.0.1"};
};

// RAII-owned live test node. Each fixture owns a unique temporary directory, an
// independently generated and file-backed identity, an independent SQLite peer
// database, an independent NetworkRuntime configuration, and its own runtime +
// run() thread.
class NodeFixture {
public:
    NodeFixture(TopologyConfig topology);
    ~NodeFixture();

    NodeFixture(const NodeFixture&) = delete;
    NodeFixture& operator=(const NodeFixture&) = delete;
    NodeFixture(NodeFixture&&) = delete;
    NodeFixture& operator=(NodeFixture&&) = delete;

    // Generate identity, open the store, initialize the runtime, register test
    // message observers, and start run() on a dedicated thread.
    void start();

    // Graceful shutdown: stop the runtime, join threads, remove temp files.
    void stop();

    [[nodiscard]] NetworkRuntime& runtime() noexcept { return *runtime_; }
    [[nodiscard]] const NodeId& node_id() const noexcept { return node_id_; }
    [[nodiscard]] std::uint16_t listen_port() const noexcept { return listen_port_; }
    [[nodiscard]] const std::string& listen_address() const noexcept { return listen_address_; }
    [[nodiscard]] const std::string& temp_dir() const noexcept { return temp_dir_; }
    [[nodiscard]] const Bytes& network_identifier() const noexcept { return network_identifier_; }
    [[nodiscard]] bool is_running() const noexcept { return runtime_ != nullptr && runtime_->is_running(); }

    // Connect to another live fixture (requires the peer to authenticate as its
    // advertised NodeId). Returns true once the connection attempt is issued;
    // use wait_for_peer to observe the authenticated session. last_connect_ec()
    // reports the handshake result of the most recent attempt.
    bool connect_to(NodeFixture& peer);
    [[nodiscard]] const boost::system::error_code& last_connect_ec() const noexcept {
        return last_connect_ec_;
    }

    // Close the authenticated session with the given peer.
    boost::system::error_code disconnect(const NodeId& peer);

    // Wait until a session with peer exists (bounded wait).
    WaitResult wait_for_peer(const NodeId& peer, std::chrono::milliseconds timeout);

    // Wait until no session with peer exists (bounded wait).
    WaitResult wait_for_no_peer(const NodeId& peer, std::chrono::milliseconds timeout);

    // Send an application message to a connected peer.
    boost::system::error_code send(NodeFixture& peer, PeerMessageType type,
                                   const Bytes& payload);

    // Send an application message by node ID.
    boost::system::error_code send(const NodeId& peer, PeerMessageType type,
                                   const Bytes& payload);

    // Receive the next observed application message (bounded wait).
    WaitResult receive_next(ReceivedMessage& out, std::chrono::milliseconds timeout);

    // Connected peer node IDs.
    [[nodiscard]] std::vector<NodeId> connected_peers() const;

    // Human-readable diagnostics for failure reporting.
    [[nodiscard]] std::string diagnostics() const;

    // Sign with this node's own file-backed identity (test-only convenience).
    [[nodiscard]] Signature sign(const Bytes& message) const {
        return identity_.sign(message);
    }
    [[nodiscard]] const PublicKey& public_key() const noexcept {
        return identity_.public_key();
    }

private:
    void register_observers();
    std::uint64_t now_ms() const noexcept;

    boost::system::error_code last_connect_ec_;
    TopologyConfig topology_;
    std::string temp_dir_;
    std::string identity_path_;
    std::string db_path_;
    Bytes network_identifier_;
    std::string listen_address_;
    NodeId node_id_;
    std::uint16_t listen_port_{0};

    NodeIdentity identity_;

    std::unique_ptr<NetworkRuntime> runtime_;
    std::thread runtime_thread_;

    mutable std::mutex msg_mutex_;
    std::condition_variable msg_cv_;
    std::deque<ReceivedMessage> messages_;
};

} // namespace feinstat::network::test
