#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/secure_session.hpp"

#include <boost/system/error_code.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace feinstat::network {

// A single authenticated peer session, wrapping a SecureSession with peer
// metadata (lifecycle tracking, last-seen, protocol version, capabilities).
class PeerSession {
public:
    PeerSession(std::shared_ptr<SecureSession> session,
                SecureHandshakeResult handshake_result,
                std::uint64_t connected_at_unix_ms);

    ~PeerSession();

    PeerSession(const PeerSession&) = delete;
    PeerSession& operator=(const PeerSession&) = delete;
    PeerSession(PeerSession&&) noexcept;
    PeerSession& operator=(PeerSession&&) noexcept;

    SecureSession& secure_session() noexcept { return *session_; }
    [[nodiscard]] const SecureSession& secure_session() const noexcept { return *session_; }

    [[nodiscard]] const NodeId& peer_node_id() const noexcept { return handshake_result_.peer_node_id; }
    [[nodiscard]] const PublicKey& peer_public_key() const noexcept { return handshake_result_.peer_public_key; }
    [[nodiscard]] const SignedNodeRecord& peer_record() const noexcept { return handshake_result_.peer_record; }
    [[nodiscard]] const Hash& tls_cert_hash() const noexcept { return handshake_result_.tls_cert_hash; }
    [[nodiscard]] std::uint16_t protocol_version() const noexcept { return handshake_result_.protocol_version; }
    [[nodiscard]] const std::vector<std::string>& capabilities() const noexcept { return handshake_result_.capabilities; }
    [[nodiscard]] const std::vector<NodeEndpoint>& endpoints() const noexcept { return handshake_result_.endpoints; }

    [[nodiscard]] std::uint64_t connected_at_unix_ms() const noexcept { return connected_at_unix_ms_; }
    [[nodiscard]] std::uint64_t last_seen_unix_ms() const noexcept { return last_seen_unix_ms_; }
    [[nodiscard]] bool is_authenticated() const noexcept { return session_->is_authenticated(); }
    [[nodiscard]] bool is_open() const noexcept { return session_->is_open(); }

    void touch(std::uint64_t now_unix_ms) noexcept { last_seen_unix_ms_ = now_unix_ms; }

    std::size_t send_message(std::uint16_t message_type,
                             const Bytes& payload,
                             boost::system::error_code& ec) noexcept {
        return session_->send_message(message_type, payload, ec);
    }

    std::size_t receive_message(std::uint16_t& message_type,
                                Bytes& payload,
                                boost::system::error_code& ec) noexcept {
        return session_->receive_message(message_type, payload, ec);
    }

    void close(boost::system::error_code& ec) noexcept { session_->close(ec); }

private:
    std::shared_ptr<SecureSession> session_;
    SecureHandshakeResult handshake_result_;
    std::uint64_t connected_at_unix_ms_{0};
    std::uint64_t last_seen_unix_ms_{0};
};

} // namespace feinstat::network
