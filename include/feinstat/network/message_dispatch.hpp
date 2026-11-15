#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"
#include "feinstat/network/peer_exchange.hpp"
#include "feinstat/network/peer_session.hpp"
#include "feinstat/network/peer_store.hpp"

#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace feinstat::network {

enum class PeerMessageType : std::uint16_t {
    kReserved = 0,
    kProtocolNegotiate = 1,
    kProtocolNegotiateAck = 2,
    kNodeIdentity = 3,
    kNodeIdentityAck = 4,
    kPing = 5,
    kPong = 6,
    kPeerRecordAnnounce = 7,
    kPeerRecordRequest = 8,
    kPeerRecordResponse = 9,
    kCapabilityAdvertise = 10,
    kCapabilityAck = 11,
    kDisconnect = 12,
    kError = 255,
};

inline constexpr std::size_t kMaxPingPayloadBytes = 64;
inline constexpr std::size_t kMaxPeerRecordBytes = 64 * 1024;
inline constexpr std::size_t kMaxCapabilityAdvertiseBytes = 256;
inline constexpr std::size_t kMaxErrorBytes = 1024;

using MessageHandler = std::function<void(std::shared_ptr<PeerSession>,
                                           PeerMessageType,
                                           const Bytes&,
                                           boost::system::error_code&)>;

// Versioned, bounded message-dispatch path over authenticated peer sessions.
// All messages have size limits, validation, and resource controls.
class MessageDispatcher {
public:
    struct Config {
        std::uint16_t protocol_version{8};
        std::vector<std::string> capabilities;
        std::size_t max_message_queue{1024};
    };

    MessageDispatcher(SecureSessionContext& tls_context,
                      PeerStore& store,
                      PeerExchange& exchange,
                      Config config);

    ~MessageDispatcher();

    MessageDispatcher(const MessageDispatcher&) = delete;
    MessageDispatcher& operator=(const MessageDispatcher&) = delete;
    MessageDispatcher(MessageDispatcher&&) = delete;
    MessageDispatcher& operator=(MessageDispatcher&&) = delete;

    void register_handler(PeerMessageType type, MessageHandler handler);

    std::size_t send(std::shared_ptr<PeerSession> session,
                     PeerMessageType type,
                     const Bytes& payload,
                     boost::system::error_code& ec) noexcept;

    std::size_t dispatch(std::shared_ptr<PeerSession> session,
                         boost::system::error_code& ec) noexcept;

    std::size_t ping(std::shared_ptr<PeerSession> session,
                     boost::system::error_code& ec) noexcept;

    std::size_t disconnect(std::shared_ptr<PeerSession> session,
                           boost::system::error_code& ec) noexcept;

    [[nodiscard]] std::uint16_t protocol_version() const noexcept { return config_.protocol_version; }

private:
    std::size_t validate_message(PeerMessageType type, const Bytes& payload) const noexcept;

    SecureSessionContext& tls_context_;
    PeerStore& store_;
    PeerExchange& exchange_;
    Config config_;
    std::map<PeerMessageType, MessageHandler> handlers_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point> ping_times_;
};

} // namespace feinstat::network
