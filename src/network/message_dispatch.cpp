#include "feinstat/network/message_dispatch.hpp"

#include <chrono>
#include <iostream>

namespace feinstat::network {

MessageDispatcher::MessageDispatcher(SecureSessionContext& tls_context,
                                      PeerStore& store,
                                      PeerExchange& exchange,
                                      Config config)
    : tls_context_(tls_context), store_(store), exchange_(exchange),
      config_(std::move(config)) {}

MessageDispatcher::~MessageDispatcher() = default;

void MessageDispatcher::register_handler(PeerMessageType type, MessageHandler handler) {
    handlers_[type] = std::move(handler);
}

std::size_t MessageDispatcher::validate_message(PeerMessageType type,
                                                 const Bytes& payload) const noexcept {
    switch (type) {
        case PeerMessageType::kPing:
            return payload.size() > kMaxPingPayloadBytes ? payload.size() : 0;
        case PeerMessageType::kPeerRecordAnnounce:
        case PeerMessageType::kPeerRecordResponse:
            return payload.size() > kMaxPeerRecordBytes ? payload.size() : 0;
        case PeerMessageType::kCapabilityAdvertise:
            return payload.size() > kMaxCapabilityAdvertiseBytes ? payload.size() : 0;
        case PeerMessageType::kError:
            return payload.size() > kMaxErrorBytes ? payload.size() : 0;
        default:
            return payload.size() > kMaxPeerRecordBytes ? payload.size() : 0;
    }
}

std::size_t MessageDispatcher::send(std::shared_ptr<PeerSession> session,
                                     PeerMessageType type,
                                     const Bytes& payload,
                                     boost::system::error_code& ec) noexcept {
    if (!session || !session->is_authenticated()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
        return 0;
    }
    if (validate_message(type, payload) != 0) {
        ec = boost::system::errc::make_error_code(boost::system::errc::message_size);
        return 0;
    }
    return session->send_message(static_cast<std::uint16_t>(type), payload, ec);
}

std::size_t MessageDispatcher::dispatch(std::shared_ptr<PeerSession> session,
                                         boost::system::error_code& ec) noexcept {
    if (!session || !session->is_authenticated()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
        return 0;
    }

    std::uint16_t raw_type{0};
    Bytes payload;
    std::size_t received = session->receive_message(raw_type, payload, ec);
    if (ec || received == 0) return received;

    PeerMessageType type = static_cast<PeerMessageType>(raw_type);

    if (validate_message(type, payload) != 0) {
        ec = boost::system::errc::make_error_code(boost::system::errc::message_size);
        return 0;
    }

    auto it = handlers_.find(type);
    if (it == handlers_.end()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::protocol_error);
        return 0;
    }

    it->second(session, type, payload, ec);
    return received;
}

std::size_t MessageDispatcher::ping(std::shared_ptr<PeerSession> session,
                                     boost::system::error_code& ec) noexcept {
    auto now = std::chrono::steady_clock::now();
    ping_times_[reinterpret_cast<std::uint64_t>(session.get())] = now;
    Bytes payload(8, 0);
    auto ns = now.time_since_epoch().count();
    for (int i = 0; i < 8; ++i) {
        payload[i] = static_cast<Byte>((ns >> (56 - i * 8)) & 0xff);
    }
    return send(session, PeerMessageType::kPing, payload, ec);
}

std::size_t MessageDispatcher::disconnect(std::shared_ptr<PeerSession> session,
                                           boost::system::error_code& ec) noexcept {
    return send(session, PeerMessageType::kDisconnect, {}, ec);
}

} // namespace feinstat::network
