#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/secure_session.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() { return Bytes(32, 0x42); }

} // namespace

FS_TEST(secure_session_tls_context_generates_credentials) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    FS_REQUIRE(ctx.tls_cert_der().size() > 0);
    FS_REQUIRE(ctx.tls_public_key() != PublicKey{});
    FS_REQUIRE(ctx.tls_cert_hash() != Hash{});
    FS_REQUIRE(ctx.protocol_version() == 8);
    FS_REQUIRE(ctx.network_identifier() == make_network_id());
}

FS_TEST(secure_session_tls_binding_signature_verifies) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    auto identity = Ed25519KeyProvider::generate();
    Signature sig = ctx.sign_tls_binding(identity);

    ByteWriter binding_writer;
    binding_writer.string("FST TLS Binding v1");
    binding_writer.blob(ctx.tls_cert_der());
    Bytes binding_msg = std::move(binding_writer).take();

    FS_REQUIRE(verify_ed25519(identity.public_key(), binding_msg, sig));
    FS_REQUIRE(!verify_ed25519(Ed25519KeyProvider::generate().public_key(), binding_msg, sig));
}

FS_TEST(secure_session_handshake_message_round_trip) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    auto identity = Ed25519KeyProvider::generate();

    Bytes handshake_data;
    auto ec = SecureSession::build_handshake_message(ctx, identity, handshake_data);
    FS_REQUIRE(!ec);
    FS_REQUIRE(handshake_data.size() > 0);

    SecureHandshakeResult result;
    ec = SecureSession::parse_handshake_message(handshake_data, result);
    FS_REQUIRE(!ec);
    FS_REQUIRE(result.peer_node_id == identity.node_id());
    FS_REQUIRE(result.peer_public_key == identity.public_key());
    FS_REQUIRE(result.tls_cert_hash == ctx.tls_cert_hash());
    FS_REQUIRE(result.protocol_version == 8);
    FS_REQUIRE(result.capabilities == std::vector<std::string>{"peer"});
    FS_REQUIRE(result.endpoints.size() == 1);
    FS_REQUIRE(result.endpoints[0].host == "127.0.0.1");
    FS_REQUIRE(result.endpoints[0].port == 30331);
}

FS_TEST(secure_session_handshake_rejects_wrong_tls_cert_hash) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    auto identity = Ed25519KeyProvider::generate();

    // Build a valid handshake, then corrupt the TLS cert hash.
    Bytes handshake_data;
    auto ec = SecureSession::build_handshake_message(ctx, identity, handshake_data);
    FS_REQUIRE(!ec);

    // Parse and tamper: replace the cert hash field.
    ByteReader reader(handshake_data);
    (void)reader.u8();           // message type
    (void)reader.blob(32);       // network id
    (void)reader.string(128);    // node id string
    (void)reader.u16();          // protocol version
    (void)reader.blob(1024 * 1024); // tls cert der
    // Now at cert hash position - we can't easily modify via reader,
    // so rebuild with wrong hash manually.
    ByteWriter hw;
    hw.u8(0);
    hw.blob(make_network_id());
    hw.string(identity.node_id().value());
    hw.u16(8);
    hw.blob(ctx.tls_cert_der());
    Hash wrong_hash{};
    wrong_hash.fill(0xab);
    hw.fixed(wrong_hash);
    hw.fixed(ctx.sign_tls_binding(identity));
    Hash session_nonce{};
    session_nonce.fill(0xcd);
    hw.fixed(session_nonce);
    Signature session_sig{};
    session_sig.fill(0xef);
    hw.fixed(session_sig);

    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = make_network_id();
    record.node_id = identity.node_id();
    record.key_algorithm = 1;
    record.public_key = identity.public_key();
    record.sequence = 1;
    record.protocol_versions = {8};
    record.capabilities = {"peer"};
    record.endpoints = {{TransportType::Tcp, "127.0.0.1", 30331}};
    record.expiry_unix_ms = 0;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = identity.sign(std::move(dw).take());
    hw.blob(record.serialize());

    SecureHandshakeResult result;
    auto ec2 = SecureSession::parse_handshake_message(std::move(hw).take(), result);
    FS_REQUIRE(static_cast<bool>(ec2));
}

FS_TEST(secure_session_handshake_rejects_bad_binding_signature) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    auto identity = Ed25519KeyProvider::generate();
    auto other = Ed25519KeyProvider::generate();

    ByteWriter hw;
    hw.u8(0);
    hw.blob(make_network_id());
    hw.string(identity.node_id().value());
    hw.u16(8);
    hw.blob(ctx.tls_cert_der());
    hw.fixed(ctx.tls_cert_hash());
    // Sign with the WRONG identity key.
    hw.fixed(other.sign_domain("FST TLS Binding v1",
        [&ctx] {
            ByteWriter w;
            w.string("FST TLS Binding v1");
            w.blob(ctx.tls_cert_der());
            return std::move(w).take();
        }()));
    Hash session_nonce{};
    session_nonce.fill(0xcd);
    hw.fixed(session_nonce);
    Signature session_sig{};
    session_sig.fill(0xef);
    hw.fixed(session_sig);

    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = make_network_id();
    record.node_id = identity.node_id();
    record.key_algorithm = 1;
    record.public_key = identity.public_key();
    record.sequence = 1;
    record.protocol_versions = {8};
    record.capabilities = {"peer"};
    record.endpoints = {{TransportType::Tcp, "127.0.0.1", 30331}};
    record.expiry_unix_ms = 0;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = identity.sign(std::move(dw).take());
    hw.blob(record.serialize());

    SecureHandshakeResult result;
    auto ec = SecureSession::parse_handshake_message(std::move(hw).take(), result);
    FS_REQUIRE(static_cast<bool>(ec));
}

FS_TEST(secure_session_handshake_rejects_node_id_mismatch) {
    SecureSessionContext ctx(make_network_id(), 8, {"peer"},
                             {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    ctx.generate_tls_credentials();

    auto identity = Ed25519KeyProvider::generate();
    auto other = Ed25519KeyProvider::generate();

    // Build handshake with identity A but node ID of identity B.
    ByteWriter hw;
    hw.u8(0);
    hw.blob(make_network_id());
    hw.string(other.node_id().value()); // wrong node ID
    hw.u16(8);
    hw.blob(ctx.tls_cert_der());
    hw.fixed(ctx.tls_cert_hash());
    hw.fixed(ctx.sign_tls_binding(identity));
    Hash session_nonce{};
    session_nonce.fill(0xcd);
    hw.fixed(session_nonce);
    Signature session_sig{};
    session_sig.fill(0xef);
    hw.fixed(session_sig);

    SignedNodeRecord record{};
    record.format_version = 1;
    record.network_identifier = make_network_id();
    record.node_id = identity.node_id(); // record says identity A
    record.key_algorithm = 1;
    record.public_key = identity.public_key();
    record.sequence = 1;
    record.protocol_versions = {8};
    record.capabilities = {"peer"};
    record.endpoints = {{TransportType::Tcp, "127.0.0.1", 30331}};
    record.expiry_unix_ms = 0;
    ByteWriter dw;
    dw.string(SignedNodeRecord::kSignDomain);
    dw.blob(record.canonical_body());
    record.signature = identity.sign(std::move(dw).take());
    hw.blob(record.serialize());

    SecureHandshakeResult result;
    auto ec = SecureSession::parse_handshake_message(std::move(hw).take(), result);
    FS_REQUIRE(static_cast<bool>(ec));
}

FS_TEST(secure_session_message_framing_constants) {
    FS_REQUIRE(SecureSession::test_kMaxMessageBytes() == 1U * 1024U * 1024U);
    FS_REQUIRE(SecureSession::test_kFrameHeaderBytes() == 4U + 2U + 32U);
    FS_REQUIRE(SecureSession::test_kHandshakeMessageType() == 0);
}
