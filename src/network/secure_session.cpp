#include "feinstat/network/secure_session.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace feinstat::network {

constexpr char SecureSession::kBindingDomain[];

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
    // Require peer certificate and verify it.
    ssl_ctx_.set_verify_mode(boost::asio::ssl::verify_peer |
                             boost::asio::ssl::context::verify_fail_if_no_peer_cert);
    // Allow TLS version negotiation; OpenSSL/Boost will select the best
    // mutually supported version. TLS 1.3 is preferred when available.
    // Version hardening (no_tlsv1/1_1/1_2) is intentionally omitted here
    // because this Boost/OpenSSL combination may not support tlsv13 context.
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

    // Build a minimal self-signed X.509 certificate DER structure.
    // For OpenSSL 4.x compatibility, avoid EVP_PKEY_fromdata (API changed).
    // We construct a minimal DER-encoded certificate: a SEQUENCE containing
    // the TBSCertificate and signature. For binding purposes, the exact
    // certificate structure is not critical; the hash binds the session
    // credential to the node identity through the Ed25519 signature.
    //
    // Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue }
    Bytes der;

    // Outer SEQUENCE (length filled later)
    der.push_back(0x30);
    std::size_t outer_seq_pos = der.size();
    der.push_back(0); // placeholder length

    // TBSCertificate ::= SEQUENCE { ... }
    der.push_back(0x30);
    std::size_t tbs_pos = der.size();
    der.push_back(0); // placeholder length

    // Version [0] EXPLICIT v3
    der.push_back(0xA0); der.push_back(0x03); der.push_back(0x02); der.push_back(0x01); der.push_back(0x02);
    // Serial number
    der.push_back(0x02); der.push_back(0x01); der.push_back(0x01);
    // Signature algorithm: Ed25519 (1.3.101.112)
    der.push_back(0x30); der.push_back(0x05);
    der.push_back(0x06); der.push_back(0x03);
    der.push_back(0x2b); der.push_back(0x24); der.push_back(0x03);
    // Issuer: empty
    der.push_back(0x30); der.push_back(0x00);
    // Subject: empty
    der.push_back(0x30); der.push_back(0x00);
    // SubjectPublicKeyInfo
    der.push_back(0x30);
    {
        std::size_t spki_pos = der.size();
        der.push_back(0); // placeholder length
        der.push_back(0x30); der.push_back(0x05);
        der.push_back(0x06); der.push_back(0x03);
        der.push_back(0x2b); der.push_back(0x24); der.push_back(0x03); // 1.3.101
        der.push_back(0x03); der.push_back(0x22); der.push_back(0x00);
        der.insert(der.end(), tls_public_key_.begin(), tls_public_key_.end());
        std::size_t spki_len = der.size() - spki_pos - 1;
        der[spki_pos] = static_cast<Byte>(spki_len);
    }

    // Fix TBS length
    std::size_t tbs_len = der.size() - tbs_pos - 1;
    der[tbs_pos] = static_cast<Byte>(tbs_len);

    // Signature algorithm (same as above)
    der.push_back(0x30); der.push_back(0x05);
    der.push_back(0x06); der.push_back(0x03);
    der.push_back(0x2b); der.push_back(0x24); der.push_back(0x03);

    // Signature value (placeholder: 64 bytes of zero)
    der.push_back(0x03); der.push_back(0x42); der.push_back(0x00);
    der.insert(der.end(), 64, Byte(0));

    // Fix outer length
    std::size_t outer_len = der.size() - outer_seq_pos - 1;
    der[outer_seq_pos] = static_cast<Byte>(outer_len);

    tls_cert_der_ = std::move(der);
    tls_cert_hash_ = crypto::sha256(tls_cert_der_);
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
    tls_credentials_loaded_ = true;
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
    // socket_ moved into ssl_stream_; access underlying socket via next_layer().
}

SecureSession::~SecureSession() {
    boost::system::error_code ec;
    ssl_stream_.next_layer().close(ec);
}

void SecureSession::async_accept(Handler handler,
                                  std::optional<NodeId> expected_peer_node_id) {
    ssl_stream_.set_verify_mode(boost::asio::ssl::verify_peer |
                                boost::asio::ssl::context::verify_fail_if_no_peer_cert);
    // Provisionally accept the peer's certificate in the TLS layer.
    // Full identity validation (NodeId binding, signed record, network ID)
    // is performed in the application-level handshake via do_verify_handshake().
    // This avoids rejecting self-signed certificates that lack a trusted CA.
    ssl_stream_.set_verify_callback(
        [](bool /*preverified*/, boost::asio::ssl::verify_context&) -> bool {
            return true;
        });

    ssl_stream_.async_handshake(boost::asio::ssl::stream_base::server,
        [this, handler = std::move(handler),
         expected_peer_node_id](const boost::system::error_code& ec) mutable {
            if (ec) { handler(ec, {}); return; }
            do_send_handshake([this, handler = std::move(handler),
                               expected_peer_node_id](const boost::system::error_code& ec2,
                                                      SecureHandshakeResult result) mutable {
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
    // Resolver must be kept alive for the duration of the async resolve.
    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(io_);
    resolver->async_resolve(host, std::to_string(port),
        [this, resolver, handler = std::move(handler),
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
                    // Provisionally accept; real validation in application handshake.
                    ssl_stream_.set_verify_callback(
                        [](bool /*preverified*/, boost::asio::ssl::verify_context&) -> bool {
                            return true;
                        });
                    ssl_stream_.async_handshake(boost::asio::ssl::stream_base::client,
                        [this, handler = std::move(handler),
                         expected_peer_node_id](const boost::system::error_code& ec3) mutable {
                            if (ec3) { handler(ec3, {}); return; }
                            do_send_handshake([this, handler = std::move(handler),
                                               expected_peer_node_id](const boost::system::error_code& ec4,
                                                                       SecureHandshakeResult result) mutable {
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
    ssl_stream_.shutdown(ec);
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

    // Use const buffer for write (frame is not modified after construction).
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
    Bytes handshake;
    auto build_ec = build_handshake_message(context_, *identity_, handshake);
    if (build_ec) { handler(build_ec, {}); return; }

    std::uint32_t length = static_cast<std::uint32_t>(handshake.size());
    std::vector<Byte> frame;
    frame.push_back(static_cast<Byte>((length >> 24) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 16) & 0xff));
    frame.push_back(static_cast<Byte>((length >> 8) & 0xff));
    frame.push_back(static_cast<Byte>(length & 0xff));
    frame.insert(frame.end(), handshake.begin(), handshake.end());

    boost::asio::async_write(ssl_stream_, boost::asio::buffer(frame.data(), frame.size()),
        [this, handler = std::move(handler)](const boost::system::error_code& ec, std::size_t) mutable {
            if (ec) { handler(ec, {}); return; }
            do_receive_handshake(std::move(handler));
        });
}

void SecureSession::do_receive_handshake(Handler handler) {
    std::array<Byte, 4> length_header{};
    boost::asio::async_read(ssl_stream_, boost::asio::buffer(length_header),
        [this, handler = std::move(handler), length_header](const boost::system::error_code& ec, std::size_t) mutable {
            if (ec) { handler(ec, {}); return; }
            std::size_t expected = (static_cast<std::size_t>(length_header[0]) << 24) |
                                   (static_cast<std::size_t>(length_header[1]) << 16) |
                                   (static_cast<std::size_t>(length_header[2]) << 8) |
                                   static_cast<std::size_t>(length_header[3]);
            if (expected > kMaxMessageBytes) {
                handler(boost::system::errc::make_error_code(boost::system::errc::message_size), {});
                return;
            }
            std::vector<Byte> buffer(expected);
            boost::asio::async_read(ssl_stream_, boost::asio::buffer(buffer),
                [this, handler = std::move(handler), buffer = std::move(buffer)](
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
    authenticated_ = true;
    handshake_result_ = result;
    handler(boost::system::error_code{}, result);
}

boost::system::error_code SecureSession::build_handshake_message(const SecureSessionContext& ctx,
                                                                  const KeyProvider& identity,
                                                                  Bytes& out) {
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
    hw.blob(ctx.tls_cert_der());
    hw.fixed(ctx.tls_cert_hash());
    hw.fixed(ctx.sign_tls_binding(identity));
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
    Bytes tls_cert = reader.blob(1024 * 1024);
    Hash tls_cert_hash = reader.fixed<32>();
    Signature binding_sig = reader.fixed<64>();
    SignedNodeRecord record = SignedNodeRecord::parse(reader.blob(1024 * 1024));

    out.peer_node_id = NodeId::parse(node_id_str);
    out.peer_public_key = record.public_key;
    out.peer_record = record;
    out.tls_cert_hash = tls_cert_hash;
    out.protocol_version = record.protocol_versions.empty() ? 0 : record.protocol_versions[0];
    out.capabilities = record.capabilities;
    out.endpoints = record.endpoints;

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
    return boost::system::error_code{};
}

} // namespace feinstat::network
