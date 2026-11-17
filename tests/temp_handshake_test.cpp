#include "test.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/secure_session.hpp"

#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <thread>

using namespace feinstat;
using namespace feinstat::crypto;
using namespace feinstat::network;

namespace {

Bytes make_network_id() { return Bytes(32, 0x42); }

class TestKeyProvider : public KeyProvider {
public:
    TestKeyProvider(PublicKey pk, SecretKey sk) : pk_(pk), sk_(sk) {}
    PublicKey public_key() const override { return pk_; }
    NodeId node_id() const override { return NodeId::from_public_key(pk_); }
    Signature sign(const Bytes& message) const override {
        return crypto::sign_ed25519(sk_, message);
    }
    Signature sign_domain(const std::string& domain, const Bytes& message) const override {
        ByteWriter w;
        w.string(domain);
        w.blob(message);
        return crypto::sign_ed25519(sk_, std::move(w).take());
    }
private:
    PublicKey pk_;
    SecretKey sk_;
};

} // namespace

FS_TEST(temp_handshake_live_test) {
    Bytes network_id = make_network_id();
    SecureSessionContext tls_a(network_id, 8, {"peer"},
        {{TransportType::Tcp, "127.0.0.1", 30331}}, 0);
    tls_a.generate_tls_credentials();
    SecureSessionContext tls_b(network_id, 8, {"peer"},
        {{TransportType::Tcp, "127.0.0.1", 30332}}, 0);
    tls_b.generate_tls_credentials();

    auto pair_a = crypto::generate_ed25519_keypair();
    auto pair_b = crypto::generate_ed25519_keypair();
    std::shared_ptr<const KeyProvider> id_a = std::make_shared<TestKeyProvider>(pair_a.public_key, pair_a.secret_key);
    std::shared_ptr<const KeyProvider> id_b = std::make_shared<TestKeyProvider>(pair_b.public_key, pair_b.secret_key);

    // Use two independent io_contexts so the server and client never share an
    // io.stop()/io.run() race. The server's io_context runs only server
    // handlers; the client's runs only client handlers.
    boost::asio::io_context io_server;
    boost::asio::io_context io_client;

    auto acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(io_server);
    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::make_address("127.0.0.1"), 0);
    acceptor->open(ep.protocol());
    acceptor->set_option(boost::asio::socket_base::reuse_address(true));
    acceptor->bind(ep);
    acceptor->listen();
    auto port = acceptor->local_endpoint().port();

    std::atomic<bool> server_done{false};
    std::atomic<bool> client_done{false};
    boost::system::error_code server_ec;
    boost::system::error_code client_ec;

    std::thread server_thread([&]() {
        auto session = std::make_shared<SecureSession>(io_server, tls_b, id_b);
        acceptor->async_accept(session->socket(),
            [&](const boost::system::error_code& ec) {
                if (ec) { server_ec = ec; server_done = true; io_server.stop(); return; }
                session->async_accept(
                    [&](const boost::system::error_code& hec, SecureHandshakeResult result) {
                        server_ec = hec;
                        std::cout << "[server] handshake done: ec=" << hec.message() << std::endl;
                        if (!hec) {
                            std::cout << "[server] authenticated peer=" << result.peer_node_id.value() << std::endl;
                        }
                        server_done = true;
                        io_server.stop();
                    }, std::nullopt);
            });
        io_server.run();
        // session is destroyed here, only after io_server.run() has fully
        // drained and all handshake callbacks have completed. No handler can
        // be writing to session->handshake_result_ at this point.
    });

    // Give the server time to start listening before the client connects.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto client_session = std::make_shared<SecureSession>(io_client, tls_a, id_a);
    client_session->async_connect("127.0.0.1", port,
        [&](const boost::system::error_code& ec, SecureHandshakeResult result) {
            client_ec = ec;
            std::cout << "[client] connect done: ec=" << ec.message() << std::endl;
            if (!ec) {
                std::cout << "[client] authenticated peer=" << result.peer_node_id.value() << std::endl;
            }
            client_done = true;
            io_client.stop();
        }, std::nullopt);

    io_client.run();
    server_thread.join();

    std::cout << "server_ec=" << server_ec.message() << " client_ec=" << client_ec.message() << std::endl;
    FS_REQUIRE(!server_ec);
    FS_REQUIRE(!client_ec);
}