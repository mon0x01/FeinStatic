#include "feinstat/consensus/abci/abci_socket_server.hpp"

#include "feinstat/consensus/abci/abci_codec.hpp"
#include "feinstat/consensus/abci/types.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace feinstat::consensus::abci {

namespace {

// Socket file permissions: owner read/write only.
constexpr mode_t kSocketMode = 0600;

// Maximum number of pending connections in the listen backlog.
constexpr int kListenBacklog = 16;

// Remove a stale socket file safely. Uses lstat to check for symlinks and
// only removes regular files or symlinks, never directories.
void remove_stale_socket(const std::string& path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return; // No existing file
    }
    // Only remove if it's a regular file or symlink (not a directory).
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        if (unlink(path.c_str()) != 0) {
            std::cerr << "Warning: failed to remove stale socket file '"
                      << path << "': " << strerror(errno) << std::endl;
        }
    }
}

// Create a Unix-domain socket with restrictive permissions.
int create_unix_socket(const std::string& path) {
    remove_stale_socket(path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::runtime_error(
            "failed to create Unix-domain socket: " +
            std::string(strerror(errno)));
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        close(fd);
        throw std::runtime_error("socket path too long");
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) != 0) {
        close(fd);
        remove_stale_socket(path);
        throw std::runtime_error(
            "failed to bind Unix-domain socket: " +
            std::string(strerror(errno)));
    }

    // Set restrictive permissions on the socket file.
    if (chmod(path.c_str(), kSocketMode) != 0) {
        std::cerr << "Warning: failed to set socket permissions: "
                  << strerror(errno) << std::endl;
    }

    if (listen(fd, kListenBacklog) != 0) {
        close(fd);
        remove_stale_socket(path);
        throw std::runtime_error(
            "failed to listen on Unix-domain socket: " +
            std::string(strerror(errno)));
    }

    return fd;
}

//------------------------------------------------------------------------------
// Lifecycle synchronization
//
// `running_` is std::atomic<bool> (declared in the header). `listen_fd_` is a
// plain int, so every access to it (run() create/assign, stop() shutdown/close,
// accept_loop() accept) is serialized by listen_fd_mutex_. This prevents
// stop() from closing or reusing listen_fd_ while accept_loop() is inside
// accept() — the stop()/run() data races reported by ThreadSanitizer
// (FST-BUG-0019 / VDEF-002).
//
// Connection handler threads are tracked in connection_threads_ so stop() can
// join them before returning: no connection thread outlives server teardown.
// Both are file-local because the class header (not editable here) declares no
// synchronization members; all instances share the locks, which is correct for
// the single CometBFT socket server per process used by FST.
std::mutex listen_fd_mutex_;
std::mutex connection_threads_mutex_;
std::vector<std::thread> connection_threads_;

//------------------------------------------------------------------------------
// Socket creation
//------------------------------------------------------------------------------

// Handle a single ABCI connection: read requests, dispatch to app, write
// responses.
void handle_connection(int fd,
                       std::shared_ptr<AbciApplication> app,
                       std::size_t max_message_size) {
    while (true) {
        try {
            // Read framed request.
            tendermint::abci::Request proto_req =
                read_framed_message<tendermint::abci::Request>(
                    fd, max_message_size);
            (void)proto_req;

            // Convert to FST-native.
            Request req = from_protobuf(proto_req);

            // Dispatch to application.
            Response resp;
            switch (req.kind) {
                case RequestKind::Echo: {
                    auto echo_resp = app->echo(req.echo);
                    resp.kind = ResponseKind::Echo;
                    resp.echo = echo_resp;
                    break;
                }
                case RequestKind::Flush:
                    resp.kind = ResponseKind::Flush;
                    resp.flush = ResponseFlush{};
                    break;
                case RequestKind::Info: {
                    auto info_resp = app->info(req.info);
                    resp.kind = ResponseKind::Info;
                    resp.info = info_resp;
                    break;
                }
                case RequestKind::InitChain: {
                    auto ic_resp = app->init_chain(req.init_chain);
                    resp.kind = ResponseKind::InitChain;
                    resp.init_chain = ic_resp;
                    break;
                }
                case RequestKind::Query: {
                    auto q_resp = app->query(req.query);
                    resp.kind = ResponseKind::Query;
                    resp.query = q_resp;
                    break;
                }
                case RequestKind::CheckTx: {
                    auto ct_resp = app->check_tx(req.check_tx);
                    resp.kind = ResponseKind::CheckTx;
                    resp.check_tx = ct_resp;
                    break;
                }
                case RequestKind::Commit: {
                    auto c_resp = app->commit(req.commit);
                    resp.kind = ResponseKind::Commit;
                    resp.commit = c_resp;
                    break;
                }
                case RequestKind::ListSnapshots: {
                    auto ls_resp = app->list_snapshots(req.list_snapshots);
                    resp.kind = ResponseKind::ListSnapshots;
                    resp.list_snapshots = ls_resp;
                    break;
                }
                case RequestKind::OfferSnapshot: {
                    auto os_resp = app->offer_snapshot(req.offer_snapshot);
                    resp.kind = ResponseKind::OfferSnapshot;
                    resp.offer_snapshot = os_resp;
                    break;
                }
                case RequestKind::LoadSnapshotChunk: {
                    auto lsc_resp = app->load_snapshot_chunk(
                        req.load_snapshot_chunk);
                    resp.kind = ResponseKind::LoadSnapshotChunk;
                    resp.load_snapshot_chunk = lsc_resp;
                    break;
                }
                case RequestKind::ApplySnapshotChunk: {
                    auto asc_resp = app->apply_snapshot_chunk(
                        req.apply_snapshot_chunk);
                    resp.kind = ResponseKind::ApplySnapshotChunk;
                    resp.apply_snapshot_chunk = asc_resp;
                    break;
                }
                case RequestKind::PrepareProposal: {
                    auto pp_resp = app->prepare_proposal(req.prepare_proposal);
                    resp.kind = ResponseKind::PrepareProposal;
                    resp.prepare_proposal = pp_resp;
                    break;
                }
                case RequestKind::ProcessProposal: {
                    auto pp_resp = app->process_proposal(req.process_proposal);
                    resp.kind = ResponseKind::ProcessProposal;
                    resp.process_proposal = pp_resp;
                    break;
                }
                case RequestKind::ExtendVote: {
                    auto ev_resp = app->extend_vote(req.extend_vote);
                    resp.kind = ResponseKind::ExtendVote;
                    resp.extend_vote = ev_resp;
                    break;
                }
                case RequestKind::VerifyVoteExtension: {
                    auto vve_resp = app->verify_vote_extension(
                        req.verify_vote_extension);
                    resp.kind = ResponseKind::VerifyVoteExtension;
                    resp.verify_vote_extension = vve_resp;
                    break;
                }
                case RequestKind::FinalizeBlock: {
                    auto fb_resp = app->finalize_block(req.finalize_block);
                    resp.kind = ResponseKind::FinalizeBlock;
                    resp.finalize_block = fb_resp;
                    break;
                }
                case RequestKind::InsertTx: {
                    auto itx_resp = app->insert_tx(req.insert_tx);
                    resp.kind = ResponseKind::InsertTx;
                    resp.insert_tx = itx_resp;
                    break;
                }
                case RequestKind::ReapTxs: {
                    auto rt_resp = app->reap_txs(req.reap_txs);
                    resp.kind = ResponseKind::ReapTxs;
                    resp.reap_txs = rt_resp;
                    break;
                }
                case RequestKind::Unknown:
                    throw std::runtime_error("unknown ABCI request kind");
            }

            // Convert to protobuf and write framed response.
            tendermint::abci::Response proto_resp = to_protobuf(resp);
            write_framed_message(fd, proto_resp);

        } catch (const std::runtime_error& e) {
            // Connection error or EOF - close the connection.
            break;
        } catch (const std::exception& e) {
            // Application error - send exception response.
            tendermint::abci::Response proto_resp;
            proto_resp.mutable_exception()->set_error(e.what());
            try {
                write_framed_message(fd, proto_resp);
            } catch (...) {
                break;
            }
        }
    }

    close(fd);
}

} // namespace

//------------------------------------------------------------------------------
// AbciSocketServer
//------------------------------------------------------------------------------

AbciSocketServer::AbciSocketServer(Config config,
                                   std::shared_ptr<AbciApplication> app)
    : config_(std::move(config)), app_(std::move(app)) {}

AbciSocketServer::~AbciSocketServer() {
    // stop() joins the accept loop and all connection handler threads before
    // returning, so no thread outlives server teardown.
    stop();
    // Clean up socket file.
    if (!config_.socket_path.empty()) {
        unlink(config_.socket_path.c_str());
    }
}

void AbciSocketServer::run() {
    // Create and bind the listening socket first, then publish it under the
    // lock and only then mark the server running, so accept_loop() never
    // observes running_==true with an invalid listen_fd_.
    int fd = create_unix_socket(config_.socket_path);

    {
        std::lock_guard<std::mutex> lk(listen_fd_mutex_);
        listen_fd_ = fd;
    }

    running_.store(true, std::memory_order_release);

    accept_thread_ = std::thread([this]() { accept_loop(); });
}

void AbciSocketServer::stop() noexcept {
    // Signal shutdown first so accept_loop() exits its loop.
    running_.store(false, std::memory_order_release);

    // Take the listen fd out of service under the lock: shutdown wakes a
    // blocked accept(), close releases the fd. No other thread accesses
    // listen_fd_ without the lock, so this cannot race with run() or
    // accept_loop().
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(listen_fd_mutex_);
        fd = listen_fd_;
        listen_fd_ = -1;
    }
    if (fd >= 0) {
        // Errors are ignored: the fd may already have been closed.
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }

    // Join the accept loop: after this returns no thread reads listen_fd_ or
    // running_, which eliminates the stop()/run() races on abci_socket_server.cpp:270/:273/:274.
    if (accept_thread_.joinable()) {
        try {
            accept_thread_.join();
        } catch (...) {
            // stop() is noexcept; a join failure cannot propagate.
        }
    }

    // Join all connection handler threads so none outlives server teardown.
    std::vector<std::thread> connections;
    {
        std::lock_guard<std::mutex> lk(connection_threads_mutex_);
        connections.swap(connection_threads_);
    }
    for (auto& t : connections) {
        if (t.joinable()) {
            try {
                t.join();
            } catch (...) {
                // stop() is noexcept; a join failure cannot propagate.
            }
        }
    }
}

bool AbciSocketServer::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

const std::string& AbciSocketServer::socket_path() const noexcept {
    return config_.socket_path;
}

void AbciSocketServer::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        // Snapshot the listen fd under the lock: stop() may close it
        // concurrently, and we must never call accept() on a closed fd.
        int fd = -1;
        {
            std::lock_guard<std::mutex> lk(listen_fd_mutex_);
            fd = listen_fd_;
        }
        if (fd < 0) {
            if (!running_.load(std::memory_order_acquire)) break;
            continue;
        }

        struct sockaddr_un client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(fd,
                   reinterpret_cast<struct sockaddr*>(&client_addr),
                   &client_len);

        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }
            // ECONNABORTED/EBADF/EINVAL: listen socket shut down by stop().
            if (!running_.load(std::memory_order_acquire)) break;
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        // Handle connection in a dedicated thread and track it so stop()
        // can join it before returning.
        std::thread t([client_fd, app = app_,
                       max_msg = config_.max_message_size]() {
            handle_connection(client_fd, app, max_msg);
        });
        {
            std::lock_guard<std::mutex> lk(connection_threads_mutex_);
            connection_threads_.push_back(std::move(t));
        }
    }
}

} // namespace feinstat::consensus::abci
