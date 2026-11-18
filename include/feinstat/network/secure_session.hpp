#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace feinstat::network {

class SecureSession;

// Result of a secure session handshake.
struct SecureHandshakeResult {
    NodeId peer_node_id;
    PublicKey peer_public_key;
    SignedNodeRecord peer_record;
    Hash tls_cert_hash;
    std::uint16_t protocol_version{0};
    std::vector<std::string> capabilities;
    std::vector<NodeEndpoint> endpoints;
    Hash session_nonce; // Session nonce bound to this TLS session (replay protection).
};

struct SecureSessionContextConfig {
    Bytes network_identifier;
    std::uint16_t protocol_version{8};
    std::vector<std::string> capabilities;
    std::vector<NodeEndpoint> endpoints;
    std::uint64_t record_expiry_unix_ms{0};
};

// TLS 1.3 secure session context. Self-signed X.509 certificate bound to FST
// NodeId via Ed25519 signature over the certificate hash. No central CA.
class SecureSessionContext {
public:
    SecureSessionContext(Bytes network_identifier,
                         std::uint16_t protocol_version,
                         std::vector<std::string> capabilities,
                         std::vector<NodeEndpoint> endpoints,
                         std::uint64_t record_expiry_unix_ms = 0);
    ~SecureSessionContext();

    SecureSessionContext(const SecureSessionContext&) = delete;
    SecureSessionContext& operator=(const SecureSessionContext&) = delete;
    SecureSessionContext(SecureSessionContext&&) noexcept;
    SecureSessionContext& operator=(SecureSessionContext&&) noexcept;

    void generate_tls_credentials();
    void load_tls_credentials(const std::string& cert_path,
                              const std::string& key_path);
    Signature sign_tls_binding(const KeyProvider& identity) const;

    [[nodiscard]] Hash tls_cert_hash() const noexcept { return tls_cert_hash_; }
    [[nodiscard]] const Bytes& tls_cert_der() const noexcept { return tls_cert_der_; }
    [[nodiscard]] const PublicKey& tls_public_key() const noexcept { return tls_public_key_; }

    [[nodiscard]] const Bytes& network_identifier() const noexcept { return network_identifier_; }
    [[nodiscard]] std::uint16_t protocol_version() const noexcept { return protocol_version_; }
    [[nodiscard]] const std::vector<std::string>& capabilities() const noexcept { return capabilities_; }
    [[nodiscard]] const std::vector<NodeEndpoint>& endpoints() const noexcept { return endpoints_; }
    [[nodiscard]] std::uint64_t record_expiry_unix_ms() const noexcept { return record_expiry_unix_ms_; }

    [[nodiscard]] boost::asio::ssl::context& ssl_context() noexcept { return ssl_ctx_; }
    [[nodiscard]] const boost::asio::ssl::context& ssl_context() const noexcept { return ssl_ctx_; }

private:
    Bytes network_identifier_;
    std::uint16_t protocol_version_;
    std::vector<std::string> capabilities_;
    std::vector<NodeEndpoint> endpoints_;
    std::uint64_t record_expiry_unix_ms_;

    boost::asio::ssl::context ssl_ctx_;
    Bytes tls_cert_der_;
    PublicKey tls_public_key_{};
    SecretKey tls_secret_key_{};
    Hash tls_cert_hash_{};
    bool tls_credentials_loaded_{false};
};

// A single authenticated peer session over TLS 1.3.
//
// All reads, writes, shutdowns and closes on the underlying ssl_stream_ are
// serialized on a single io_context strand bound to this session, so there is
// never concurrent access to the OpenSSL stream from multiple threads.
// send_message() is non-blocking: it enqueues to a per-session write queue and
// returns immediately. receive_message() is replaced by start_receive(), which
// runs an internal async read loop and delivers each frame to a caller-provided
// handler. The session derives enable_shared_from_this and must always be
// constructed via std::make_shared.
class SecureSession : public std::enable_shared_from_this<SecureSession> {
public:
    using Handler = std::function<void(boost::system::error_code, SecureHandshakeResult)>;

    // Called for every frame delivered by the internal async read loop. `ec`
    // is non-success on a read/close error; no further frames are delivered
    // after an error.
    using FrameHandler = std::function<void(std::shared_ptr<SecureSession>,
                                            std::uint16_t message_type,
                                            Bytes payload,
                                            boost::system::error_code& ec)>;

    // Callback fired once when the session is closed (read error, close(), or
    // shutdown). Used by the runtime to remove the session from admission
    // bookkeeping.
    using SessionClosedHandler = std::function<void(std::shared_ptr<SecureSession>)>;

    SecureSession(boost::asio::io_context& io,
                  SecureSessionContext& context,
                  std::shared_ptr<const KeyProvider> identity);
    ~SecureSession();

    SecureSession(const SecureSession&) = delete;
    SecureSession& operator=(const SecureSession&) = delete;
    SecureSession(SecureSession&&) = delete;
    SecureSession& operator=(SecureSession&&) = delete;

    // Socket access, for use by the acceptor before the session is bound to
    // its strand. Not safe to use once async operations are in flight.
    boost::asio::ip::tcp::socket& socket() noexcept { return ssl_stream_.next_layer(); }

    // Asynchronous accept (server side). Performs TLS 1.3 handshake, then
    // application-level identity handshake. All operations are non-blocking
    // and serialized on the session's strand.
    void async_accept(Handler handler,
                      std::optional<NodeId> expected_peer_node_id = std::nullopt);

    // Asynchronous connect (client side). Performs DNS resolution, TCP connect,
    // TLS 1.3 handshake, then application-level identity handshake. All
    // operations are non-blocking and serialized on the session's strand.
    void async_connect(const std::string& host,
                       std::uint16_t port,
                       Handler handler,
                       std::optional<NodeId> expected_peer_node_id = std::nullopt);

    // Close the session. Posts to the session's strand: cancels pending async
    // operations, shuts down the SSL stream, closes the socket, and fires
    // on_session_closed. Idempotent and safe to call from any thread.
    void close(boost::system::error_code& ec) noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_authenticated() const noexcept { return authenticated_.load(std::memory_order_acquire); }
    [[nodiscard]] const SecureHandshakeResult& handshake_result() const noexcept { return handshake_result_; }
    [[nodiscard]] const NodeId& node_id() const noexcept { return handshake_result_.peer_node_id; }

    // Non-blocking send. Enqueues the frame to the per-session write queue on
    // the session's strand and returns immediately; the frame is written as
    // soon as the drain reaches it. Returns the number of bytes enqueued (or 0
    // on failure, with ec set). Backpressure: if the queue is full, ec is
    // no_buffer_space and nothing is enqueued.
    std::size_t send_message(std::uint16_t message_type,
                             const Bytes& payload,
                             boost::system::error_code& ec) noexcept;

    // Start the internal async read loop. Each successfully read frame is
    // delivered to `handler` on the session's strand. Must be called after the
    // session is authenticated (e.g. from on_session_established).
    void start_receive(FrameHandler handler);

    // Set the callback fired once when the session is closed.
    void set_on_session_closed(SessionClosedHandler handler) { on_session_closed_ = std::move(handler); }

    // Test-accessible constants.
    static constexpr std::size_t test_kMaxMessageBytes() { return kMaxMessageBytes; }
    static constexpr std::size_t test_kFrameHeaderBytes() { return kFrameHeaderBytes; }
    static constexpr std::uint8_t test_kHandshakeMessageType() { return kHandshakeMessageType; }

    // Test-accessible handshake message construction / parsing (public for
    // unit tests that exercise the wire format directly).
    static boost::system::error_code build_handshake_message(const SecureSessionContext& ctx,
                                                              const KeyProvider& identity,
                                                              Bytes& out);
    static boost::system::error_code parse_handshake_message(const Bytes& data,
                                                              SecureHandshakeResult& out);

private:
    void do_send_handshake(Handler handler,
                           std::optional<NodeId> expected_peer_node_id);
    void do_receive_handshake(Handler handler);
    void do_verify_handshake(const SecureHandshakeResult& result,
                             std::optional<NodeId> expected_peer_node_id,
                             Handler handler);

    // Read loop (all run on the session's strand).
    void read_header();
    void read_payload(std::uint16_t message_type,
                      std::uint32_t length,
                      Hash expected_checksum);
    void deliver_frame(std::uint16_t message_type, Bytes payload);
    void on_read_error(const boost::system::error_code& ec);

    // Write queue (all run on the session's strand).
    struct WriteItem {
        std::uint16_t type;
        Bytes payload;
    };
    void enqueue_and_drain(Bytes frame);
    void write_next();
    void on_write_complete(std::shared_ptr<Bytes> payload, const boost::system::error_code& ec, std::size_t);

    void close_locked() noexcept;

    Bytes build_frame(std::uint16_t message_type, const Bytes& payload) const;

    boost::asio::io_context& io_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream_;
    boost::asio::io_context::strand strand_;
    SecureSessionContext& context_;
    std::shared_ptr<const KeyProvider> identity_;

    std::atomic<bool> authenticated_{false};
    std::atomic<bool> closed_{false};
    std::atomic<std::size_t> write_queue_size_{0};
    std::atomic<bool> live_tls_cert_hash_known_{false};

    SecureHandshakeResult handshake_result_{};
    Hash live_tls_cert_hash_{};

    FrameHandler frame_handler_;
    SessionClosedHandler on_session_closed_;

    std::deque<WriteItem> write_queue_;
    bool write_draining_ = false;
    std::size_t write_queue_max_ = 256;

    static constexpr std::size_t kMaxMessageBytes = 1U * 1024U * 1024U;
    static constexpr std::size_t kFrameHeaderBytes = 4U + 2U + 32U;
    static constexpr std::uint8_t kHandshakeMessageType = 0;
    static constexpr char kBindingDomain[] = "FST TLS Binding v1";
    static constexpr char kSessionDomain[] = "FST TLS Session v1";
};

} // namespace feinstat::network
