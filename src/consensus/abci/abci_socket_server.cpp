#include "feinstat/consensus/abci/abci_socket_server.hpp"

#include "feinstat/consensus/abci/abci_codec.hpp"
#include "feinstat/consensus/abci/types.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

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
    stop();
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    // Clean up socket file.
    if (!config_.socket_path.empty()) {
        unlink(config_.socket_path.c_str());
    }
}

void AbciSocketServer::run() {
    running_ = true;

    listen_fd_ = create_unix_socket(config_.socket_path);

    accept_thread_ = std::thread([this]() { accept_loop(); });
}

void AbciSocketServer::stop() noexcept {
    running_ = false;
    if (listen_fd_ >= 0) {
        // Shutdown the listen socket to wake up accept(), then close.
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

bool AbciSocketServer::is_running() const noexcept {
    return running_;
}

const std::string& AbciSocketServer::socket_path() const noexcept {
    return config_.socket_path;
}

void AbciSocketServer::accept_loop() {
    while (running_) {
        struct sockaddr_un client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(listen_fd_,
                   reinterpret_cast<struct sockaddr*>(&client_addr),
                   &client_len);

        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }
            if (!running_) {
                break; // Socket was closed by stop()
            }
            std::cerr << "Accept failed: " << strerror(errno) << std::endl;
            continue;
        }

        // Handle connection in a dedicated thread.
        std::thread([client_fd, app = app_,
                     max_msg = config_.max_message_size]() {
            handle_connection(client_fd, app, max_msg);
        }).detach();
    }
}

} // namespace feinstat::consensus::abci
