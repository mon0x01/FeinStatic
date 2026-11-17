#pragma once

#include "feinstat/consensus/abci/application.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace feinstat::consensus::abci {

// Unix-domain socket ABCI server for CometBFT v0.40.0.
//
// The server listens on a local Unix-domain socket and speaks the Tendermint
// Socket Protocol (TSP): varint length-delimited protobuf messages. It accepts
// connections from a single CometBFT process and routes ABCI requests to the
// deterministic FstAbciApplication.
//
// Security properties:
//   - Socket file created with mode 0600 (owner read/write only)
//   - Socket file created with O_EXCL|O_NOFOLLOW to prevent symlink attacks
//   - Stale socket files are removed before binding (unlink, not symlink follow)
//   - No TCP endpoint is exposed; only local Unix-domain socket
//   - Maximum message size bounded to prevent memory exhaustion
//
// Threading model:
//   - Accept loop runs on a dedicated thread
//   - Each connection gets a read thread and a write thread
//   - Application calls are serialized by a mutex (ABCI app is not
//     thread-safe; CometBFT sends one request at a time per connection)
class AbciSocketServer {
public:
    // Configuration for the socket server.
    struct Config {
        std::string socket_path;      // Unix-domain socket path
        std::size_t max_message_size{16U * 1024U * 1024U}; // 16 MB
        std::size_t response_buffer_size{1000};            // response queue depth
    };

    AbciSocketServer(Config config,
                     std::shared_ptr<AbciApplication> app);

    ~AbciSocketServer();

    // Start listening and accepting connections. Blocks until stop() is called.
    void run();

    // Signal the server to stop. Causes run() to return.
    void stop() noexcept;

    // Returns true if the server is currently running.
    [[nodiscard]] bool is_running() const noexcept;

    // Returns the socket path being used.
    [[nodiscard]] const std::string& socket_path() const noexcept;

private:
    void accept_loop();

    Config config_;
    std::shared_ptr<AbciApplication> app_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    int listen_fd_{-1};
};

} // namespace feinstat::consensus::abci
