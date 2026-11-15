#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
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
class SecureSession {
public:
    using Handler = std::function<void(boost::system::error_code, SecureHandshakeResult)>;

    SecureSession(boost::asio::io_context& io,
                  SecureSessionContext& context,
                  std::shared_ptr<const KeyProvider> identity);
    ~SecureSession();

    SecureSession(const SecureSession&) = delete;
    SecureSession& operator=(const SecureSession&) = delete;
    SecureSession(SecureSession&&) = delete;
    SecureSession& operator=(SecureSession&&) = delete;

    boost::asio::ip::tcp::socket& socket() noexcept { return ssl_stream_.next_layer(); }
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& ssl_stream() noexcept { return ssl_stream_; }

    void async_accept(Handler handler,
                      std::optional<NodeId> expected_peer_node_id = std::nullopt);
    void async_connect(const std::string& host,
                       std::uint16_t port,
                       Handler handler,
                       std::optional<NodeId> expected_peer_node_id = std::nullopt);

    void close(boost::system::error_code& ec) noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_authenticated() const noexcept { return authenticated_; }
    [[nodiscard]] const SecureHandshakeResult& handshake_result() const noexcept { return handshake_result_; }

    std::size_t send_message(std::uint16_t message_type,
                             const Bytes& payload,
                             boost::system::error_code& ec) noexcept;
    std::size_t receive_message(std::uint16_t& message_type,
                                Bytes& payload,
                                boost::system::error_code& ec) noexcept;

    [[nodiscard]] boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream() noexcept { return ssl_stream_; }

    // Test-accessible handshake message construction and parsing.
    static boost::system::error_code build_handshake_message(const SecureSessionContext& ctx,
                                                             const KeyProvider& identity,
                                                             Bytes& out);
    static boost::system::error_code parse_handshake_message(const Bytes& data,
                                                             SecureHandshakeResult& out);

private:
    void do_send_handshake(Handler handler);
    void do_receive_handshake(Handler handler);
    void do_verify_handshake(const SecureHandshakeResult& result,
                             std::optional<NodeId> expected_peer_node_id,
                             Handler handler);

    boost::asio::io_context& io_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream_;
    SecureSessionContext& context_;
    std::shared_ptr<const KeyProvider> identity_;

    bool authenticated_{false};
    SecureHandshakeResult handshake_result_{};

public:
    // Test-accessible constants.
    static constexpr std::size_t test_kMaxMessageBytes() { return kMaxMessageBytes; }
    static constexpr std::size_t test_kFrameHeaderBytes() { return kFrameHeaderBytes; }
    static constexpr std::uint8_t test_kHandshakeMessageType() { return kHandshakeMessageType; }

private:
    static constexpr std::size_t kMaxMessageBytes = 1U * 1024U * 1024U;
    static constexpr std::size_t kFrameHeaderBytes = 4U + 2U + 32U;
    static constexpr std::uint8_t kHandshakeMessageType = 0;
    static constexpr char kBindingDomain[] = "FST TLS Binding v1";
};

} // namespace feinstat::network
