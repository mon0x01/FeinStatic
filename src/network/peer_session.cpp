#include "feinstat/network/peer_session.hpp"

namespace feinstat::network {

PeerSession::PeerSession(std::shared_ptr<SecureSession> session,
                          SecureHandshakeResult handshake_result,
                          std::uint64_t connected_at_unix_ms)
    : session_(std::move(session)),
      handshake_result_(std::move(handshake_result)),
      connected_at_unix_ms_(connected_at_unix_ms),
      last_seen_unix_ms_(connected_at_unix_ms) {}

PeerSession::~PeerSession() {
    boost::system::error_code ec;
    session_->close(ec);
}

PeerSession::PeerSession(PeerSession&& other) noexcept
    : session_(std::move(other.session_)),
      handshake_result_(std::move(other.handshake_result_)),
      connected_at_unix_ms_(other.connected_at_unix_ms_),
      last_seen_unix_ms_(other.last_seen_unix_ms_) {}

PeerSession& PeerSession::operator=(PeerSession&& other) noexcept {
    if (this != &other) {
        session_ = std::move(other.session_);
        handshake_result_ = std::move(other.handshake_result_);
        connected_at_unix_ms_ = other.connected_at_unix_ms_;
        last_seen_unix_ms_ = other.last_seen_unix_ms_;
    }
    return *this;
}

} // namespace feinstat::network
