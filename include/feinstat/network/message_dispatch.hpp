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

// Maximum number of peer records carried in a single peer-record message.
// Keeps request/response/announce payloads bounded and predictable.
inline constexpr std::size_t kMaxPeerRecordMessages = 32;

using MessageHandler = std::function<void(std::shared_ptr<PeerSession>,
                                           PeerMessageType,
                                           const Bytes&,
                                           boost::system::error_code&)>;

// Observers are notified of every successfully dispatched message after its
// handler runs. Unlike handlers, observers never replace a handler and cannot
// change the dispatch result; they exist so callers can tap the message stream
// (e.g. tests observing control-plane responses that are handled by default).
using MessageObserver = std::function<void(std::shared_ptr<PeerSession>,
                                           PeerMessageType,
                                           const Bytes&)>;

//------------------------------------------------------------------------------
// Peer-record wire encoding (control plane).
//
// All peer-record messages share a bounded, length-prefixed container:
//
//   u32 record_count
//   record_count × { u32 record_bytes; opaque serialized_record }
//
// A kPeerRecordRequest uses an alternate container carrying requested node IDs:
//
//   u32 requested_count
//   requested_count × string(node_id)   // requested_count == 0 means "all"
//
// Every container is validated for record count, per-record size, cumulative
// size, and trailing garbage before any record is parsed or persisted.
//------------------------------------------------------------------------------

// Encode a peer-record request listing the node IDs of interest. An empty list
// requests all records the peer is willing to share.
Bytes encode_peer_record_request(const std::vector<NodeId>& requested);

// Decode a peer-record request. Sets *requested and returns success, or a
// non-success error_code on truncation, oversized fields, or trailing garbage.
boost::system::error_code decode_peer_record_request(const Bytes& data,
                                                      std::vector<NodeId>& requested);

// Encode a peer-record response (or announce) carrying the given records.
Bytes encode_peer_record_response(const std::vector<SignedNodeRecord>& records);

// Decode a peer-record response (or announce). Sets *records and returns
// success, or a non-success error_code on any validation failure.
boost::system::error_code decode_peer_record_response(const Bytes& data,
                                                       std::vector<SignedNodeRecord>& records);

// Encode a single-record peer-record announce.
inline Bytes encode_peer_record_announce(const SignedNodeRecord& record) {
    return encode_peer_record_response({record});
}

// Decode a single-record peer-record announce.
inline boost::system::error_code decode_peer_record_announce(const Bytes& data,
                                                              SignedNodeRecord& record) {
    std::vector<SignedNodeRecord> records;
    auto ec = decode_peer_record_response(data, records);
    if (ec) return ec;
    if (records.size() != 1) {
        return boost::system::errc::make_error_code(boost::system::errc::protocol_error);
    }
    record = std::move(records[0]);
    return boost::system::error_code{};
}

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

    // Register an observer notified after a message's handler succeeds. An
    // observer never replaces the handler and cannot alter dispatch results.
    void add_observer(MessageObserver observer);

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
    std::vector<MessageObserver> observers_;
    std::map<std::uint64_t, std::chrono::steady_clock::time_point> ping_times_;
};

} // namespace feinstat::network
