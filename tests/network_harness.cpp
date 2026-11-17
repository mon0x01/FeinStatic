#include "network_harness.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/bootstrap.hpp"
#include "feinstat/network/node_record.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace feinstat::network::test {

namespace {

std::uint64_t now_ms() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string make_temp_dir(const std::string& prefix) {
    const char* env = std::getenv("FST_TEST_TMPDIR");
    std::string base = (env != nullptr) ? env : std::filesystem::temp_directory_path().string();
    std::string dir = base + "/" + prefix + "-" +
        std::to_string(now_ms()) + "-" + std::to_string(std::rand());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void remove_file(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

} // namespace

NodeFixture::NodeFixture(TopologyConfig topology)
    : topology_(std::move(topology)),
      network_identifier_(topology_.network_identifier),
      listen_address_(topology_.listen_address),
      identity_(NodeIdentity::generate()),
      runtime_(std::make_unique<NetworkRuntime>()) {
    temp_dir_ = make_temp_dir("fst-node");
    identity_path_ = temp_dir_ + "/node_identity";
    db_path_ = temp_dir_ + "/peers.db";

    // Persist the independent file-backed identity (mode-0600 via NodeIdentity).
    identity_.save_new(identity_path_);
    node_id_ = NodeId::from_public_key(identity_.public_key());
}

NodeFixture::~NodeFixture() {
    try {
        stop();
    } catch (...) {
        // Destructor must not throw.
    }
    remove_file(db_path_);
    remove_file(identity_path_);
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
}

void NodeFixture::start() {
    // Start from the runtime's current configuration so a test may set fields
    // such as bootstrap_seed_records via runtime().config() before start().
    NetworkRuntimeConfig config = runtime_->config();
    config.identity_path = identity_path_;
    config.database_path = db_path_;
    config.network_identifier = network_identifier_;
    config.listen_address = listen_address_;
    config.listen_port = 0; // dynamically assigned
    config.protocol_version = 8;
    config.capabilities = {"peer", "bootstrap"};
    config.endpoints = {{TransportType::Tcp, listen_address_, listen_port_}};
    config.record_expiry_unix_ms = 0;
    config.tick_interval = std::chrono::milliseconds(200);

    std::error_code ec = runtime_->initialize(config);
    if (ec) {
        std::ostringstream os;
        os << "NetworkRuntime::initialize failed: " << ec.message();
        throw std::runtime_error(os.str());
    }

    register_observers();

    listen_port_ = runtime_->listen_port();
    if (listen_port_ == 0) {
        throw std::runtime_error("runtime did not assign a listening port");
    }

    runtime_thread_ = std::thread([this]() { runtime_->run(); });

    // Wait until the run() thread has actually entered its loop so that
    // is_running() is reliably true once start() returns.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!runtime_->is_running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NodeFixture::stop() {
    if (runtime_) {
        runtime_->stop();
    }
    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }
}

void NodeFixture::register_observers() {
    MessageDispatcher& disp = runtime_->dispatcher();

    // Observe incoming pongs (the default kPing handler auto-responds; we only
    // observe the pong here).
    disp.register_handler(PeerMessageType::kPong,
        [this](std::shared_ptr<PeerSession> session, PeerMessageType type,
               const Bytes& payload, boost::system::error_code& ec) {
            (void)session;
            ReceivedMessage m{session->peer_node_id(), type, payload};
            {
                std::lock_guard<std::mutex> lock(msg_mutex_);
                messages_.push_back(std::move(m));
            }
            msg_cv_.notify_all();
            ec = boost::system::error_code{};
        });

    // Observe test application messages.
    disp.register_handler(kTestAppMessage,
        [this](std::shared_ptr<PeerSession> session, PeerMessageType type,
               const Bytes& payload, boost::system::error_code& ec) {
            ReceivedMessage m{session->peer_node_id(), type, payload};
            {
                std::lock_guard<std::mutex> lock(msg_mutex_);
                messages_.push_back(std::move(m));
            }
            msg_cv_.notify_all();
            ec = boost::system::error_code{};
        });

    // Observe graceful disconnects.
    disp.register_handler(PeerMessageType::kDisconnect,
        [this](std::shared_ptr<PeerSession> session, PeerMessageType type,
               const Bytes& payload, boost::system::error_code& ec) {
            (void)payload;
            ReceivedMessage m{session->peer_node_id(), type, {}};
            {
                std::lock_guard<std::mutex> lock(msg_mutex_);
                messages_.push_back(std::move(m));
            }
            msg_cv_.notify_all();
            session->close(ec);
        });

    // Observe control-plane peer-record messages. These are handled and
    // persisted by the default handlers, so a non-replacing observer taps them
    // for tests without suppressing persistence.
    disp.add_observer(
        [this](std::shared_ptr<PeerSession> session, PeerMessageType type,
               const Bytes& payload) {
            if (type != PeerMessageType::kPeerRecordAnnounce &&
                type != PeerMessageType::kPeerRecordResponse) {
                return;
            }
            ReceivedMessage m{session->peer_node_id(), type, payload};
            {
                std::lock_guard<std::mutex> lock(msg_mutex_);
                messages_.push_back(std::move(m));
            }
            msg_cv_.notify_all();
        });
}

bool NodeFixture::connect_to(NodeFixture& peer) {
    runtime_->admission().connect_to_peer(
        peer.node_id(), peer.listen_address(), peer.listen_port(),
        [this](const boost::system::error_code& ec, std::shared_ptr<PeerSession>) {
            last_connect_ec_ = ec;
        });
    return true;
}

boost::system::error_code NodeFixture::disconnect(const NodeId& peer) {
    auto session = runtime_->admission().session(peer);
    if (!session) {
        return boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
    }
    boost::system::error_code ec;
    session->close(ec);
    return ec;
}

WaitResult NodeFixture::wait_for_peer(const NodeId& peer,
                                      std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (runtime_->admission().session(peer) != nullptr) {
            return WaitResult::Success;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return WaitResult::Timeout;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

WaitResult NodeFixture::wait_for_no_peer(const NodeId& peer,
                                         std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (runtime_->admission().session(peer) == nullptr) {
            return WaitResult::Success;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return WaitResult::Timeout;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

boost::system::error_code NodeFixture::send(NodeFixture& peer, PeerMessageType type,
                                            const Bytes& payload) {
    return send(peer.node_id(), type, payload);
}

boost::system::error_code NodeFixture::send(const NodeId& peer, PeerMessageType type,
                                            const Bytes& payload) {
    auto session = runtime_->admission().session(peer);
    if (!session) {
        return boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
    }
    boost::system::error_code ec;
    runtime_->dispatcher().send(session, type, payload, ec);
    return ec;
}

WaitResult NodeFixture::receive_next(ReceivedMessage& out,
                                      std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(msg_mutex_);
    if (msg_cv_.wait_for(lock, timeout, [this] { return !messages_.empty(); })) {
        out = std::move(messages_.front());
        messages_.pop_front();
        return WaitResult::Success;
    }
    return WaitResult::Timeout;
}

std::vector<NodeId> NodeFixture::connected_peers() const {
    std::vector<NodeId> result;
    // Iterate the admission session map under its lock via a snapshot. The
    // AdmissionController does not expose a snapshot, so we read session() for
    // known peers is not possible here; instead return empty and rely on
    // session()/wait_for_peer for connectivity checks. This is kept for API
    // symmetry but the test uses wait_for_peer.
    (void)result;
    return result;
}

std::string NodeFixture::diagnostics() const {
    std::ostringstream os;
    os << "node_id=" << node_id_.value()
       << " listen=" << listen_address_ << ":" << listen_port_
       << " running=" << (runtime_ ? runtime_->is_running() : false)
       << " connections=" << runtime_->admission().connection_count();
    return os.str();
}

std::uint64_t NodeFixture::now_ms() const noexcept {
    return ::feinstat::network::test::now_ms();
}

} // namespace feinstat::network::test
