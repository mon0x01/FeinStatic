#include "feinstat/network/secure_session.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/node_record.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace feinstat::network {

//------------------------------------------------------------------------------
// SecureSessionContext
//------------------------------------------------------------------------------

SecureSessionContext::SecureSessionContext(
    Bytes network_identifier,
    std::uint16_t protocol_version,
    std::vector<std::string> capabilities,
    std::vector<NodeEndpoint> endpoints,
    std::uint64_t record_expiry_unix_ms)
    : network_identifier_(std::move(network_identifier)),
      protocol_version_(protocol_version),
      capabilities_(std::move(capabilities)),
      endpoints_(std::move(endpoints)),
      record_expiry_unix_ms_(record_expiry_unix_ms),
      ssl_ctx_(boost::asio::ssl::context::tlsv13) {
    ssl_ctx_.set_options(boost::asio::ssl::context::no_tlsv1 |
                         boost::asio::ssl::context::no_tlsv1_1 |
                         boost::asio::ssl::context::no_tlsv1_2);
    ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer |
                             boost::asio::ssl::context::verify_fail_if_no_peer_cert);
}

SecureSessionContext::~SecureSessionContext() {
    OPENSSL_cleanse(tls_secret_key_.data(), tls_secret_key_.size());
    tls_public_key_.fill(0);
}

SecureSessionContext::SecureSessionContext(SecureSessionContext&& other) noexcept
    : network_identifier_(std::move(other.network_identifier_)),
      protocol_version_(other.protocol_version_),
      capabilities_(std::move(other.capabilities_)),
      endpoints_(std::move(other.endpoints_)),
      record_expiry_unix_ms_(other.record_expiry_unix_ms_),
      ssl_ctx_(std::move(other.ssl_ctx_)),
      tls_cert_der_(std::move(other.tls_cert_der_)),
      tls_public_key_(other.tls_public_key_),
      tls_secret_key_(other.tls_secret_key_),
      tls_cert_hash_(other.tls_cert_hash_),
      tls_credentials_loaded_(other.tls_credentials_loaded_) {
    other.tls_public_key_.fill(0);
    OPENSSL_cleanse(other.tls_secret_key_.data(), other.tls_secret_key_.size());
    other.tls_secret_key_.fill(0);
    other.tls_cert_hash_.fill(0);
    other.tls_credentials_loaded_ = false;
}

SecureSessionContext& SecureSessionContext::operator=(SecureSessionContext&& other) noexcept {
    if (this != &other) {
        OPENSSL_cleanse(tls_secret_key_.data(), tls_secret_key_.size());
        network_identifier_ = std::move(other.network_identifier_);
        protocol_version_ = other.protocol_version_;
        capabilities_ = std::move(other.capabilities_);
        endpoints_ = std::move(other.endpoints_);
        record_expiry_unix_ms_ = other.record_expiry_unix_ms_;
        ssl_ctx_ = std::move(other.ssl_ctx_);
        tls_cert_der_ = std::move(other.tls_cert_der_);
        tls_public_key_ = other.tls_public_key_;
        tls_secret_key_ = other.tls_secret_key_;
        tls_cert_hash_ = other.tls_cert_hash_;
        tls_credentials_loaded_ = other.tls_credentials_loaded_;
        other.tls_public_key_.fill(0);
        OPENSSL_cleanse(other.tls_secret_key_.data(), other.tls_secret_key_.size());
        other.tls_secret_key_.fill(0);
        other.tls_cert_hash_.fill(0);
        other.tls_credentials_loaded_ = false;
    }
    return *this;
}

void SecureSessionContext::generate_tls_credentials() {
    auto pair = crypto::generate_ed25519_keypair();
    tls_public_key_ = pair.public_key;
    tls_secret_key_ = pair.secret_key;

    EVP_PKEY* evp_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                      tls_secret_key_.data(),
                                                      tls_secret_key_.size());
    if (!evp_key) throw std::runtime_error("failed to create EVP_PKEY from private key");

    X509* cert = X509_new();
    if (!cert) { EVP_PKEY_free(evp_key); throw std::runtime_error("failed to create X509"); }

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 365L * 24L * 60L * 60L);

    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (const unsigned char*)"FST Network", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char*)crypto::hex(tls_public_key_.data(),
                                                                  tls_public_key_.size()).c_str(),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_set_subject_name(cert, name);
    X509_NAME_free(name);

    X509_set_pubkey(cert, evp_key);

    if (X509_sign(cert, evp_key, nullptr) <= 0) {
        X509_free(cert);
        EVP_PKEY_free(evp_key);
        throw std::runtime_error("failed to sign TLS certificate");
    }

    int der_len = i2d_X509(cert, nullptr);
    if (der_len <= 0) {
        X509_free(cert);
        EVP_PKEY_free(evp_key);
        throw std::runtime_error("failed to serialize TLS certificate");
    }
    tls_cert_der_.resize(static_cast<std::size_t>(der_len));
    unsigned char* der_write_ptr = tls_cert_der_.data();
    der_len = i2d_X509(cert, &der_write_ptr);
    tls_cert_der_.resize(static_cast<std::size_t>(der_len));

    tls_cert_hash_ = crypto::sha256(tls_cert_der_);

    if (SSL_CTX_use_certificate(ssl_ctx_.native_handle(), cert) <= 0) {
        X509_free(cert);
        EVP_PKEY_free(evp_key);
        throw std::runtime_error("failed to load TLS certificate into SSL context");
    }

    if (SSL_CTX_use_PrivateKey(ssl_ctx_.native_handle(), evp_key) <= 0) {
        X509_free(cert);
        EVP_PKEY_free(evp_key);
        throw std::runtime_error("failed to load TLS private key into SSL context");
    }

    if (SSL_CTX_check_private_key(ssl_ctx_.native_handle()) <= 0) {
        X509_free(cert);
        EVP_PKEY_free(evp_key);
        throw std::runtime_error("TLS certificate and private key do not match");
    }

    X509_free(cert);
    EVP_PKEY_free(evp_key);
    tls_credentials_loaded_ = true;
}

void SecureSessionContext::load_tls_credentials(const std::string& cert_path,
                                                 const std::string& key_path) {
    ssl_ctx_.use_certificate_file(cert_path, boost::asio::ssl::context::pem);
    ssl_ctx_.use_private_key_file(key_path, boost::asio::ssl::context::pem);

    std::ifstream cert_file(cert_path, std::ios::binary);
    if (!cert_file) throw std::runtime_error("cannot open TLS certificate file");
    std::string pem((std::istreambuf_iterator<char>(cert_file)),
                     std::istreambuf_iterator<char>());

    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (!bio) throw std::runtime_error("failed to create BIO for certificate");
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) throw std::runtime_error("failed to parse TLS certificate");

    int der_len = i2d_X509(cert, nullptr);
    if (der_len <= 0) { X509_free(cert); throw std::runtime_error("failed to serialize cert"); }
    tls_cert_der_.resize(static_cast<std::size_t>(der_len));
    unsigned char* der_ptr = tls_cert_der_.data();
    der_len = i2d_X509(cert, &der_ptr);
    tls_cert_der_.resize(static_cast<std::size_t>(der_len));
    X509_free(cert);

    tls_cert_hash_ = crypto::sha256(tls_cert_der_);
}

Signature SecureSessionContext::sign_tls_binding(const KeyProvider& identity) const {
    if (!tls_credentials_loaded_) throw std::logic_error("TLS credentials not loaded");
    return identity.sign_domain("FST TLS Binding v1", tls_cert_der_);
}

//------------------------------------------------------------------------------
// SecureSession
//------------------------------------------------------------------------------

SecureSession::SecureSession(boost::asio::io_context& io,
                             SecureSessionContext& context,
                             std::shared_ptr<const KeyProvider> identity)
    : io_(io), socket_(io),
      ssl_stream_(std::move(socket_), context.ssl_context()),
      context_(context), identity_(std::move(identity)) {
}

SecureSession::~SecureSession() {
    boost::system::error_code ec;
    ssl_stream_.next_layer().close(ec);
}

void SecureSession::async_accept(Handler handler,
                                  std::optional<NodeId> expected_peer_node_id) {
    ssl_stream_.set_verify_mode(boost::asio::ssl::verify_peer |
                                boost::asio::ssl::context::verify_fail_if_no_peer_cert);
    ssl_stream_.set_verify_callback(
        [this](bool /*preverified*/, boost::asio::ssl::verify_context& ctx) -> bool {
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (cert) {
                int der_len = i2d_X509(cert, nullptr);
                if (der_len > 0) {
                    Bytes der(static_cast<std::size_t>(der_len));
                    unsigned char* ptr = der.data();
                    der_len = i2d_X509(cert, &ptr);
                    der.resize(static_cast<std::size_t>(der_len));
                    live_tls_cert_hash_ = crypto::sha256(der);
                    live_tls_cert_hash_known_ = true;
                }
            }
            return true;
        });

    ssl_stream_.async_handshake(boost::asio::ssl::stream_base::server,
        [this, handler = std::move(handler),
         expected_peer_node_id](const boost::system::error_code& ec) mutable {
            if (ec) { handler(ec, {}); return; }
do_send_handshake([this, handler = std::move(handler),
                       expected_peer_node_id](const boost::system::error_code& ec2,
                                              SecureHandshakeResult /*result*/) mutable {
                if (ec2) { handler(ec2, {}); return; }
                do_receive_handshake([this, handler = std::move(handler),
                                      expected_peer_node_id](const boost::system::error_code& ec3,
                                                             SecureHandshakeResult peer_result) mutable {
                    if (ec3) { handler(ec3, {}); return; }
                    do_verify_handshake(peer_result, expected_peer_node_id,
                                        [handler = std::move(handler)](const boost::system::error_code& ec4,
                                                                        SecureHandshakeResult result) {
                                            handler(ec4, result);
                                        });
                });
            });
        });
}

void SecureSession::async_connect(const std::string& host,
                                   std::uint16_t port,
                                   Handler handler,
                                   std::optional<NodeId> expected_peer_node_id) {
    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(io_);
    resolver->async_resolve(host, std::to_string(port),
        [this, resolver = std::move(resolver), handler = std::move(handler),
         expected_peer_node_id](const boost::system::error_code& ec,
                                boost::asio::ip::tcp::resolver::results_type endpoints) mutable {
            if (ec) { handler(ec, {}); return; }
            boost::asio::async_connect(ssl_stream_.next_layer(), endpoints,
                [this, handler = std::move(handler),
                 expected_peer_node_id](const boost::system::error_code& ec2,
                                         const boost::asio::ip::tcp::endpoint&) mutable {
                    if (ec2) { handler(ec2, {}); return; }
                    ssl_stream_.next_layer().set_option(boost::asio::ip::tcp::no_delay(true));
                    ssl_stream_.set_verify_mode(boost::asio::ssl::verify_peer |
                                                boost::asio::ssl::context::verify_fail_if_no_peer_cert);
                    ssl_stream_.set_verify_callback(
                        [this](bool /*preverified*/, boost::asio::ssl::verify_context& ctx) -> bool {
                            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
                            if (cert) {
                                int der_len = i2d_X509(cert, nullptr);
                                if (der_len > 0) {
                                    Bytes der(static_cast<std::size_t>(der_len));
                                    unsigned char* ptr = der.data();
                                    der_len = i2d_X509(cert, &ptr);
                                    der.resize(static_cast<std::size_t>(der_len));
                                    live_tls_cert_hash_ = crypto::sha256(der);
                                    live_tls_cert_hash_known_ = true;
                                }
                            }
                            return true;
                        });
                    ssl_stream_.async_handshake(boost::asio::ssl::stream_base::client,
                        [this, handler = std::move(handler),
                         expected_peer_node_id](const boost::system::error_code& ec3) mutable {
                            if (ec3) { handler(ec3, {}); return; }
do_send_handshake([this, handler = std::move(handler),
                       expected_peer_node_id](const boost::system::error_code& ec4,
                                              SecureHandshakeResult /*result*/) mutable {
                                if (ec4) { handler(ec4, {}); return; }
                                do_receive_handshake([this, handler = std::move(handler),
                                                      expected_peer_node_id](const boost::system::error_code& ec5,
                                                                             SecureHandshakeResult peer_result) mutable {
                                    if (ec5) { handler(ec5, {}); return; }
                                    do_verify_handshake(peer_result, expected_peer_node_id,
                                        [handler = std::move(handler)](const boost::system::error_code& ec6,
                                                                        SecureHandshakeResult result) {
                                            handler(ec6, result);
                                        });
                                });
                            });
                        });
                });
        });
}

void SecureSession::close(boost::system::error_code& ec) noexcept {
    // Shut down the underlying transport rather than performing a TLS-level
    // close_notify handshake. A TLS shutdown would block waiting for a peer
    // that may not reciprocate, and would race with a dispatch worker that is
    // blocked in a synchronous read on this same stream. A transport shutdown
    // unblocks any blocked read/write and never waits on the peer.
    ssl_stream_.next_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    ssl_stream_.next_layer().close(ec);
}

bool SecureSession::is_open() const noexcept {
    return ssl_stream_.next_layer().is_open();
}

std::size_t SecureSession::send_message(std::uint16_t message_type,
                                         const Bytes& payload,
                                         boost::system::error_code& ec) noexcept {
    if (payload.size() > kMaxMessageBytes) {
        ec = boost::system::errc::make_error_code(boost::system::errc::message_size);
        return 0;
    }
    Bytes frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    frame.push_back(static_cast<Byte>((length >> 24) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 16) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 8) & 0xff));
    frame.push_back(static_cast<Byte>(length & 0xff));
    frame.push_back(static_cast<Byte>((message_type >> 8) & 0xff));
    frame.push_back(static_cast<Byte>(message_type & 0xff));
    Hash checksum = crypto::sha256(payload);
    frame.insert(frame.end(), checksum.begin(), checksum.end());
    frame.insert(frame.end(), payload.begin(), payload.end());

    boost::asio::write(ssl_stream_, boost::asio::buffer(frame.data(), frame.size()), ec);
    if (!ec) return frame.size();
    return 0;
}

std::size_t SecureSession::receive_message(std::uint16_t& message_type,
                                            Bytes& payload,
                                            boost::system::error_code& ec) noexcept {
    std::array<Byte, kFrameHeaderBytes> header{};
    boost::asio::read(ssl_stream_, boost::asio::buffer(header), ec);
    if (ec) return 0;

    std::uint32_t length = (static_cast<std::uint32_t>(header[0]) << 24) |
                           (static_cast<std::uint32_t>(header[1]) << 16) |
                           (static_cast<std::uint32_t>(header[2]) << 8) |
                           static_cast<std::uint32_t>(header[3]);
    if (length > kMaxMessageBytes) {
        ec = boost::system::errc::make_error_code(boost::system::errc::message_size);
        return 0;
    }
    message_type = static_cast<std::uint16_t>((static_cast<std::uint16_t>(header[4]) << 8) |
                                               header[5]);

    Hash expected_checksum{};
    std::copy(header.begin() + 6, header.begin() + 38, expected_checksum.begin());

    payload.resize(length);
    if (!payload.empty()) {
        boost::asio::read(ssl_stream_, boost::asio::buffer(payload), ec);
        if (ec) return 0;
    }

    Hash actual = crypto::sha256(payload);
    if (actual != expected_checksum) {
        ec = boost::system::errc::make_error_code(boost::system::errc::protocol_error);
        return 0;
    }
    return payload.size();
}

void SecureSession::do_send_handshake(Handler handler) {
    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = context_.network_identifier();
    record.node_id = identity_->node_id();
    record.key_algorithm = 1;
    record.public_key = identity_->public_key();
    record.sequence = 1;
    record.protocol_versions = {context_.protocol_version()};
    record.capabilities = context_.capabilities();
    record.endpoints = context_.endpoints();
    record.expiry_unix_ms = context_.record_expiry_unix_ms() ? context_.record_expiry_unix_ms() : 0;

    ByteWriter dw;
    dw.string(std::string(SignedNodeRecord::kSignDomain));
    dw.blob(record.canonical_body());
    record.signature = identity_->sign_domain(std::string(SignedNodeRecord::kSignDomain),
                                              std::move(dw).take());

    Hash session_nonce{};
    if (RAND_bytes(session_nonce.data(), static_cast<int>(session_nonce.size())) != 1) {
        handler(boost::system::errc::make_error_code(boost::system::errc::io_error), {});
        return;
    }

    ByteWriter hw;
    hw.u8(kHandshakeMessageType);
    hw.blob(context_.network_identifier());
    hw.string(identity_->node_id().value());
    hw.u16(context_.protocol_version());
    hw.blob(context_.tls_cert_der());
    hw.fixed(context_.tls_cert_hash());
    hw.fixed(context_.sign_tls_binding(*identity_));
    hw.fixed(session_nonce);

    ByteWriter session_sig_writer;
    session_sig_writer.string(kSessionDomain);
    session_sig_writer.fixed(session_nonce);
    session_sig_writer.blob(context_.network_identifier());
    session_sig_writer.u16(context_.protocol_version());
    session_sig_writer.string(identity_->node_id().value());
    session_sig_writer.fixed(context_.tls_cert_hash());
    Bytes session_sig_msg = std::move(session_sig_writer).take();
    hw.fixed(identity_->sign(session_sig_msg));

    hw.blob(record.serialize());

    Bytes handshake = std::move(hw).take();

    std::uint32_t length = static_cast<std::uint32_t>(handshake.size());
    std::vector<Byte> frame;
    frame.push_back(static_cast<Byte>((length >> 24) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 16) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 8) & 0xff));
    frame.push_back(static_cast<Byte>(length & 0xff));
    frame.insert(frame.end(), handshake.begin(), handshake.end());

    // The async_write buffer and the completion handler's ownership of the frame
    // must be established without relying on argument evaluation order. If the
    // move-into-lambda capture were evaluated before buffer(frame.data(), ...) the
    // buffer would reference moved-from storage (nullptr, 0) and async_write would
    // complete with success while writing zero bytes, leaving the peer's read
    // blocked forever. Establish the buffer first, then move the frame into the
    // handler so the buffer remains valid for the lifetime of the async operation.
    auto write_buffer = boost::asio::buffer(frame);
    boost::asio::async_write(ssl_stream_, std::move(write_buffer),
        [this, frame = std::move(frame), handler = std::move(handler),
         session_nonce](const boost::system::error_code& ec, std::size_t) mutable {
            (void)frame;
            if (ec) { handler(ec, {}); return; }
            // Record our session nonce so it survives until the peer's handshake
            // is received and verified by the caller (async_accept/async_connect).
            handshake_result_.session_nonce = session_nonce;
            handler(boost::system::error_code{}, {});
        });
}

void SecureSession::do_receive_handshake(Handler handler) {
    auto length_header = std::make_shared<std::array<Byte, 4>>();
    boost::asio::async_read(ssl_stream_, boost::asio::buffer(*length_header),
        [this, handler = std::move(handler), length_header](const boost::system::error_code& ec, std::size_t) mutable {
            if (ec) { handler(ec, {}); return; }
            std::size_t expected = (static_cast<std::size_t>((*length_header)[0]) << 24) |
                                   (static_cast<std::size_t>((*length_header)[1]) << 16) |
                                   (static_cast<std::size_t>((*length_header)[2]) << 8) |
                                   static_cast<std::size_t>((*length_header)[3]);
            if (expected > kMaxMessageBytes) {
                handler(boost::system::errc::make_error_code(boost::system::errc::message_size), {});
                return;
            }
            std::vector<Byte> buffer(expected);
            // Same argument-evaluation-order hazard as do_send_handshake: build
            // the read buffer before moving the storage into the completion
            // handler so the async_read cannot reference moved-from memory.
            auto read_buffer = boost::asio::buffer(buffer);
            boost::asio::async_read(ssl_stream_, std::move(read_buffer),
                [handler = std::move(handler), buffer = std::move(buffer)](
                    const boost::system::error_code& ec2, std::size_t) mutable {
                    if (ec2) { handler(ec2, {}); return; }
                    SecureHandshakeResult parsed_result;
                    auto parse_ec = parse_handshake_message(buffer, parsed_result);
                    if (parse_ec) { handler(parse_ec, {}); return; }
                    handler(boost::system::error_code{}, std::move(parsed_result));
                });
        });
}

void SecureSession::do_verify_handshake(const SecureHandshakeResult& result,
                                         std::optional<NodeId> expected_peer_node_id,
                                         Handler handler) {
    if (!result.peer_node_id.matches(result.peer_public_key)) {
        handler(boost::system::errc::make_error_code(boost::system::errc::protocol_error), {});
        return;
    }
    if (expected_peer_node_id && result.peer_node_id != *expected_peer_node_id) {
        handler(boost::system::errc::make_error_code(boost::system::errc::protocol_error), {});
        return;
    }
    if (!live_tls_cert_hash_known_) {
        handler(boost::system::errc::make_error_code(boost::system::errc::protocol_error), {});
        return;
    }
    if (result.tls_cert_hash != live_tls_cert_hash_) {
        handler(boost::system::errc::make_error_code(boost::system::errc::protocol_error), {});
        return;
    }

    authenticated_ = true;
    handshake_result_ = result;
    handler(boost::system::error_code{}, result);
}

boost::system::error_code SecureSession::build_handshake_message(const SecureSessionContext& ctx,
                                                                  const KeyProvider& identity,
                                                                  Bytes& out) {
    Hash session_nonce{};
    if (RAND_bytes(session_nonce.data(), static_cast<int>(session_nonce.size())) != 1) {
        return boost::system::errc::make_error_code(boost::system::errc::io_error);
    }

    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = ctx.network_identifier();
    record.node_id = identity.node_id();
    record.key_algorithm = 1;
    record.public_key = identity.public_key();
    record.sequence = 1;
    record.protocol_versions = {ctx.protocol_version()};
    record.capabilities = ctx.capabilities();
    record.endpoints = ctx.endpoints();
    record.expiry_unix_ms = ctx.record_expiry_unix_ms() ? ctx.record_expiry_unix_ms() : 0;

    ByteWriter dw;
    dw.string(std::string(SignedNodeRecord::kSignDomain));
    dw.blob(record.canonical_body());
    record.signature = identity.sign_domain(std::string(SignedNodeRecord::kSignDomain),
                                            std::move(dw).take());

    ByteWriter hw;
    hw.u8(kHandshakeMessageType);
    hw.blob(ctx.network_identifier());
    hw.string(identity.node_id().value());
    hw.u16(ctx.protocol_version());
    hw.blob(ctx.tls_cert_der());
    hw.fixed(ctx.tls_cert_hash());
    hw.fixed(ctx.sign_tls_binding(identity));
    hw.fixed(session_nonce);

    ByteWriter session_sig_writer;
    session_sig_writer.string(kSessionDomain);
    session_sig_writer.fixed(session_nonce);
    session_sig_writer.blob(ctx.network_identifier());
    session_sig_writer.u16(ctx.protocol_version());
    session_sig_writer.string(identity.node_id().value());
    session_sig_writer.fixed(ctx.tls_cert_hash());
    Bytes session_sig_msg = std::move(session_sig_writer).take();
    hw.fixed(identity.sign(session_sig_msg));

    hw.blob(record.serialize());

    out = std::move(hw).take();
    return boost::system::error_code{};
}

boost::system::error_code SecureSession::parse_handshake_message(const Bytes& data,
                                                                  SecureHandshakeResult& out) {
    ByteReader reader(data);
    std::uint8_t msg_type = reader.u8();
    if (msg_type != kHandshakeMessageType) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }
    Bytes network_id = reader.blob(32);
    std::string node_id_str = reader.string(128);
    std::uint16_t peer_protocol_version = reader.u16();
    Bytes tls_cert = reader.blob(1024 * 1024);
    Hash tls_cert_hash = reader.fixed<32>();
    Signature binding_sig = reader.fixed<64>();
    Hash session_nonce = reader.fixed<32>();
    Signature session_sig = reader.fixed<64>();
    SignedNodeRecord record = SignedNodeRecord::parse(reader.blob(1024 * 1024));

    out.peer_node_id = NodeId::parse(node_id_str);
    out.peer_public_key = record.public_key;
    out.peer_record = record;
    out.tls_cert_hash = tls_cert_hash;
    out.protocol_version = peer_protocol_version;
    out.capabilities = record.capabilities;
    out.endpoints = record.endpoints;
    out.session_nonce = session_nonce;

    if (!out.peer_node_id.matches(out.peer_public_key)) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }
    if (crypto::sha256(tls_cert) != tls_cert_hash) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }

    ByteWriter binding_writer;
    binding_writer.string(kBindingDomain);
    binding_writer.blob(tls_cert);
    Bytes binding_msg = std::move(binding_writer).take();
    if (!crypto::verify_ed25519(out.peer_public_key, binding_msg, binding_sig)) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }

    ByteWriter session_writer;
    session_writer.string(kSessionDomain);
    session_writer.fixed(session_nonce);
    session_writer.blob(network_id);
    session_writer.u16(peer_protocol_version);
    session_writer.string(node_id_str);
    session_writer.fixed(tls_cert_hash);
    Bytes session_msg = std::move(session_writer).take();
    if (!crypto::verify_ed25519(out.peer_public_key, session_msg, session_sig)) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }

    return boost::system::error_code{};
}

} // namespace feinstat::network
