#include "feinstat/network/message_dispatch.hpp"

#include <chrono>
#include <iostream>

#include <boost/system/errc.hpp>

namespace feinstat::network {

MessageDispatcher::MessageDispatcher(SecureSessionContext& tls_context,
                                      PeerStore& store,
                                      PeerExchange& exchange,
                                      Config config)
    : tls_context_(tls_context), store_(store), exchange_(exchange),
      config_(std::move(config)) {
    // tls_context_, store_, and exchange_ are stored for future use;
    // they are intentionally not referenced in the current implementation.
    (void)tls_context_;
    (void)store_;
    (void)exchange_;
}

MessageDispatcher::~MessageDispatcher() = default;

void MessageDispatcher::register_handler(PeerMessageType type, MessageHandler handler) {
    handlers_[type] = std::move(handler);
}

void MessageDispatcher::add_observer(MessageObserver observer) {
    observers_.push_back(std::move(observer));
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

namespace {

boost::system::error_code encode_error(boost::system::errc::errc_t code) noexcept {
    return boost::system::errc::make_error_code(code);
}

} // namespace

Bytes encode_peer_record_request(const std::vector<NodeId>& requested) {
    ByteWriter writer;
    writer.u32(static_cast<std::uint32_t>(requested.size()));
    for (const NodeId& id : requested) {
        writer.string(id.value());
    }
    return std::move(writer).take();
}

boost::system::error_code decode_peer_record_request(const Bytes& data,
                                                      std::vector<NodeId>& requested) {
    requested.clear();
    ByteReader reader(data);
    std::uint32_t count;
    try {
        count = reader.u32();
    } catch (const std::exception&) {
        return encode_error(boost::system::errc::protocol_error);
    }
    if (count > kMaxPeerRecordMessages) {
        return encode_error(boost::system::errc::message_size);
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string id_text;
        try {
            id_text = reader.string(128);
        } catch (const std::exception&) {
            return encode_error(boost::system::errc::protocol_error);
        }
        try {
            requested.push_back(NodeId::parse(id_text));
        } catch (const std::exception&) {
            return encode_error(boost::system::errc::protocol_error);
        }
    }
    if (!reader.empty()) {
        return encode_error(boost::system::errc::protocol_error);
    }
    return boost::system::error_code{};
}

Bytes encode_peer_record_response(const std::vector<SignedNodeRecord>& records) {
    // Filter oversized records first so the count matches the records actually
    // written (records are validated before sharing). Each record is written
    // with ByteWriter::blob, which already prefixes its own u32 length.
    std::vector<Bytes> serialized_records;
    serialized_records.reserve(records.size());
    for (const SignedNodeRecord& record : records) {
        const Bytes serialized = record.serialize();
        if (serialized.size() > kMaxRecordBytes) {
            continue; // records are validated before sharing; skip oversized
        }
        serialized_records.push_back(std::move(serialized));
    }

    ByteWriter writer;
    writer.u32(static_cast<std::uint32_t>(serialized_records.size()));
    for (const Bytes& serialized : serialized_records) {
        writer.blob(serialized);
    }
    return std::move(writer).take();
}

boost::system::error_code decode_peer_record_response(const Bytes& data,
                                                       std::vector<SignedNodeRecord>& records) {
    records.clear();
    ByteReader reader(data);
    std::uint32_t count;
    try {
        count = reader.u32();
    } catch (const std::exception&) {
        return encode_error(boost::system::errc::protocol_error);
    }
    if (count > kMaxPeerRecordMessages) {
        return encode_error(boost::system::errc::message_size);
    }
    std::size_t cumulative = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        Bytes record_bytes;
        try {
            record_bytes = reader.blob(kMaxRecordBytes);
        } catch (const std::exception&) {
            return encode_error(boost::system::errc::protocol_error);
        }
        cumulative += record_bytes.size();
        if (cumulative > kMaxPeerRecordBytes) {
            return encode_error(boost::system::errc::message_size);
        }
        try {
            records.push_back(SignedNodeRecord::parse(record_bytes));
        } catch (const std::exception&) {
            return encode_error(boost::system::errc::protocol_error);
        }
    }
    if (!reader.empty()) {
        return encode_error(boost::system::errc::protocol_error);
    }
    return boost::system::error_code{};
}

std::size_t MessageDispatcher::send(std::shared_ptr<PeerSession> session,
                                     PeerMessageType type,
                                     const Bytes& payload,
                                     boost::system::error_code& ec) noexcept {
    if (!session || !session->is_authenticated()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
        return 0;
    }
    // Application-level size/type validation is enforced by the receiver in
    // handle_frame() at the trust boundary; the transport layer (send_message)
    // rejects any frame exceeding kMaxMessageBytes. The sender does not
    // pre-reject application payloads so that receiver-side rejection (and the
    // resulting session close) remains observable end-to-end.
    return session->send_message(static_cast<std::uint16_t>(type), payload, ec);
}

void MessageDispatcher::handle_frame(std::shared_ptr<PeerSession> session,
                                      PeerMessageType type,
                                      const Bytes& payload,
                                      boost::system::error_code& ec) noexcept {
    ec = boost::system::error_code{};
    if (!session || !session->is_authenticated()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::not_a_stream);
        return;
    }

    if (validate_message(type, payload) != 0) {
        ec = boost::system::errc::make_error_code(boost::system::errc::message_size);
        return;
    }

    auto it = handlers_.find(type);
    if (it == handlers_.end()) {
        ec = boost::system::errc::make_error_code(boost::system::errc::protocol_error);
        return;
    }

    it->second(session, type, payload, ec);
    if (!ec) {
        for (auto& observer : observers_) {
            observer(session, type, payload);
        }
    }
}

std::size_t MessageDispatcher::ping(std::shared_ptr<PeerSession> session,
                                     boost::system::error_code& ec) noexcept {
    auto now = std::chrono::steady_clock::now();
    ping_times_[reinterpret_cast<std::uint64_t>(session.get())] = now;
    Bytes payload(8, 0);
    auto ns = now.time_since_epoch().count();
    for (std::size_t i = 0; i < 8; ++i) {
        payload[i] = static_cast<Byte>((ns >> (56 - static_cast<int>(i) * 8)) & 0xff);
    }
    return send(session, PeerMessageType::kPing, payload, ec);
}

std::size_t MessageDispatcher::disconnect(std::shared_ptr<PeerSession> session,
                                           boost::system::error_code& ec) noexcept {
    return send(session, PeerMessageType::kDisconnect, {}, ec);
}

} // namespace feinstat::network
