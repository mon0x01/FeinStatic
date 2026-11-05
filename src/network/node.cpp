#include "feinstat/network/node.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <cstdio>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace feinstat::network {
namespace {

using boost::asio::ip::tcp;
using ErrorCode = boost::system::error_code;

constexpr std::array<Byte, 4> kMagic{'F', 'S', 'N', '1'};
constexpr std::uint16_t kWireVersion = 8;
constexpr std::size_t kHeaderBytes = 116;
constexpr std::size_t kMaximumBlockBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumErrorBytes = 4096;

using MessageType = std::uint16_t;
constexpr MessageType kHello = 1;
constexpr MessageType kHelloAck = 2;
constexpr MessageType kStatus = 3;
constexpr MessageType kStatusResult = 4;
constexpr MessageType kSubmitTransaction = 5;
constexpr MessageType kSubmitTransactionResult = 6;
constexpr MessageType kSubmitBlock = 7;
constexpr MessageType kSubmitBlockResult = 8;
constexpr MessageType kGetBlocks = 9;
constexpr MessageType kBlocksResult = 10;
constexpr MessageType kHelloChallenge = 11;
constexpr MessageType kClientAuth = 12;
constexpr MessageType kFindCommonAncestor = 13;
constexpr MessageType kCommonAncestorResult = 14;
constexpr MessageType kRaftMessage = 15;
constexpr MessageType kRaftMessageResult = 16;
constexpr MessageType kRaftProposeBlock = 17;
constexpr MessageType kRaftProposeBlockResult = 18;
constexpr MessageType kRaftStatus = 19;
constexpr MessageType kRaftStatusResult = 20;
constexpr MessageType kRaftChangeMembership = 21;
constexpr MessageType kRaftChangeMembershipResult = 22;
constexpr MessageType kRaftApplyTrustPolicy = 23;
constexpr MessageType kRaftApplyTrustPolicyResult = 24;
constexpr MessageType kRaftPeerHealth = 25;
constexpr MessageType kRaftPeerHealthResult = 26;
constexpr MessageType kRaftUpdatePeerEndpoint = 27;
constexpr MessageType kRaftUpdatePeerEndpointResult = 28;
constexpr MessageType kError = 255;

constexpr std::size_t kMaximumRaftOutboundMessages = 1024;
constexpr std::size_t kMaximumRaftOutboundBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumGossipOutboundMessages = 512;
constexpr std::size_t kMaximumGossipOutboundBytes = 32U * 1024U * 1024U;
constexpr std::array<Byte, 8> kEndpointRegistryMagic{'F', 'S', 'P', 'E', 'N', 'D', '1', '\n'};

bool valid_endpoint_host(const std::string& host) {
    if (host.empty() || host.size() > 253) return false;
    try {
        (void)boost::asio::ip::make_address(host);
        return true;
    } catch (...) {
    }
    if (host.front() == '.' || host.back() == '.') return false;
    std::size_t label_length = 0;
    bool label_starts_hyphen = false;
    for (std::size_t i = 0; i < host.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(host[i]);
        if (ch == '.') {
            if (label_length == 0 || label_length > 63 || label_starts_hyphen || host[i - 1] == '-') return false;
            label_length = 0;
            label_starts_hyphen = false;
            continue;
        }
        const bool alphanumeric = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                  (ch >= '0' && ch <= '9');
        if (!alphanumeric && ch != '-') return false;
        if (label_length == 0) label_starts_hyphen = ch == '-';
        ++label_length;
    }
    return label_length > 0 && label_length <= 63 && !label_starts_hyphen && host.back() != '-';
}

void write_endpoint_bytes(int fd, const Bytes& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "write peer endpoint registry");
        }
        if (written == 0) throw std::runtime_error("short write while storing peer endpoint registry");
        offset += static_cast<std::size_t>(written);
    }
}

Bytes encode_endpoint_registry(const std::string& cluster_id, std::uint64_t generation,
                               const std::map<std::string, RaftPeerEndpoint>& endpoints) {
    if (cluster_id.empty() || cluster_id.size() > 128 || endpoints.size() > consensus::kRaftMaximumMembers) {
        throw std::invalid_argument("peer endpoint registry cluster or peer count is invalid");
    }
    ByteWriter payload;
    payload.string(cluster_id);
    payload.u64(generation);
    payload.u32(static_cast<std::uint32_t>(endpoints.size()));
    for (const auto& [peer_id, endpoint] : endpoints) {
        if (peer_id != endpoint.peer_id || peer_id.empty() || peer_id.size() > 128 ||
            !valid_endpoint_host(endpoint.host) || endpoint.port == 0) {
            throw std::invalid_argument("peer endpoint registry contains an invalid endpoint");
        }
        payload.string(peer_id);
        payload.string(endpoint.host);
        payload.u32(endpoint.port);
        payload.u8(endpoint.enabled ? 1U : 0U);
    }
    const Bytes body = std::move(payload).take();
    ByteWriter envelope;
    envelope.fixed(kEndpointRegistryMagic);
    envelope.u8(1);
    envelope.blob(body);
    envelope.fixed(crypto::sha256(body));
    return std::move(envelope).take();
}

void save_endpoint_registry_atomic(const std::string& path, const std::string& cluster_id,
                                   std::uint64_t generation,
                                   const std::map<std::string, RaftPeerEndpoint>& endpoints) {
    if (path.empty()) throw std::invalid_argument("peer endpoint registry path is empty");
    const Bytes encoded = encode_endpoint_registry(cluster_id, generation, endpoints);
    const std::filesystem::path target(path);
    const auto parent = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) throw std::system_error(ec, "create peer endpoint registry directory");
    std::string temporary = path + ".tmp." + std::to_string(::getpid()) + "." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "create peer endpoint registry temporary file");
    bool open_fd = true;
    try {
        if (::fchmod(fd, 0600) != 0) throw std::system_error(errno, std::generic_category(), "chmod peer endpoint registry");
        write_endpoint_bytes(fd, encoded);
        if (::fsync(fd) != 0) throw std::system_error(errno, std::generic_category(), "fsync peer endpoint registry");
        if (::close(fd) != 0) { open_fd = false; throw std::system_error(errno, std::generic_category(), "close peer endpoint registry"); }
        open_fd = false;
        if (::rename(temporary.c_str(), path.c_str()) != 0) {
            throw std::system_error(errno, std::generic_category(), "replace peer endpoint registry");
        }
        const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd >= 0) { (void)::fsync(dir_fd); (void)::close(dir_fd); }
    } catch (...) {
        if (open_fd) (void)::close(fd);
        (void)::unlink(temporary.c_str());
        throw;
    }
}

std::map<std::string, RaftPeerEndpoint> load_endpoint_registry(
    const std::string& path, const std::string& expected_cluster_id,
    const std::map<std::string, RaftPeerEndpoint>& expected_roster,
    std::uint64_t& generation) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open peer endpoint registry");
    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0) {
        const int error = errno; (void)::close(fd);
        throw std::system_error(error, std::generic_category(), "stat peer endpoint registry");
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || (metadata.st_mode & 0077) != 0 ||
        metadata.st_size <= 0 || metadata.st_size > 16 * 1024) {
        (void)::close(fd);
        throw std::runtime_error("peer endpoint registry must be an owner-only regular file no larger than 16 KiB");
    }
    Bytes encoded(static_cast<std::size_t>(metadata.st_size));
    std::size_t offset = 0;
    while (offset < encoded.size()) {
        const ssize_t count = ::read(fd, encoded.data() + offset, encoded.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            const int error = errno; (void)::close(fd);
            throw std::system_error(error, std::generic_category(), "read peer endpoint registry");
        }
        if (count == 0) { (void)::close(fd); throw std::runtime_error("peer endpoint registry is truncated"); }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(fd) != 0) throw std::system_error(errno, std::generic_category(), "close peer endpoint registry");
    ByteReader envelope(encoded);
    if (envelope.fixed<8>() != kEndpointRegistryMagic || envelope.u8() != 1) {
        throw std::runtime_error("unsupported peer endpoint registry format");
    }
    const Bytes body = envelope.blob(12 * 1024);
    const Hash checksum = envelope.fixed<32>();
    if (!envelope.empty() || crypto::sha256(body) != checksum) {
        throw std::runtime_error("peer endpoint registry checksum or envelope validation failed");
    }
    ByteReader reader(body);
    const std::string cluster_id = reader.string(128);
    generation = reader.u64();
    const auto count = reader.u32();
    if (generation == 0) throw std::runtime_error("peer endpoint registry generation must be positive");
    if (cluster_id != expected_cluster_id) throw std::runtime_error("peer endpoint registry belongs to a different cluster");
    if (count != expected_roster.size() || count > consensus::kRaftMaximumMembers) {
        throw std::runtime_error("peer endpoint registry roster does not match the configured Raft peer roster");
    }
    std::map<std::string, RaftPeerEndpoint> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        RaftPeerEndpoint endpoint;
        endpoint.peer_id = reader.string(128);
        endpoint.host = reader.string(253);
        const auto raw_port = reader.u32();
        const auto enabled = reader.u8();
        if (raw_port == 0 || raw_port > UINT16_MAX) throw std::runtime_error("peer endpoint registry port is outside valid bounds");
        endpoint.port = static_cast<std::uint16_t>(raw_port);
        if (enabled > 1 || !valid_endpoint_host(endpoint.host) ||
            !expected_roster.contains(endpoint.peer_id)) {
            throw std::runtime_error("peer endpoint registry contains an invalid or unconfigured peer endpoint");
        }
        endpoint.enabled = enabled == 1;
        if (!result.emplace(endpoint.peer_id, endpoint).second) {
            throw std::runtime_error("peer endpoint registry contains duplicate peer IDs");
        }
    }
    if (!reader.empty() || result.size() != expected_roster.size()) {
        throw std::runtime_error("peer endpoint registry contains trailing data or missing peers");
    }
    return result;
}

std::uint64_t unix_time_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

struct Frame {
    MessageType type{0};
    std::uint64_t sequence{0};
    Bytes payload;
};

struct FrameSecurity {
    const NodeIdentity* local_identity{nullptr};
    PublicKey remote_public_key{};
    Hash session_binding{};
    std::uint64_t transmit_sequence{0};
    std::uint64_t receive_sequence{0};
    crypto::AeadKey transmit_key{};
    crypto::AeadKey receive_key{};
    crypto::AeadNoncePrefix transmit_nonce_prefix{};
    crypto::AeadNoncePrefix receive_nonce_prefix{};

    ~FrameSecurity() {
        OPENSSL_cleanse(transmit_key.data(), transmit_key.size());
        OPENSSL_cleanse(receive_key.data(), receive_key.size());
        OPENSSL_cleanse(transmit_nonce_prefix.data(), transmit_nonce_prefix.size());
        OPENSSL_cleanse(receive_nonce_prefix.data(), receive_nonce_prefix.size());
    }
};

struct EphemeralKeyWiper {
    crypto::X25519KeyPair& key_pair;
    ~EphemeralKeyWiper() { OPENSSL_cleanse(key_pair.secret_key.data(), key_pair.secret_key.size()); }
};

void append_u16(Bytes& output, std::uint16_t value) {
    output.push_back(static_cast<Byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<Byte>(value & 0xffU));
}

std::uint16_t read_u16(const Byte* input) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[0]) << 8U) |
                                      static_cast<std::uint16_t>(input[1]));
}

crypto::AeadNonce frame_nonce(const crypto::AeadNoncePrefix& prefix, std::uint64_t sequence) {
    crypto::AeadNonce nonce{};
    std::copy(prefix.begin(), prefix.end(), nonce.begin());
    for (int shift = 56, index = 4; shift >= 0; shift -= 8, ++index) {
        nonce[static_cast<std::size_t>(index)] = static_cast<Byte>((sequence >> shift) & 0xffU);
    }
    return nonce;
}

Bytes frame_associated_data(const Hash& binding, MessageType type, std::uint64_t sequence) {
    ByteWriter writer;
    writer.string("FeinStatic encrypted frame AAD v1");
    writer.fixed(binding);
    writer.u32(type);
    writer.u64(sequence);
    return std::move(writer).take();
}

Bytes encode_frame(MessageType type, const Bytes& payload, const FrameSecurity* security) {
    if (payload.size() > kMaximumFrameBytes) throw std::length_error("network payload exceeds maximum frame size");
    const std::uint64_t sequence = security == nullptr ? 0 : security->transmit_sequence;
    Bytes wire_payload = payload;
    Signature signature{};
    if (security != nullptr) {
        if (security->local_identity == nullptr) throw std::logic_error("authenticated frame has no local node identity");
        const auto nonce = frame_nonce(security->transmit_nonce_prefix, sequence);
        wire_payload = crypto::aes256gcm_encrypt(security->transmit_key, nonce,
            frame_associated_data(security->session_binding, type, sequence), payload);
        signature = security->local_identity->sign(detail::authenticated_frame_signing_message(
            security->session_binding, type, sequence, wire_payload));
    }
    if (wire_payload.size() > kMaximumFrameBytes + 16U) throw std::length_error("encrypted network payload exceeds frame limit");
    Bytes frame;
    frame.reserve(kHeaderBytes + wire_payload.size());
    frame.insert(frame.end(), kMagic.begin(), kMagic.end());
    append_u16(frame, kWireVersion);
    append_u16(frame, type);
    const auto length = static_cast<std::uint32_t>(wire_payload.size());
    frame.push_back(static_cast<Byte>((length >> 24U) & 0xffU));
    frame.push_back(static_cast<Byte>((length >> 16U) & 0xffU));
    frame.push_back(static_cast<Byte>((length >> 8U) & 0xffU));
    frame.push_back(static_cast<Byte>(length & 0xffU));
    for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(static_cast<Byte>((sequence >> shift) & 0xffULL));
    const Hash checksum = crypto::sha256(wire_payload);
    frame.insert(frame.end(), checksum.begin(), checksum.end());
    frame.insert(frame.end(), signature.begin(), signature.end());
    frame.insert(frame.end(), wire_payload.begin(), wire_payload.end());
    return frame;
}

template <typename Completion>
void timed_operation(tcp::socket& socket,
                     std::chrono::milliseconds timeout,
                     Completion&& initiate,
                     const char* operation_name) {
    auto& io = static_cast<boost::asio::io_context&>(socket.get_executor().context());
    io.restart();
    boost::asio::steady_timer timer(io);
    bool completed = false;
    bool timed_out = false;
    ErrorCode operation_error;
    timer.expires_after(timeout);
    timer.async_wait([&](const ErrorCode& error) {
        if (!error) {
            timed_out = true;
            ErrorCode ignored;
            socket.cancel(ignored);
        }
    });
    initiate([&](const ErrorCode& error, std::size_t) {
        operation_error = error;
        completed = true;
        timer.cancel();
    });
    io.run();
    if (timed_out) {
        throw std::runtime_error(std::string("network ") + operation_name + " timed out");
    }
    if (!completed) throw std::runtime_error(std::string("network ") + operation_name + " did not complete");
    if (operation_error) {
        throw boost::system::system_error(operation_error, std::string("network ") + operation_name);
    }
}

tcp::resolver::results_type timed_resolve(tcp::resolver& resolver,
                                                   const std::string& host,
                                                   const std::string& service,
                                                   std::chrono::milliseconds timeout) {
    auto& io = static_cast<boost::asio::io_context&>(resolver.get_executor().context());
    io.restart();
    boost::asio::steady_timer timer(io);
    bool completed = false;
    bool timed_out = false;
    ErrorCode operation_error;
    tcp::resolver::results_type results;
    timer.expires_after(timeout);
    timer.async_wait([&](const ErrorCode& error) {
        if (!error) {
            timed_out = true;
            resolver.cancel();
        }
    });
    resolver.async_resolve(host, service,
        [&](const ErrorCode& error, const tcp::resolver::results_type& resolved) {
            operation_error = error;
            results = resolved;
            completed = true;
            timer.cancel();
        });
    io.run();
    if (timed_out) throw std::runtime_error("network name resolution timed out");
    if (!completed) throw std::runtime_error("network name resolution did not complete");
    if (operation_error) throw boost::system::system_error(operation_error, "network name resolution");
    return results;
}

void timed_connect(tcp::socket& socket,
                   const tcp::resolver::results_type& endpoints,
                   std::chrono::milliseconds timeout) {
    timed_operation(socket, timeout,
        [&](auto handler) {
            boost::asio::async_connect(socket, endpoints,
                [completion = std::move(handler)](const ErrorCode& error, const tcp::endpoint&) mutable {
                    completion(error, 0);
                });
        }, "connect");
}

void timed_read(tcp::socket& socket, void* data, std::size_t size,
                std::chrono::milliseconds timeout) {
    timed_operation(socket, timeout,
        [&](auto handler) { boost::asio::async_read(socket, boost::asio::buffer(data, size), std::move(handler)); },
        "read");
}

void timed_write(tcp::socket& socket, const Bytes& data,
                 std::chrono::milliseconds timeout) {
    timed_operation(socket, timeout,
        [&](auto handler) { boost::asio::async_write(socket, boost::asio::buffer(data), std::move(handler)); },
        "write");
}

void write_frame(tcp::socket& socket, MessageType type, const Bytes& payload,
                 std::chrono::milliseconds timeout, FrameSecurity* security = nullptr) {
    const Bytes encoded = encode_frame(type, payload, security);
    timed_write(socket, encoded, timeout);
    if (security != nullptr) {
        if (security->transmit_sequence == UINT64_MAX) throw std::overflow_error("peer transmit sequence exhausted");
        ++security->transmit_sequence;
    }
}

Frame read_frame(tcp::socket& socket, std::chrono::milliseconds timeout,
                 FrameSecurity* security = nullptr) {
    std::array<Byte, kHeaderBytes> header{};
    timed_read(socket, header.data(), header.size(), timeout);
    if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
        throw std::runtime_error("network frame magic mismatch");
    }
    if (read_u16(header.data() + 4) != kWireVersion) {
        throw std::runtime_error("unsupported network wire version");
    }
    const MessageType type = read_u16(header.data() + 6);
    const std::uint32_t length = (static_cast<std::uint32_t>(header[8]) << 24U) |
                                 (static_cast<std::uint32_t>(header[9]) << 16U) |
                                 (static_cast<std::uint32_t>(header[10]) << 8U) |
                                  static_cast<std::uint32_t>(header[11]);
    if (length > kMaximumFrameBytes + 16U) throw std::length_error("network frame exceeds maximum size");
    std::uint64_t sequence = 0;
    for (std::size_t index = 12; index < 20; ++index) {
        sequence = (sequence << 8U) | header[index];
    }
    Hash expected_checksum{};
    std::copy(header.begin() + 20, header.begin() + 52, expected_checksum.begin());
    Signature signature{};
    std::copy(header.begin() + 52, header.end(), signature.begin());
    Bytes payload(length);
    if (!payload.empty()) timed_read(socket, payload.data(), payload.size(), timeout);
    if (crypto::sha256(payload) != expected_checksum) {
        throw std::runtime_error("network frame checksum mismatch");
    }
    if (security == nullptr) {
        if (sequence != 0 || std::any_of(signature.begin(), signature.end(), [](Byte value) { return value != 0; })) {
            throw std::runtime_error("unexpected authenticated frame without an authenticated session");
        }
    } else {
        if (sequence != security->receive_sequence) {
            throw std::runtime_error("peer frame sequence is replayed or out of order");
        }
        if (!crypto::verify_ed25519(security->remote_public_key,
                                    detail::authenticated_frame_signing_message(security->session_binding, type, sequence, payload), signature)) {
            throw std::runtime_error("peer frame authentication signature is invalid");
        }
        if (security->receive_sequence == UINT64_MAX) throw std::overflow_error("peer receive sequence exhausted");
        const auto nonce = frame_nonce(security->receive_nonce_prefix, sequence);
        payload = crypto::aes256gcm_decrypt(security->receive_key, nonce,
            frame_associated_data(security->session_binding, type, sequence), payload);
        if (payload.size() > kMaximumFrameBytes) throw std::length_error("decrypted peer payload exceeds maximum frame size");
        ++security->receive_sequence;
    }
    return {type, sequence, std::move(payload)};
}

void require_empty(const ByteReader& reader, const char* message) {
    if (!reader.empty()) throw std::runtime_error(message);
}

Bytes status_payload(const ledger::Ledger& ledger) {
    ByteWriter writer;
    const auto& chain = ledger.chain();
    writer.u64(chain.height());
    writer.fixed(chain.blocks().back().hash);
    if (ledger.mempool().transactions().size() > UINT32_MAX) {
        throw std::length_error("mempool exceeds protocol count limit");
    }
    writer.u32(static_cast<std::uint32_t>(ledger.mempool().transactions().size()));
    return std::move(writer).take();
}

PeerStatus parse_status(const Bytes& payload) {
    ByteReader reader(payload);
    PeerStatus status;
    status.height = reader.u64();
    status.tip_hash = reader.fixed<32>();
    status.pending_transactions = reader.u32();
    require_empty(reader, "trailing bytes in peer status response");
    return status;
}

Bytes hello_core_payload(const PeerChainParameters& parameters) {
    ByteWriter writer;
    writer.u32(kProtocolVersion);
    writer.fixed(parameters.genesis_hash);
    writer.u32(parameters.difficulty);
    writer.u64(parameters.block_reward);
    return std::move(writer).take();
}

PeerChainParameters peer_chain_parameters(const ledger::Ledger& ledger) {
    return PeerChainParameters{
        ledger.chain().blocks().front().hash,
        ledger.chain().difficulty(),
        ledger.chain().block_reward()};
}

Bytes client_auth_message(const Bytes& core, const std::string& client_id,
                          const std::string& server_id, const Hash& client_nonce,
                          const Hash& server_nonce, const PublicKey& client_ephemeral,
                          const PublicKey& server_ephemeral) {
    ByteWriter writer;
    writer.string("FeinStatic authenticated peer client proof v2");
    writer.blob(core);
    writer.string(client_id);
    writer.string(server_id);
    writer.fixed(client_nonce);
    writer.fixed(server_nonce);
    writer.fixed(client_ephemeral);
    writer.fixed(server_ephemeral);
    return std::move(writer).take();
}

Bytes server_challenge_message(const Bytes& core, const std::string& client_id,
                               const std::string& server_id, const Hash& client_nonce,
                               const Hash& server_nonce, const PublicKey& client_ephemeral,
                               const PublicKey& server_ephemeral) {
    return detail::authenticated_peer_session_transcript(core, client_id, server_id,
        client_nonce, server_nonce, client_ephemeral, server_ephemeral);
}

Bytes server_auth_message(const Bytes& core, const std::string& client_id,
                          const std::string& server_id, const Hash& client_nonce,
                          const Hash& server_nonce, const PublicKey& client_ephemeral,
                          const PublicKey& server_ephemeral, const Bytes& status) {
    ByteWriter writer;
    writer.string("FeinStatic authenticated peer server acknowledgement v2");
    writer.blob(core);
    writer.string(client_id);
    writer.string(server_id);
    writer.fixed(client_nonce);
    writer.fixed(server_nonce);
    writer.fixed(client_ephemeral);
    writer.fixed(server_ephemeral);
    writer.blob(status);
    return std::move(writer).take();
}

FrameSecurity make_frame_security(const NodeIdentity* local_identity,
                                  const PublicKey& remote_public_key,
                                  const SecretKey& local_ephemeral_secret,
                                  const PublicKey& remote_ephemeral_public,
                                  const Bytes& core, const std::string& client_id,
                                  const std::string& server_id, const Hash& client_nonce,
                                  const Hash& server_nonce, const PublicKey& client_ephemeral,
                                  const PublicKey& server_ephemeral, bool client_side) {
    SecretKey shared = crypto::derive_x25519_shared_secret(local_ephemeral_secret, remote_ephemeral_public);
    struct Wiper { SecretKey& value; ~Wiper() { OPENSSL_cleanse(value.data(), value.size()); } } wiper{shared};
    const Bytes transcript = detail::authenticated_peer_session_transcript(core, client_id,
        server_id, client_nonce, server_nonce, client_ephemeral, server_ephemeral);
    auto keys = crypto::derive_peer_session_keys(shared, transcript);
    FrameSecurity result;
    result.local_identity = local_identity;
    result.remote_public_key = remote_public_key;
    result.session_binding = crypto::sha256(transcript);
    if (client_side) {
        result.transmit_key = keys.client_to_server;
        result.receive_key = keys.server_to_client;
        result.transmit_nonce_prefix = keys.client_nonce_prefix;
        result.receive_nonce_prefix = keys.server_nonce_prefix;
    } else {
        result.transmit_key = keys.server_to_client;
        result.receive_key = keys.client_to_server;
        result.transmit_nonce_prefix = keys.server_nonce_prefix;
        result.receive_nonce_prefix = keys.client_nonce_prefix;
    }
    return result;
}

Hash secure_nonce() {
    Hash nonce{};
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        throw std::runtime_error("OpenSSL failed to generate peer handshake nonce");
    }
    return nonce;
}

struct AuthenticatedClientHello {
    std::string peer_id;
    PublicKey public_key{};
    Hash nonce{};
    PublicKey ephemeral_public_key{};
    Bytes core;
};

AuthenticatedClientHello validate_hello(const Bytes& payload, const ledger::Ledger& ledger,
                                        const PeerAuthConfig* authentication) {
    ByteReader reader(payload);
    const auto version = reader.u32();
    const Hash genesis = reader.fixed<32>();
    const auto difficulty = reader.u32();
    const Amount reward = reader.u64();
    if (version != kProtocolVersion) throw std::runtime_error("peer protocol version mismatch");
    if (genesis != ledger.chain().blocks().front().hash) {
        throw std::runtime_error("peer belongs to a different genesis chain");
    }
    if (difficulty != ledger.chain().difficulty() || reward != ledger.chain().block_reward()) {
        throw std::runtime_error("peer consensus parameters do not match local chain");
    }

    ByteWriter core_writer;
    core_writer.u32(version);
    core_writer.fixed(genesis);
    core_writer.u32(difficulty);
    core_writer.u64(reward);
    AuthenticatedClientHello result;
    result.core = std::move(core_writer).take();

    if (authentication == nullptr) {
        require_empty(reader, "unexpected authentication data from an unauthenticated peer");
        return result;
    }
    if (!authentication->identity) throw std::invalid_argument("authenticated node has no local identity");

    result.peer_id = reader.string(128);
    result.public_key = reader.fixed<32>();
    result.nonce = reader.fixed<32>();
    result.ephemeral_public_key = reader.fixed<32>();
    require_empty(reader, "trailing bytes in authenticated peer hello");
    if (peer_id_from_public_key(result.public_key) != result.peer_id) {
        throw std::runtime_error("peer identity does not match its public key");
    }
    const PublicKey* trusted = authentication->trusted_peers.find(result.peer_id);
    if (trusted == nullptr || *trusted != result.public_key) {
        throw std::runtime_error("peer identity is not in the node trust store");
    }
    return result;
}

struct AuthenticatedServerChallenge {
    std::string peer_id;
    PublicKey public_key{};
    Hash nonce{};
    PublicKey ephemeral_public_key{};
};

Bytes server_challenge_payload(const PeerAuthConfig& authentication,
                               const AuthenticatedClientHello& client,
                               const Hash& server_nonce,
                               const PublicKey& server_ephemeral_public) {
    if (!authentication.identity) throw std::invalid_argument("authenticated node has no local identity");
    const std::string server_id = authentication.identity->peer_id();
    const PublicKey& server_public_key = authentication.identity->public_key();
    const Signature signature = authentication.identity->sign(server_challenge_message(
        client.core, client.peer_id, server_id, client.nonce, server_nonce,
        client.ephemeral_public_key, server_ephemeral_public));
    ByteWriter writer;
    writer.string(server_id);
    writer.fixed(server_public_key);
    writer.fixed(server_nonce);
    writer.fixed(server_ephemeral_public);
    writer.fixed(signature);
    return std::move(writer).take();
}

AuthenticatedServerChallenge validate_server_challenge(
    const Bytes& payload, const PeerAuthConfig& authentication,
    const AuthenticatedClientHello& client) {
    ByteReader reader(payload);
    AuthenticatedServerChallenge result;
    result.peer_id = reader.string(128);
    result.public_key = reader.fixed<32>();
    result.nonce = reader.fixed<32>();
    result.ephemeral_public_key = reader.fixed<32>();
    const Signature signature = reader.fixed<64>();
    require_empty(reader, "trailing bytes in authenticated peer challenge");
    if (result.peer_id != authentication.expected_peer_id) {
        throw std::runtime_error("connected peer identity does not match the expected peer ID");
    }
    if (peer_id_from_public_key(result.public_key) != result.peer_id) {
        throw std::runtime_error("peer server identity does not match its public key");
    }
    const PublicKey* trusted = authentication.trusted_peers.find(result.peer_id);
    if (trusted == nullptr || *trusted != result.public_key) {
        throw std::runtime_error("peer server identity is not in the local trust store");
    }
    if (!crypto::verify_ed25519(result.public_key,
            server_challenge_message(client.core, client.peer_id, result.peer_id,
                                     client.nonce, result.nonce, client.ephemeral_public_key,
                                     result.ephemeral_public_key), signature)) {
        throw std::runtime_error("peer server challenge signature is invalid");
    }
    return result;
}

Bytes hello_ack_payload(const ledger::Ledger& ledger, const PeerAuthConfig& authentication,
                        const AuthenticatedClientHello& client,
                        const AuthenticatedServerChallenge& server) {
    const Bytes status = status_payload(ledger);
    const Signature signature = authentication.identity->sign(server_auth_message(
        client.core, client.peer_id, server.peer_id, client.nonce, server.nonce,
        client.ephemeral_public_key, server.ephemeral_public_key, status));
    ByteWriter writer;
    writer.blob(status);
    writer.fixed(signature);
    return std::move(writer).take();
}

PeerStatus validate_hello_ack(const Bytes& payload, const PeerAuthConfig* authentication,
                              const AuthenticatedClientHello* client,
                              const AuthenticatedServerChallenge* server) {
    if (authentication == nullptr) return parse_status(payload);
    ByteReader reader(payload);
    const Bytes status = reader.blob(1024);
    const Signature signature = reader.fixed<64>();
    require_empty(reader, "trailing bytes in authenticated peer response");
    if (!crypto::verify_ed25519(server->public_key,
            server_auth_message(client->core, client->peer_id, server->peer_id,
                                client->nonce, server->nonce, client->ephemeral_public_key,
                                server->ephemeral_public_key, status), signature)) {
        throw std::runtime_error("peer server acknowledgement signature is invalid");
    }
    return parse_status(status);
}

void send_error(tcp::socket& socket, const std::string& message,
                std::chrono::milliseconds timeout, FrameSecurity* security = nullptr) noexcept {
    try {
        const std::string bounded = message.substr(0, kMaximumErrorBytes);
        ByteWriter writer;
        writer.string(bounded);
        write_frame(socket, kError, writer.data(), timeout, security);
    } catch (...) {
        // The original protocol/validation error is more useful than a
        // secondary failure while trying to report it to a disconnected peer.
    }
}

std::string decode_error(const Bytes& payload) {
    ByteReader reader(payload);
    std::string message = reader.string(kMaximumErrorBytes);
    require_empty(reader, "trailing bytes in peer error response");
    return message;
}

void expect_type(const Frame& frame, MessageType expected) {
    if (frame.type == kError) throw std::runtime_error("peer rejected request: " + decode_error(frame.payload));
    if (frame.type != expected) throw std::runtime_error("unexpected peer response message type");
}

class ClientConnection {
public:
    ClientConnection(const std::string& host, std::uint16_t port,
                     const ledger::Ledger& ledger, std::chrono::milliseconds timeout,
                     const PeerAuthConfig* authentication)
        : ClientConnection(host, port, peer_chain_parameters(ledger), timeout, authentication) {}

    ClientConnection(const std::string& host, std::uint16_t port,
                     const PeerChainParameters& parameters, std::chrono::milliseconds timeout,
                     const PeerAuthConfig* authentication)
        : io_(), socket_(io_), timeout_(timeout) {
        tcp::resolver resolver(io_);
        const auto endpoints = timed_resolve(resolver, host, std::to_string(port), timeout_);
        timed_connect(socket_, endpoints, timeout_);
        socket_.set_option(tcp::no_delay(true));
        const Bytes core = hello_core_payload(parameters);
        AuthenticatedClientHello client;
        client.core = core;
        AuthenticatedServerChallenge server;
        crypto::X25519KeyPair ephemeral{};
        EphemeralKeyWiper ephemeral_wiper{ephemeral};
        if (authentication != nullptr) {
            if (!authentication->identity) throw std::invalid_argument("authenticated peer client has no node identity");
            client.peer_id = authentication->identity->peer_id();
            client.public_key = authentication->identity->public_key();
            client.nonce = secure_nonce();
            ephemeral = crypto::generate_x25519_keypair();
            client.ephemeral_public_key = ephemeral.public_key;
            ByteWriter hello;
            hello.raw(core.data(), core.size());
            hello.string(client.peer_id);
            hello.fixed(client.public_key);
            hello.fixed(client.nonce);
            hello.fixed(client.ephemeral_public_key);
            write_frame(socket_, kHello, hello.data(), timeout_);

            const Frame challenge_frame = read_frame(socket_, timeout_);
            expect_type(challenge_frame, kHelloChallenge);
            server = validate_server_challenge(challenge_frame.payload, *authentication, client);
            const Signature proof = authentication->identity->sign(client_auth_message(
                client.core, client.peer_id, server.peer_id, client.nonce, server.nonce,
                client.ephemeral_public_key, server.ephemeral_public_key));
            ByteWriter proof_payload;
            proof_payload.fixed(proof);
            write_frame(socket_, kClientAuth, proof_payload.data(), timeout_);
        } else {
            write_frame(socket_, kHello, core, timeout_);
        }
        const Frame response = read_frame(socket_, timeout_);
        expect_type(response, kHelloAck);
        status_ = validate_hello_ack(response.payload, authentication,
                                     authentication ? &client : nullptr,
                                     authentication ? &server : nullptr);
        if (authentication != nullptr) {
            security_.emplace(make_frame_security(authentication->identity.get(), server.public_key,
                ephemeral.secret_key, server.ephemeral_public_key, client.core, client.peer_id,
                server.peer_id, client.nonce, server.nonce, client.ephemeral_public_key,
                server.ephemeral_public_key, true));
            OPENSSL_cleanse(ephemeral.secret_key.data(), ephemeral.secret_key.size());
        }
    }

    [[nodiscard]] PeerStatus status() const noexcept { return status_; }
    [[nodiscard]] Frame request(MessageType type, const Bytes& payload) {
        write_frame(socket_, type, payload, timeout_, security_ ? &*security_ : nullptr);
        return read_frame(socket_, timeout_, security_ ? &*security_ : nullptr);
    }

private:
    boost::asio::io_context io_;
    tcp::socket socket_;
    std::chrono::milliseconds timeout_;
    PeerStatus status_{};
    std::optional<FrameSecurity> security_;
};

struct RaftServerContext {
    consensus::RaftLedgerAdapter* adapter{nullptr};
    const std::set<std::string>* members{nullptr};
    std::string local_peer_id;
    std::function<void(std::vector<consensus::RaftMessage>)> enqueue;
    std::function<void(const ledger::Transaction&, const std::string&)> enqueue_transaction;
    std::function<TrustPolicyApplyResult(const std::string&, const Bytes&)> apply_trust_policy;
    std::function<RaftPeerHealthReport()> peer_health;
    std::function<RaftPeerEndpointUpdateResult(const std::string&, const std::string&, std::uint16_t, bool)> update_endpoint;
};

void serve_connection(tcp::socket& socket, ledger::Ledger& ledger,
                      std::chrono::milliseconds timeout, const PeerAuthConfig* authentication,
                      RaftServerContext* raft_context, std::mutex& state_mutex) {
    std::optional<FrameSecurity> session_security;
    try {
        socket.set_option(tcp::no_delay(true));
        const Frame hello = read_frame(socket, timeout);
        if (hello.type != kHello) throw std::runtime_error("first peer message must be a handshake");
        AuthenticatedClientHello client;
        {
            // Ledger and trust state have one writer. Handshake I/O and crypto
            // can overlap across workers, but identity authorization and the
            // chain-parameter read are kept inside the state boundary.
            std::lock_guard state_lock(state_mutex);
            client = validate_hello(hello.payload, ledger, authentication);
        }
        if (authentication == nullptr) {
            Bytes status;
            {
                std::lock_guard state_lock(state_mutex);
                status = status_payload(ledger);
            }
            write_frame(socket, kHelloAck, status, timeout);
        } else {
            auto ephemeral = crypto::generate_x25519_keypair();
            EphemeralKeyWiper ephemeral_wiper{ephemeral};
            const Hash server_nonce = secure_nonce();
            const std::string server_id = authentication->identity->peer_id();
            const Bytes challenge = server_challenge_payload(*authentication, client, server_nonce,
                                                              ephemeral.public_key);
            write_frame(socket, kHelloChallenge, challenge, timeout);
            const Frame proof = read_frame(socket, timeout);
            if (proof.type != kClientAuth) throw std::runtime_error("authenticated peer did not answer the server challenge");
            ByteReader proof_reader(proof.payload);
            const Signature signature = proof_reader.fixed<64>();
            require_empty(proof_reader, "trailing bytes in client authentication proof");
            if (!crypto::verify_ed25519(client.public_key,
                    client_auth_message(client.core, client.peer_id, server_id,
                                        client.nonce, server_nonce, client.ephemeral_public_key,
                                        ephemeral.public_key), signature)) {
                OPENSSL_cleanse(ephemeral.secret_key.data(), ephemeral.secret_key.size());
                throw std::runtime_error("peer client challenge response is invalid");
            }
            AuthenticatedServerChallenge server{server_id, authentication->identity->public_key(),
                                                  server_nonce, ephemeral.public_key};
            Bytes acknowledgement;
            {
                std::lock_guard state_lock(state_mutex);
                acknowledgement = hello_ack_payload(ledger, *authentication, client, server);
            }
            write_frame(socket, kHelloAck, acknowledgement, timeout);
            session_security.emplace(make_frame_security(authentication->identity.get(), client.public_key,
                ephemeral.secret_key, client.ephemeral_public_key, client.core, client.peer_id,
                server.peer_id, client.nonce, server_nonce, client.ephemeral_public_key,
                ephemeral.public_key, false));
            OPENSSL_cleanse(ephemeral.secret_key.data(), ephemeral.secret_key.size());
        }
        const Frame request = read_frame(socket, timeout, session_security ? &*session_security : nullptr);
        // All protocol operations that inspect or mutate Ledger, Raft,
        // endpoint, or trust state run on this single-writer critical section.
        // Network acceptance, handshake reads, and cryptographic challenge
        // work remain concurrent and independently time-bounded.
        std::unique_lock state_lock(state_mutex);
        if (request.type == kStatus) {
            require_empty(ByteReader(request.payload), "status request must be empty");
            const Bytes response = status_payload(ledger);
            state_lock.unlock();
            write_frame(socket, kStatusResult, response, timeout, session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kSubmitTransaction) {
            ByteReader reader(request.payload);
            const Bytes encoded = reader.blob(1U * 1024U * 1024U);
            require_empty(reader, "trailing bytes in transaction submission");
            const auto transaction = ledger::Transaction::deserialize(encoded);
            std::string error;
            const bool accepted = ledger.submit(transaction, &error);
            if (accepted && raft_context != nullptr && raft_context->enqueue_transaction) {
                raft_context->enqueue_transaction(transaction, client.peer_id);
            }
            ByteWriter response;
            response.u8(accepted ? 1 : 0);
            response.string(accepted ? std::string{} : error.substr(0, kMaximumErrorBytes));
            state_lock.unlock();
            write_frame(socket, kSubmitTransactionResult, response.data(), timeout, session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kSubmitBlock) {
            if (raft_context != nullptr) {
                throw std::runtime_error("Raft-enabled node refuses direct block submission; Ledger blocks must be committed through Raft");
            }
            ByteReader reader(request.payload);
            const Bytes encoded = reader.blob(kMaximumBlockBytes);
            require_empty(reader, "trailing bytes in block submission");
            const auto block = ledger::Block::deserialize(encoded);
            ledger.accept_blocks({block});
            ByteWriter response;
            response.u8(1);
            state_lock.unlock();
            write_frame(socket, kSubmitBlockResult, response.data(), timeout, session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kFindCommonAncestor) {
            if (raft_context != nullptr) {
                throw std::runtime_error("chain synchronization is disabled on a Raft member; use consensus replication");
            }
            ByteReader reader(request.payload);
            const auto count = reader.u32();
            if (count == 0 || count > 64) throw std::invalid_argument("chain locator count is outside protocol bounds");
            const auto& blocks = ledger.chain().blocks();
            std::uint64_t previous_height = UINT64_MAX;
            std::optional<std::pair<std::uint64_t, Hash>> common;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto height = reader.u64();
                const Hash hash = reader.fixed<32>();
                if (height >= previous_height) {
                    throw std::invalid_argument("chain locator heights must be strictly descending");
                }
                previous_height = height;
                if (!common.has_value() && height < blocks.size() &&
                    blocks[static_cast<std::size_t>(height)].hash == hash) {
                    common = std::make_pair(height, hash);
                }
            }
            require_empty(reader, "trailing bytes in common-ancestor request");
            if (!common.has_value()) {
                throw std::runtime_error("peer chain locator has no common block; genesis handshake should have prevented this");
            }
            ByteWriter response;
            response.u64(common->first);
            response.fixed(common->second);
            state_lock.unlock();
            write_frame(socket, kCommonAncestorResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kGetBlocks) {
            if (raft_context != nullptr) {
                throw std::runtime_error("block-range synchronization is disabled on a Raft member; use consensus replication");
            }
            ByteReader reader(request.payload);
            const auto start_height = reader.u64();
            const Hash expected_previous = reader.fixed<32>();
            const auto maximum_count = reader.u32();
            require_empty(reader, "trailing bytes in block-range request");
            if (start_height == 0 || maximum_count == 0 || maximum_count > kMaximumBlocksPerBatch) {
                throw std::invalid_argument("invalid block-range request bounds");
            }
            const auto& blocks = ledger.chain().blocks();
            const auto local_height = ledger.chain().height();
            if (start_height > local_height + 1) {
                throw std::invalid_argument("requested block range starts beyond peer tip");
            }
            const auto previous_index = static_cast<std::size_t>(start_height - 1);
            if (blocks[previous_index].hash != expected_previous) {
                throw std::runtime_error("requested block range does not extend the supplied previous hash");
            }
            std::vector<Bytes> encoded_blocks;
            const std::uint64_t available = local_height >= start_height
                ? local_height - start_height + 1 : 0;
            const auto candidate_count = static_cast<std::uint32_t>(std::min<std::uint64_t>(available, maximum_count));
            std::size_t response_size = sizeof(std::uint32_t);
            for (std::uint32_t i = 0; i < candidate_count; ++i) {
                const auto index = static_cast<std::size_t>(start_height + i);
                Bytes encoded = blocks[index].serialize();
                if (encoded.size() > kMaximumBlockBytes) {
                    throw std::length_error("peer block exceeds maximum serialized block size");
                }
                if (response_size > kMaximumFrameBytes - sizeof(std::uint32_t) ||
                    encoded.size() > kMaximumFrameBytes - response_size - sizeof(std::uint32_t)) break;
                response_size += sizeof(std::uint32_t) + encoded.size();
                encoded_blocks.push_back(std::move(encoded));
            }
            if (candidate_count != 0 && encoded_blocks.empty()) {
                throw std::length_error("single peer block exceeds maximum response frame size");
            }
            ByteWriter response;
            response.u32(static_cast<std::uint32_t>(encoded_blocks.size()));
            for (const auto& encoded : encoded_blocks) response.blob(encoded);
            state_lock.unlock();
            write_frame(socket, kBlocksResult, response.data(), timeout, session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftMessage) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft RPC requires an authenticated Raft-enabled node");
            }
            if (client.peer_id.empty() || raft_context->members == nullptr ||
                !raft_context->members->contains(client.peer_id)) {
                throw std::runtime_error("authenticated peer is not a member of this Raft cluster");
            }
            ByteReader reader(request.payload);
            const Bytes encoded = reader.blob(15U * 1024U * 1024U);
            require_empty(reader, "trailing bytes in Raft RPC");
            const auto message = consensus::RaftMessage::deserialize(encoded);
            if (message.from != client.peer_id || message.to != raft_context->local_peer_id) {
                throw std::runtime_error("Raft RPC identity does not match the authenticated connection or local node");
            }
            raft_context->enqueue(raft_context->adapter->receive(message, client.peer_id));
            state_lock.unlock();
            write_frame(socket, kRaftMessageResult, {}, timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftProposeBlock) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft proposal requires an authenticated Raft-enabled node");
            }
            // Block proposals are client commands: any explicitly trusted
            // authenticated identity may submit one to the current leader.
            // Only Raft protocol messages themselves are restricted to members.
            if (client.peer_id.empty()) {
                throw std::runtime_error("Raft proposal requires an authenticated client identity");
            }
            ByteReader reader(request.payload);
            const Address miner = reader.string(128);
            require_empty(reader, "trailing bytes in Raft block proposal");
            if (!crypto::is_valid_address(miner)) throw std::invalid_argument("Raft block proposal miner address is invalid");
            const auto proposal = raft_context->adapter->propose_next_block(miner);
            raft_context->enqueue(proposal.outbound);
            ByteWriter response;
            response.u64(proposal.index);
            response.u64(proposal.term);
            response.fixed(proposal.block_hash);
            state_lock.unlock();
            write_frame(socket, kRaftProposeBlockResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftChangeMembership) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft membership change requires an authenticated Raft-enabled node");
            }
            // Membership administration is restricted to explicitly assigned
            // operator identities; ordinary replication/observer peers cannot
            // change the cluster voter set.
            if (client.peer_id.empty() || !authentication->trusted_peers.is_operator(client.peer_id)) {
                throw std::runtime_error("Raft membership change denied: authenticated identity lacks operator role");
            }
            ByteReader reader(request.payload);
            const auto count = reader.u32();
            if (count < 3 || count > consensus::kRaftMaximumMembers || count % 2U == 0U) {
                throw std::invalid_argument("new Raft voter set must contain 3, 5, 7, or 9 members");
            }
            std::vector<std::string> new_members;
            new_members.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) new_members.push_back(reader.string(128));
            require_empty(reader, "trailing bytes in Raft membership change request");
            auto outbound = raft_context->adapter->propose_membership_change(new_members);
            raft_context->enqueue(outbound);
            const auto& state = raft_context->adapter->persistent_state();
            ByteWriter response;
            response.u64(state.commit_index);
            response.u64(state.log.empty() ? state.snapshot_index : state.log.back().index);
            response.u64(state.current_term);
            state_lock.unlock();
            write_frame(socket, kRaftChangeMembershipResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftPeerHealth) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft peer health requires an authenticated Raft-enabled node");
            }
            if (client.peer_id.empty() || !authentication->trusted_peers.is_operator(client.peer_id)) {
                throw std::runtime_error("Raft peer health denied: authenticated identity lacks operator role");
            }
            if (!raft_context->peer_health) throw std::runtime_error("Raft peer health is unavailable");
            require_empty(ByteReader(request.payload), "Raft peer health request must be empty");
            const auto report = raft_context->peer_health();
            ByteWriter response;
            response.u64(report.endpoint_registry_generation);
            response.u64(report.outbound_dropped_total);
            response.u32(static_cast<std::uint32_t>(report.worker_count));
            response.u32(static_cast<std::uint32_t>(report.peers.size()));
            for (const auto& peer : report.peers) {
                response.string(peer.peer_id);
                response.string(peer.host);
                response.u32(peer.port);
                response.u8(peer.enabled ? 1U : 0U);
                response.u8(peer.in_flight ? 1U : 0U);
                response.u32(static_cast<std::uint32_t>(peer.queued_messages));
                response.u64(peer.attempts);
                response.u64(peer.successes);
                response.u64(peer.failures);
                response.u64(peer.dropped);
                response.u32(peer.consecutive_failures);
                response.u64(peer.last_success_unix_ms);
                response.u64(peer.last_failure_unix_ms);
                response.u64(peer.retry_delay_ms);
                response.string(peer.last_error.substr(0, 256));
            }
            state_lock.unlock();
            write_frame(socket, kRaftPeerHealthResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftUpdatePeerEndpoint) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft endpoint management requires an authenticated Raft-enabled node");
            }
            if (client.peer_id.empty() || !authentication->trusted_peers.is_operator(client.peer_id)) {
                throw std::runtime_error("Raft endpoint management denied: authenticated identity lacks operator role");
            }
            if (!raft_context->update_endpoint) throw std::runtime_error("Raft endpoint management is unavailable");
            ByteReader reader(request.payload);
            const std::string target_id = reader.string(128);
            const std::string host = reader.string(253);
            const auto raw_port = reader.u32();
            const Byte enabled = reader.u8();
            if (raw_port == 0 || raw_port > UINT16_MAX) throw std::invalid_argument("Raft endpoint update port is outside valid bounds");
            const auto port = static_cast<std::uint16_t>(raw_port);
            require_empty(reader, "trailing bytes in Raft endpoint update request");
            if (enabled > 1) throw std::invalid_argument("Raft endpoint enabled flag must be zero or one");
            const auto result = raft_context->update_endpoint(target_id, host, port, enabled == 1);
            ByteWriter response;
            response.u64(result.endpoint_registry_generation);
            response.string(result.peer_id);
            response.string(result.host);
            response.u32(result.port);
            response.u8(result.enabled ? 1U : 0U);
            state_lock.unlock();
            write_frame(socket, kRaftUpdatePeerEndpointResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftApplyTrustPolicy) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("trust policy update requires an authenticated Raft-enabled node");
            }
            if (client.peer_id.empty() || !authentication->trusted_peers.is_operator(client.peer_id)) {
                throw std::runtime_error("trust policy update denied: authenticated identity lacks operator role");
            }
            if (!raft_context->apply_trust_policy) {
                throw std::runtime_error("trust policy updates are disabled: this node has no persistent trust-store path");
            }
            ByteReader reader(request.payload);
            const Bytes bundle = reader.blob(1024U * 1024U);
            require_empty(reader, "trailing bytes in signed trust policy update");
            const TrustPolicyApplyResult result = raft_context->apply_trust_policy(client.peer_id, bundle);
            ByteWriter response;
            response.u64(result.policy_epoch);
            response.u32(static_cast<std::uint32_t>(result.active_peers));
            response.u32(static_cast<std::uint32_t>(result.total_entries));
            state_lock.unlock();
            write_frame(socket, kRaftApplyTrustPolicyResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        if (request.type == kRaftStatus) {
            if (raft_context == nullptr || raft_context->adapter == nullptr ||
                authentication == nullptr || !session_security.has_value()) {
                throw std::runtime_error("Raft status requires an authenticated Raft-enabled node");
            }
            if (client.peer_id.empty()) throw std::runtime_error("Raft status requires an authenticated client identity");
            require_empty(ByteReader(request.payload), "Raft status request must be empty");
            const auto& node = raft_context->adapter->node();
            const auto& state = node.persistent_state();
            ByteWriter response;
            response.string(node.node_id());
            response.string(node.leader_id());
            response.u8(static_cast<Byte>(node.role()));
            response.u64(state.current_term);
            response.u64(state.commit_index);
            response.u64(state.last_applied);
            response.u64(node.last_log_index());
            response.u64(ledger.chain().height());
            response.fixed(ledger.chain().blocks().back().hash);
            const auto& membership = node.membership();
            response.u8(static_cast<Byte>(membership.phase()));
            const auto write_members = [&](const std::vector<std::string>& members) {
                response.u32(static_cast<std::uint32_t>(members.size()));
                for (const auto& member : members) response.string(member);
            };
            write_members(membership.voters());
            write_members(membership.learners());
            write_members(membership.new_voters());
            state_lock.unlock();
            write_frame(socket, kRaftStatusResult, response.data(), timeout,
                        session_security ? &*session_security : nullptr);
            return;
        }
        throw std::runtime_error("unsupported peer request message type");    } catch (const std::exception& exception) {
        send_error(socket, exception.what(), timeout, session_security ? &*session_security : nullptr);
    }
}

std::vector<ledger::Block> parse_block_batch(const Bytes& payload) {
    ByteReader reader(payload);
    const auto count = reader.u32();
    if (count > kMaximumBlocksPerBatch) throw std::runtime_error("peer returned too many blocks in one batch");
    std::vector<ledger::Block> blocks;
    blocks.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        blocks.push_back(ledger::Block::deserialize(reader.blob(kMaximumBlockBytes)));
    }
    require_empty(reader, "trailing bytes in block batch response");
    return blocks;
}

struct ChainLocatorEntry {
    std::uint64_t height{0};
    Hash hash{};
};

std::vector<ChainLocatorEntry> build_chain_locator(const ledger::Chain& chain) {
    const auto& blocks = chain.blocks();
    if (blocks.empty()) throw std::logic_error("cannot build a locator for an empty chain");
    std::vector<ChainLocatorEntry> locator;
    std::uint64_t height = chain.height();
    std::uint64_t step = 1;
    for (;;) {
        locator.push_back({height, blocks[static_cast<std::size_t>(height)].hash});
        if (height == 0) break;
        if (locator.size() >= 10) {
            if (step > UINT64_MAX / 2) step = UINT64_MAX;
            else step *= 2;
        }
        height = height > step ? height - step : 0;
        if (locator.size() > 64) throw std::length_error("chain locator exceeds protocol bound");
    }
    return locator;
}

std::pair<std::uint64_t, Hash> request_common_ancestor(
    const std::string& host, std::uint16_t port, const ledger::Ledger& ledger,
    const std::vector<ChainLocatorEntry>& locator, std::chrono::milliseconds timeout,
    const PeerAuthConfig* authentication) {
    if (locator.empty() || locator.size() > 64) throw std::invalid_argument("invalid local chain locator size");
    ClientConnection connection(host, port, ledger, timeout, authentication);
    ByteWriter request;
    request.u32(static_cast<std::uint32_t>(locator.size()));
    for (const auto& entry : locator) {
        request.u64(entry.height);
        request.fixed(entry.hash);
    }
    const Frame response = connection.request(kFindCommonAncestor, request.data());
    expect_type(response, kCommonAncestorResult);
    ByteReader reader(response.payload);
    const auto height = reader.u64();
    const Hash hash = reader.fixed<32>();
    require_empty(reader, "trailing bytes in common-ancestor response");
    if (height >= ledger.chain().blocks().size() || ledger.chain().blocks()[static_cast<std::size_t>(height)].hash != hash) {
        throw std::runtime_error("peer returned a common ancestor not present in the local chain");
    }
    return {height, hash};
}

PeerStatus request_status(const std::string& host, std::uint16_t port,
                          const ledger::Ledger& ledger,
                          std::chrono::milliseconds timeout, const PeerAuthConfig* authentication) {
    ClientConnection connection(host, port, ledger, timeout, authentication);
    const Frame response = connection.request(kStatus, {});
    expect_type(response, kStatusResult);
    return parse_status(response.payload);
}

std::vector<ledger::Block> request_blocks(const std::string& host, std::uint16_t port,
                                          const ledger::Ledger& ledger,
                                          std::uint64_t start_height,
                                          const Hash& expected_previous,
                                          std::uint32_t maximum_count,
                                          std::chrono::milliseconds timeout,
                                          const PeerAuthConfig* authentication) {
    ClientConnection connection(host, port, ledger, timeout, authentication);
    ByteWriter request;
    request.u64(start_height);
    request.fixed(expected_previous);
    request.u32(maximum_count);
    const Frame response = connection.request(kGetBlocks, request.data());
    expect_type(response, kBlocksResult);
    return parse_block_batch(response.payload);
}

} // namespace

PeerClient::PeerClient(std::string host, std::uint16_t port, std::chrono::milliseconds timeout)
    : host_(std::move(host)), port_(port), timeout_(timeout) {
    if (host_.empty()) throw std::invalid_argument("peer host cannot be empty");
    if (port_ == 0) throw std::invalid_argument("peer port must be non-zero");
    if (timeout_ <= std::chrono::milliseconds::zero()) throw std::invalid_argument("peer timeout must be positive");
}

PeerClient::PeerClient(std::string host, std::uint16_t port,
                       PeerAuthConfig authentication, std::chrono::milliseconds timeout)
    : host_(std::move(host)), port_(port), timeout_(timeout),
      identity_(std::move(authentication.identity)),
      trusted_peers_(std::move(authentication.trusted_peers)),
      expected_peer_id_(std::move(authentication.expected_peer_id)) {
    if (host_.empty()) throw std::invalid_argument("peer host cannot be empty");
    if (port_ == 0) throw std::invalid_argument("peer port must be non-zero");
    if (timeout_ <= std::chrono::milliseconds::zero()) throw std::invalid_argument("peer timeout must be positive");
    if (!identity_) throw std::invalid_argument("authenticated peer client requires a local node identity");
    if (expected_peer_id_.empty() || trusted_peers_.find(expected_peer_id_) == nullptr) {
        throw std::invalid_argument("expected peer ID must exist in the trusted peer store");
    }
    auth_config_.identity = identity_;
    auth_config_.trusted_peers = trusted_peers_;
    auth_config_.expected_peer_id = expected_peer_id_;
}

PeerStatus PeerClient::status(const ledger::Ledger& local_ledger) const {
    return request_status(host_, port_, local_ledger, timeout_, identity_ ? &auth_config_ : nullptr);
}

bool PeerClient::submit_transaction(const ledger::Ledger& local_ledger,
                                    const ledger::Transaction& transaction,
                                    std::string* error) const {
    ClientConnection connection(host_, port_, local_ledger, timeout_, identity_ ? &auth_config_ : nullptr);
    ByteWriter request;
    request.blob(transaction.serialize());
    const Frame response = connection.request(kSubmitTransaction, request.data());
    expect_type(response, kSubmitTransactionResult);
    ByteReader reader(response.payload);
    const bool accepted = reader.u8() == 1;
    const std::string message = reader.string(kMaximumErrorBytes);
    require_empty(reader, "trailing bytes in transaction result");
    if (error != nullptr) *error = accepted ? std::string{} : message;
    return accepted;
}

void PeerClient::submit_block(const ledger::Ledger& local_ledger,
                              const ledger::Block& block) const {
    ClientConnection connection(host_, port_, local_ledger, timeout_, identity_ ? &auth_config_ : nullptr);
    ByteWriter request;
    request.blob(block.serialize());
    const Frame response = connection.request(kSubmitBlock, request.data());
    expect_type(response, kSubmitBlockResult);
    ByteReader reader(response.payload);
    if (reader.u8() != 1) throw std::runtime_error("peer did not accept submitted block");
    require_empty(reader, "trailing bytes in block result");
}

SyncResult PeerClient::synchronize(
    ledger::Ledger& local_ledger,
    std::size_t maximum_batches,
    const std::vector<std::pair<std::string, std::uint64_t>>& protected_transactions) const {
    if (maximum_batches == 0) throw std::invalid_argument("maximum sync batches must be positive");
    if (maximum_batches > std::numeric_limits<std::size_t>::max() / kMaximumBlocksPerBatch) {
        throw std::length_error("maximum sync batch limit overflows");
    }

    const PeerStatus remote = request_status(host_, port_, local_ledger, timeout_, identity_ ? &auth_config_ : nullptr);
    const std::size_t maximum_blocks = maximum_batches * kMaximumBlocksPerBatch;
    const auto local_locator = build_chain_locator(local_ledger.chain());
    const auto [common_height, common_hash] = request_common_ancestor(
        host_, port_, local_ledger, local_locator, timeout_, identity_ ? &auth_config_ : nullptr);
    if (common_height > remote.height) {
        throw std::runtime_error("peer reported a common ancestor above its advertised tip");
    }
    const std::uint64_t branch_blocks = remote.height - common_height;
    if (branch_blocks > static_cast<std::uint64_t>(maximum_blocks)) {
        throw std::length_error("peer divergent branch exceeds configured synchronization block limit");
    }

    SyncResult result;
    result.peer_height = remote.height;
    result.final_height = local_ledger.chain().height();
    result.common_ancestor_height = common_height;

    // Reuse the already validated local prefix through the highest common
    // ancestor, then download only the peer's divergent suffix. Candidate
    // validation and fork choice happen before canonical state is touched.
    const auto& local_blocks = local_ledger.chain().blocks();
    std::vector<ledger::Block> candidate_chain(
        local_blocks.begin(), local_blocks.begin() + static_cast<std::ptrdiff_t>(common_height + 1));
    std::uint64_t next_height = common_height + 1;
    Hash expected_previous = common_hash;

    for (std::size_t batch = 0; next_height <= remote.height; ++batch) {
        if (batch >= maximum_batches) {
            throw std::runtime_error("peer synchronization exceeded configured batch limit");
        }
        auto blocks = request_blocks(host_, port_, local_ledger, next_height, expected_previous,
                                     kMaximumBlocksPerBatch, timeout_, identity_ ? &auth_config_ : nullptr);
        if (blocks.empty()) throw std::runtime_error("peer ended synchronization before its advertised height");
        if (blocks.size() > kMaximumBlocksPerBatch) throw std::runtime_error("peer exceeded block batch limit");
        for (const auto& block : blocks) {
            if (block.height != next_height || block.previous_hash != expected_previous) {
                throw std::runtime_error("peer returned a non-contiguous candidate chain");
            }
            expected_previous = block.hash;
            candidate_chain.push_back(block);
            ++next_height;
        }
    }

    // A peer changing its advertised tip while the suffix is being fetched
    // invalidates this attempt. No local state has been changed yet.
    const PeerStatus final_remote = request_status(host_, port_, local_ledger, timeout_, identity_ ? &auth_config_ : nullptr);
    if (final_remote.height != remote.height || final_remote.tip_hash != remote.tip_hash) {
        throw std::runtime_error("peer chain changed during synchronization; rerun sync to fetch a stable candidate");
    }
    if (candidate_chain.back().hash != remote.tip_hash) {
        throw std::runtime_error("downloaded candidate tip does not match the peer's advertised tip");
    }

    result.blocks_received = candidate_chain.size() - static_cast<std::size_t>(common_height) - 1U;
    const auto adoption = local_ledger.consider_chain(candidate_chain, protected_transactions);
    result.common_ancestor_height = adoption.common_ancestor_height;
    result.chain_adopted = adoption.adopted;
    result.chain_reorganized = adoption.adopted &&
        adoption.common_ancestor_height < adoption.previous_height;
    result.requeued_transactions = adoption.requeued_transactions;
    result.dropped_detached_transactions = adoption.dropped_detached_transactions;
    result.already_current = adoption.same_chain ||
        (!adoption.adopted && remote.height < adoption.previous_height &&
         adoption.common_ancestor_height == remote.height);
    result.local_chain_preferred = !adoption.adopted && !adoption.same_chain && !result.already_current;
    result.final_height = local_ledger.chain().height();
    result.peer_height = final_remote.height;
    return result;
}

RaftPeerStatus PeerClient::raft_status(const ledger::Ledger& local_ledger) const {
    if (!identity_) throw std::logic_error("Raft status requires an authenticated peer client");
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    const Frame response = connection.request(kRaftStatus, {});
    expect_type(response, kRaftStatusResult);
    ByteReader reader(response.payload);
    RaftPeerStatus status;
    status.node_id = reader.string(128);
    status.leader_id = reader.string(128);
    const Byte role = reader.u8();
    if (role > static_cast<Byte>(consensus::RaftRole::Leader)) {
        throw std::runtime_error("peer returned an invalid Raft role");
    }
    status.role = static_cast<consensus::RaftRole>(role);
    status.current_term = reader.u64();
    status.commit_index = reader.u64();
    status.last_applied = reader.u64();
    status.last_log_index = reader.u64();
    status.ledger_height = reader.u64();
    status.ledger_tip_hash = reader.fixed<32>();
    const Byte phase = reader.u8();
    if (phase > static_cast<Byte>(consensus::MembershipPhase::FinalPending)) {
        throw std::runtime_error("peer returned an invalid Raft membership phase");
    }
    status.membership_phase = static_cast<consensus::MembershipPhase>(phase);
    const auto read_members = [&]() {
        const auto count = reader.u32();
        if (count > consensus::kRaftMaximumMembers) {
            throw std::length_error("peer returned an oversized Raft membership list");
        }
        std::vector<std::string> members;
        members.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) members.push_back(reader.string(128));
        return members;
    };
    status.voters = read_members();
    status.learners = read_members();
    status.joint_voters = read_members();
    require_empty(reader, "trailing bytes in Raft status response");
    return status;
}

RaftBlockProposalResult PeerClient::propose_raft_block(
    const ledger::Ledger& local_ledger, const Address& miner) const {
    if (!identity_) throw std::logic_error("Raft block proposal requires an authenticated peer client");
    if (!crypto::is_valid_address(miner)) throw std::invalid_argument("invalid miner address for Raft block proposal");
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    ByteWriter request;
    request.string(miner);
    const Frame response = connection.request(kRaftProposeBlock, request.data());
    expect_type(response, kRaftProposeBlockResult);
    ByteReader reader(response.payload);
    RaftBlockProposalResult result;
    result.index = reader.u64();
    result.term = reader.u64();
    result.block_hash = reader.fixed<32>();
    require_empty(reader, "trailing bytes in Raft block proposal response");
    return result;
}

RaftMembershipProposalResult PeerClient::change_raft_membership(
    const ledger::Ledger& local_ledger, const std::vector<std::string>& new_members) const {
    if (!identity_) throw std::logic_error("Raft membership changes require an authenticated peer client");
    if (new_members.size() < 3 || new_members.size() > consensus::kRaftMaximumMembers ||
        new_members.size() % 2U == 0U) {
        throw std::invalid_argument("new Raft voter set must contain 3, 5, 7, or 9 members");
    }
    ByteWriter request;
    request.u32(static_cast<std::uint32_t>(new_members.size()));
    for (const auto& member : new_members) request.string(member);
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    const Frame response = connection.request(kRaftChangeMembership, request.data());
    expect_type(response, kRaftChangeMembershipResult);
    ByteReader reader(response.payload);
    RaftMembershipProposalResult result;
    result.commit_index = reader.u64();
    result.log_index = reader.u64();
    result.term = reader.u64();
    require_empty(reader, "trailing bytes in Raft membership change response");
    return result;
}

TrustPolicyApplyResult PeerClient::push_trust_policy(
    const ledger::Ledger& local_ledger, const Bytes& signed_policy_bundle) const {
    if (!identity_) throw std::logic_error("trust policy updates require an authenticated peer client");
    if (signed_policy_bundle.empty() || signed_policy_bundle.size() > 1024U * 1024U) {
        throw std::length_error("signed trust policy size is invalid");
    }
    if (!trusted_peers_.is_operator(identity_->peer_id())) {
        throw std::runtime_error("local identity must be an active operator to push a trust policy");
    }
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    ByteWriter request;
    request.blob(signed_policy_bundle);
    const Frame response = connection.request(kRaftApplyTrustPolicy, request.data());
    expect_type(response, kRaftApplyTrustPolicyResult);
    ByteReader reader(response.payload);
    TrustPolicyApplyResult result;
    result.policy_epoch = reader.u64();
    result.active_peers = reader.u32();
    result.total_entries = reader.u32();
    require_empty(reader, "trailing bytes in trust policy update response");
    return result;
}

RaftPeerHealthReport PeerClient::raft_peer_health(const ledger::Ledger& local_ledger) const {
    if (!identity_) throw std::logic_error("Raft peer health requires an authenticated peer client");
    if (!trusted_peers_.is_operator(identity_->peer_id())) {
        throw std::runtime_error("local identity must be an active operator to read Raft peer health");
    }
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    const Frame response = connection.request(kRaftPeerHealth, {});
    expect_type(response, kRaftPeerHealthResult);
    ByteReader reader(response.payload);
    RaftPeerHealthReport report;
    report.endpoint_registry_generation = reader.u64();
    report.outbound_dropped_total = reader.u64();
    report.worker_count = reader.u32();
    const auto count = reader.u32();
    if (count > consensus::kRaftMaximumMembers) throw std::length_error("peer health response exceeds the cluster peer limit");
    report.peers.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RaftPeerHealth peer;
        peer.peer_id = reader.string(128);
        peer.host = reader.string(253);
        const auto raw_port = reader.u32();
        const Byte enabled = reader.u8();
        if (raw_port == 0 || raw_port > UINT16_MAX) throw std::runtime_error("peer health endpoint port is outside valid bounds");
        peer.port = static_cast<std::uint16_t>(raw_port);
        const Byte in_flight = reader.u8();
        if (enabled > 1 || in_flight > 1 || peer.port == 0 || !valid_endpoint_host(peer.host)) {
            throw std::runtime_error("peer health response contains invalid endpoint state");
        }
        peer.enabled = enabled == 1;
        peer.in_flight = in_flight == 1;
        peer.queued_messages = reader.u32();
        peer.attempts = reader.u64();
        peer.successes = reader.u64();
        peer.failures = reader.u64();
        peer.dropped = reader.u64();
        peer.consecutive_failures = reader.u32();
        peer.last_success_unix_ms = reader.u64();
        peer.last_failure_unix_ms = reader.u64();
        if (!peer.enabled) peer.lifecycle_state = PeerLifecycleState::Disabled;
        else if (peer.in_flight && peer.consecutive_failures >= kPeerOfflineFailureThreshold)
            peer.lifecycle_state = PeerLifecycleState::HalfOpen;
        else if (peer.consecutive_failures >= kPeerOfflineFailureThreshold)
            peer.lifecycle_state = PeerLifecycleState::Offline;
        else if (peer.consecutive_failures > 0)
            peer.lifecycle_state = PeerLifecycleState::Degraded;
        else if (peer.last_success_unix_ms > 0)
            peer.lifecycle_state = PeerLifecycleState::Healthy;
        else peer.lifecycle_state = PeerLifecycleState::Unknown;
        peer.retry_delay_ms = reader.u64();
        peer.last_error = reader.string(256);
        report.peers.push_back(std::move(peer));
    }
    require_empty(reader, "trailing bytes in Raft peer health response");
    return report;
}

RaftPeerEndpointUpdateResult PeerClient::update_raft_peer_endpoint(
    const ledger::Ledger& local_ledger, const std::string& peer_id,
    const std::string& host, std::uint16_t port, bool enabled) const {
    if (!identity_) throw std::logic_error("Raft endpoint management requires an authenticated peer client");
    if (!trusted_peers_.is_operator(identity_->peer_id())) {
        throw std::runtime_error("local identity must be an active operator to manage Raft endpoints");
    }
    if (peer_id.empty() || peer_id.size() > 128 || !valid_endpoint_host(host) || port == 0) {
        throw std::invalid_argument("Raft endpoint update target, host, or port is invalid");
    }
    ClientConnection connection(host_, port_, local_ledger, timeout_, &auth_config_);
    ByteWriter request;
    request.string(peer_id);
    request.string(host);
    request.u32(port);
    request.u8(enabled ? 1U : 0U);
    const Frame response = connection.request(kRaftUpdatePeerEndpoint, request.data());
    expect_type(response, kRaftUpdatePeerEndpointResult);
    ByteReader reader(response.payload);
    RaftPeerEndpointUpdateResult result;
    result.endpoint_registry_generation = reader.u64();
    result.peer_id = reader.string(128);
    result.host = reader.string(253);
    const auto raw_port = reader.u32();
    const Byte result_enabled = reader.u8();
    if (raw_port == 0 || raw_port > UINT16_MAX) throw std::runtime_error("Raft endpoint update response port is outside valid bounds");
    result.port = static_cast<std::uint16_t>(raw_port);
    if (result_enabled > 1 || result.port == 0 || !valid_endpoint_host(result.host)) {
        throw std::runtime_error("Raft endpoint update response contains invalid endpoint data");
    }
    result.enabled = result_enabled == 1;
    require_empty(reader, "trailing bytes in Raft endpoint update response");
    return result;
}

void PeerClient::send_raft_message(
    const PeerChainParameters& parameters,
    const consensus::RaftMessage& message) const {
    if (!identity_) throw std::logic_error("Raft RPC requires an authenticated peer client");
    if (message.from != identity_->peer_id() || message.to != expected_peer_id_) {
        throw std::invalid_argument("outbound Raft message does not match the authenticated local/remote identities");
    }
    ClientConnection connection(host_, port_, parameters, timeout_, &auth_config_);
    ByteWriter request;
    request.blob(message.serialize());
    const Frame response = connection.request(kRaftMessage, request.data());
    expect_type(response, kRaftMessageResult);
    require_empty(ByteReader(response.payload), "Raft RPC acknowledgement must be empty");
}

bool PeerClient::send_gossip_transaction(const PeerChainParameters& parameters,
                                         const ledger::Transaction& transaction,
                                         std::string* error) const {
    if (!identity_) throw std::logic_error("transaction gossip requires an authenticated peer client");
    ClientConnection connection(host_, port_, parameters, timeout_, &auth_config_);
    ByteWriter request;
    request.blob(transaction.serialize());
    const Frame response = connection.request(kSubmitTransaction, request.data());
    expect_type(response, kSubmitTransactionResult);
    ByteReader reader(response.payload);
    const Byte accepted = reader.u8();
    if (accepted > 1) throw std::runtime_error("peer returned an invalid transaction acceptance flag");
    const std::string message = reader.string(kMaximumErrorBytes);
    require_empty(reader, "trailing bytes in transaction gossip result");
    if (accepted == 1) {
        if (error != nullptr) error->clear();
        return true;
    }
    // A duplicate is proof that the peer already has the transaction. It is
    // therefore a successful delivery, not a reason to retry forever.
    if (message.find("duplicate") != std::string::npos ||
        message.find("replayed") != std::string::npos) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (error != nullptr) *error = message;
    return false;
}

struct NodeServer::Impl {
    struct InboundConnection {
        tcp::socket::native_handle_type native_socket{-1};
        tcp protocol{tcp::v4()};
        InboundConnection() = default;
        InboundConnection(tcp::socket::native_handle_type native, tcp value)
            : native_socket(native), protocol(value) {}
    };

    struct OutboundMessage {
        enum class Kind : std::uint8_t { Raft, Transaction };
        Kind kind{Kind::Raft};
        consensus::RaftMessage raft_message;
        Bytes transaction_payload;
        std::string transaction_id;
        std::size_t encoded_size{0};
    };

    struct PeerQueues {
        std::deque<OutboundMessage> raft;
        std::deque<OutboundMessage> gossip;
        // Consensus traffic is preferred, but a sustained heartbeat/RPC stream
        // must not starve best-effort transaction dissemination forever.
        std::size_t consecutive_raft_dispatches{0};
    };

    struct InboundTokenBucket {
        double tokens{0.0};
        std::chrono::steady_clock::time_point updated{};
        std::chrono::steady_clock::time_point last_seen{};
    };

    struct PeerCounters {
        std::uint64_t attempts{0};
        std::uint64_t successes{0};
        std::uint64_t failures{0};
        std::uint64_t dropped{0};
        std::uint32_t consecutive_failures{0};
        std::uint64_t last_success_unix_ms{0};
        std::uint64_t last_failure_unix_ms{0};
        std::string last_error;
    };

    struct DispatchWork {
        std::string peer_id;
        RaftPeerEndpoint endpoint;
        OutboundMessage item;
        std::chrono::steady_clock::time_point started_at{};
    };

    Impl(ledger::Ledger& ledger_ref, const std::string& address, std::uint16_t requested_port,
         std::chrono::milliseconds requested_timeout, std::optional<PeerAuthConfig> auth = std::nullopt,
         std::optional<RaftNetworkConfig> raft = std::nullopt,
         InboundAdmissionConfig admission = {})
        : ledger(ledger_ref), timeout(requested_timeout), authentication(std::move(auth)),
          raft_configuration(std::move(raft)), inbound_admission(std::move(admission)),
          global_inbound_tokens(static_cast<double>(inbound_admission.global_burst)),
          io(), acceptor(io) {
        if (timeout <= std::chrono::milliseconds::zero()) throw std::invalid_argument("server timeout must be positive");
        validate_inbound_admission_config(inbound_admission);
        const auto bind_address = boost::asio::ip::make_address(address);
        if (!authentication.has_value() && !bind_address.is_loopback()) {
            throw std::invalid_argument("unauthenticated peer listener is restricted to loopback; configure node identity and trust store for remote access");
        }
        tcp::endpoint endpoint(bind_address, requested_port);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen(boost::asio::socket_base::max_listen_connections);
        acceptor.non_blocking(true);
        bound_port = acceptor.local_endpoint().port();

        if (raft_configuration.has_value()) {
            inbound_admission = raft_configuration->inbound_admission;
            validate_inbound_admission_config(inbound_admission);
            global_inbound_tokens = static_cast<double>(inbound_admission.global_burst);
            if (!authentication.has_value() || !authentication->identity) {
                throw std::invalid_argument("Raft network mode requires an authenticated node identity");
            }
            if (raft_configuration->database_path.empty()) {
                throw std::invalid_argument("Raft network mode requires the Ledger database path");
            }
            if (!raft_configuration->trust_store_path.empty() && !authentication.has_value()) {
                throw std::invalid_argument("Raft trust policy replication requires authenticated peer configuration");
            }
            if (raft_configuration->outbound_timeout <= std::chrono::milliseconds::zero()) {
                throw std::invalid_argument("Raft outbound timeout must be positive");
            }
            if (raft_configuration->outbound_worker_count == 0 || raft_configuration->outbound_worker_count > 8) {
                throw std::invalid_argument("Raft outbound worker count must be between one and eight");
            }
            if (raft_configuration->maximum_queued_messages_per_peer == 0 ||
                raft_configuration->maximum_queued_messages_per_peer > kMaximumRaftOutboundMessages) {
                throw std::invalid_argument("Raft per-peer queue limit must be between one and 1024 messages");
            }
            if (raft_configuration->maximum_queued_gossip_messages_per_peer == 0 ||
                raft_configuration->maximum_queued_gossip_messages_per_peer >
                    raft_configuration->maximum_queued_messages_per_peer) {
                throw std::invalid_argument("per-peer transaction gossip queue limit must be positive and no larger than the total peer queue limit");
            }
            local_peer_id = authentication->identity->peer_id();
            if (raft_configuration->consensus.node_id != local_peer_id) {
                throw std::invalid_argument("Raft node ID must match the authenticated node identity");
            }
            const auto minimum_election_ms = raft_configuration->consensus.election_timeout_min.count();
            if (minimum_election_ms <= 0 || timeout.count() >= minimum_election_ms / 2) {
                throw std::invalid_argument("Raft inbound timeout must be less than half the minimum election timeout");
            }
            raft_members.insert(raft_configuration->consensus.members.begin(),
                                raft_configuration->consensus.members.end());
            raft_members.insert(raft_configuration->consensus.learners.begin(),
                                raft_configuration->consensus.learners.end());
            if (!raft_members.contains(local_peer_id)) {
                throw std::invalid_argument("local authenticated identity is absent from the Raft voter/learner roster");
            }
            for (const auto& peer : raft_configuration->peers) {
                if (peer.peer_id.empty() || peer.peer_id == local_peer_id || !valid_endpoint_host(peer.host) || peer.port == 0) {
                    throw std::invalid_argument("Raft peer endpoint contains an invalid identity, host, or port");
                }
                if (!raft_members.contains(peer.peer_id)) {
                    throw std::invalid_argument("Raft peer endpoint is not a member of the configured cluster");
                }
                if (authentication->trusted_peers.find(peer.peer_id) == nullptr) {
                    throw std::invalid_argument("Raft peer endpoint is not in the authenticated peer trust store");
                }
                if (!raft_endpoints.emplace(peer.peer_id, peer).second) {
                    throw std::invalid_argument("Raft peer endpoint list contains a duplicate peer ID");
                }
            }
            if (raft_endpoints.size() + 1U != raft_members.size()) {
                throw std::invalid_argument("Raft endpoint list must contain exactly one endpoint for every remote cluster member");
            }
            for (const auto& member : raft_members) {
                if (member != local_peer_id && !raft_endpoints.contains(member)) {
                    throw std::invalid_argument("Raft endpoint list is missing a configured cluster member");
                }
            }
            endpoint_registry_path = raft_configuration->endpoint_registry_path.empty()
                ? raft_configuration->database_path + ".endpoints"
                : raft_configuration->endpoint_registry_path;
            std::error_code exists_error;
            const bool registry_exists = std::filesystem::exists(endpoint_registry_path, exists_error);
            if (exists_error) throw std::system_error(exists_error, "inspect peer endpoint registry");
            if (registry_exists) {
                std::uint64_t loaded_generation = 0;
                raft_endpoints = load_endpoint_registry(endpoint_registry_path,
                    raft_configuration->consensus.cluster_id, raft_endpoints, loaded_generation);
                endpoint_registry_generation = loaded_generation;
            } else {
                endpoint_registry_generation = 1;
                save_endpoint_registry_atomic(endpoint_registry_path,
                    raft_configuration->consensus.cluster_id, endpoint_registry_generation, raft_endpoints);
            }
            for (const auto& [peer_id, peer_endpoint] : raft_endpoints) {
                (void)peer_endpoint;
                peer_counters.try_emplace(peer_id);
                peer_lifecycle.try_emplace(peer_id, peer_id, raft_configuration->peer_lifecycle);
                outbound_queues.try_emplace(peer_id);
                pending_gossip_ids.try_emplace(peer_id);
            }
            chain_parameters = peer_chain_parameters(ledger);
            raft_adapter = std::make_unique<consensus::RaftLedgerAdapter>(
                ledger, raft_configuration->database_path, raft_configuration->consensus);
        }
    }

    ~Impl() {
        stop_inbound_workers();
        stop_dispatcher();
    }

    static void validate_inbound_admission_config(const InboundAdmissionConfig& config) {
        if (!std::isfinite(config.global_refill_per_second) || config.global_refill_per_second <= 0.0 ||
            !std::isfinite(config.per_address_refill_per_second) || config.per_address_refill_per_second <= 0.0 ||
            config.global_burst == 0 || config.per_address_burst == 0 ||
            config.maximum_tracked_addresses == 0 ||
            config.address_idle_expiry <= std::chrono::seconds::zero() ||
            config.worker_count == 0 || config.worker_count > 16 ||
            config.maximum_queued_connections == 0 || config.maximum_queued_connections > 4096) {
            throw std::invalid_argument("inbound admission limits must be finite and strictly positive");
        }
    }

    [[nodiscard]] std::optional<RaftServerContext> make_raft_server_context() {
        if (!raft_adapter) return std::nullopt;
        RaftServerContext context;
        context.adapter = raft_adapter.get();
        context.members = &raft_members;
        context.local_peer_id = local_peer_id;
        context.enqueue = [this](std::vector<consensus::RaftMessage> messages) {
            enqueue_raft_messages(messages);
        };
        context.enqueue_transaction = [this](const ledger::Transaction& transaction,
                                              const std::string& source_peer) {
            enqueue_transaction(transaction, source_peer);
        };
        context.apply_trust_policy = [this](const std::string& signer_peer_id, const Bytes& bundle) {
            if (!authentication.has_value() || !raft_configuration.has_value() ||
                raft_configuration->trust_store_path.empty()) {
                throw std::runtime_error("Raft node has no configured trust-store path for policy persistence");
            }
            std::lock_guard trust_lock(trust_store_mutex);
            PeerTrustStore updated = authentication->trusted_peers;
            updated.apply_signed_policy(bundle, raft_configuration->consensus.cluster_id, signer_peer_id);
            for (const auto& member_id : raft_members) {
                if (member_id != local_peer_id && !updated.trusts(member_id)) {
                    throw std::runtime_error("trust policy would revoke a pre-provisioned Raft member; complete membership removal before revocation");
                }
            }
            updated.save_atomic(raft_configuration->trust_store_path);
            authentication->trusted_peers = std::move(updated);
            return TrustPolicyApplyResult{
                authentication->trusted_peers.policy_epoch(),
                authentication->trusted_peers.active_size(),
                authentication->trusted_peers.size()};
        };
        context.peer_health = [this] { return peer_health_report(); };
        context.update_endpoint = [this](const std::string& peer_id, const std::string& host,
                                         std::uint16_t port, bool enabled) {
            return update_peer_endpoint(peer_id, host, port, enabled);
        };
        return context;
    }

    void process_inbound_socket(tcp::socket& socket) noexcept {
        try {
            auto raft_context = make_raft_server_context();
            serve_connection(socket, ledger, timeout,
                             authentication ? &*authentication : nullptr,
                             raft_context ? &*raft_context : nullptr, state_mutex);
        } catch (...) {
            // A failed connection is isolated to that worker. serve_connection
            // reports protocol failures to the remote peer when possible.
        }
        ErrorCode ignored;
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }

    void inbound_worker_loop() noexcept {
        // Each worker owns one io_context. Accepted native handles are assigned
        // to that context only after dequeue, so timed_operation() can safely
        // restart/run the context without racing other workers.
        boost::asio::io_context worker_io;
        for (;;) {
            std::optional<InboundConnection> connection;
            {
                std::unique_lock queue_lock(inbound_queue_mutex);
                inbound_condition.wait(queue_lock, [this] {
                    return inbound_stopping || !inbound_queue.empty();
                });
                if (inbound_stopping && inbound_queue.empty()) return;
                connection.emplace(std::move(inbound_queue.front()));
                inbound_queue.pop_front();
            }
            tcp::socket socket(worker_io);
            ErrorCode assign_error;
            socket.assign(connection->protocol, connection->native_socket, assign_error);
            if (assign_error) {
                (void)::close(connection->native_socket);
                continue;
            }
            process_inbound_socket(socket);
        }
    }

    void start_inbound_workers() {
        std::lock_guard lifecycle_lock(inbound_lifecycle_mutex);
        if (inbound_stopping) return;
        if (!inbound_workers.empty()) return;
        try {
            inbound_workers.reserve(inbound_admission.worker_count);
            for (std::size_t i = 0; i < inbound_admission.worker_count; ++i) {
                inbound_workers.emplace_back([this] { inbound_worker_loop(); });
            }
        } catch (...) {
            {
                std::lock_guard queue_lock(inbound_queue_mutex);
                inbound_stopping = true;
            }
            inbound_condition.notify_all();
            for (auto& worker : inbound_workers) {
                if (worker.joinable()) worker.join();
            }
            inbound_workers.clear();
            throw;
        }
    }

    [[nodiscard]] bool enqueue_inbound_socket(InboundConnection connection) {
        {
            std::lock_guard queue_lock(inbound_queue_mutex);
            if (inbound_stopping || inbound_queue.size() >= inbound_admission.maximum_queued_connections) {
                (void)::close(connection.native_socket);
                return false;
            }
            inbound_queue.emplace_back(connection);
        }
        inbound_condition.notify_one();
        return true;
    }

    void stop_inbound_workers() noexcept {
        std::lock_guard lifecycle_lock(inbound_lifecycle_mutex);
        {
            std::lock_guard queue_lock(inbound_queue_mutex);
            inbound_stopping = true;
            for (auto& connection : inbound_queue) {
                (void)::close(connection.native_socket);
            }
            inbound_queue.clear();
        }
        inbound_condition.notify_all();
        for (auto& worker : inbound_workers) {
            if (worker.joinable()) worker.join();
        }
        inbound_workers.clear();
    }

    bool admit_connection(const tcp::socket& socket) {
        ErrorCode endpoint_error;
        const auto remote = socket.remote_endpoint(endpoint_error);
        if (endpoint_error) return false;
        const std::string address = remote.address().to_string();
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(admission_mutex);

        const double elapsed_global = std::chrono::duration<double>(now - global_inbound_updated).count();
        global_inbound_tokens = std::min(static_cast<double>(inbound_admission.global_burst),
            global_inbound_tokens + elapsed_global * inbound_admission.global_refill_per_second);
        global_inbound_updated = now;

        auto bucket = inbound_address_buckets.find(address);
        if (bucket == inbound_address_buckets.end()) {
            if (inbound_address_buckets.size() >= inbound_admission.maximum_tracked_addresses) {
                for (auto it = inbound_address_buckets.begin(); it != inbound_address_buckets.end();) {
                    if (now - it->second.last_seen >= inbound_admission.address_idle_expiry) {
                        it = inbound_address_buckets.erase(it);
                    } else {
                        ++it;
                    }
                }
                if (inbound_address_buckets.size() >= inbound_admission.maximum_tracked_addresses) return false;
            }
            InboundTokenBucket fresh;
            fresh.tokens = static_cast<double>(inbound_admission.per_address_burst);
            fresh.updated = now;
            fresh.last_seen = now;
            bucket = inbound_address_buckets.emplace(address, fresh).first;
        }

        const double elapsed_address = std::chrono::duration<double>(now - bucket->second.updated).count();
        bucket->second.tokens = std::min(static_cast<double>(inbound_admission.per_address_burst),
            bucket->second.tokens + elapsed_address * inbound_admission.per_address_refill_per_second);
        bucket->second.updated = now;

        if (global_inbound_tokens < 1.0 || bucket->second.tokens < 1.0) return false;
        global_inbound_tokens -= 1.0;
        bucket->second.tokens -= 1.0;
        bucket->second.last_seen = now;
        return true;
    }

    std::size_t queued_for_peer(const PeerQueues& queue) const noexcept {
        return queue.raft.size() + queue.gossip.size();
    }

    void drop_gossip_front(const std::string& peer_id, PeerQueues& queue) {
        if (queue.gossip.empty()) return;
        auto& item = queue.gossip.front();
        outbound_bytes -= item.encoded_size;
        outbound_gossip_bytes -= item.encoded_size;
        --outbound_count;
        --outbound_gossip_count;
        pending_gossip_ids[peer_id].erase(item.transaction_id);
        queue.gossip.pop_front();
        ++outbound_dropped;
        ++peer_counters[peer_id].dropped;
    }

    bool discard_any_gossip() {
        for (auto& [peer_id, queue] : outbound_queues) {
            if (!queue.gossip.empty()) {
                drop_gossip_front(peer_id, queue);
                return true;
            }
        }
        return false;
    }

    void enqueue_raft_messages(const std::vector<consensus::RaftMessage>& messages) {
        if (!raft_configuration.has_value()) return;
        std::lock_guard queue_lock(outbound_mutex);
        std::lock_guard endpoint_lock(endpoint_mutex);
        for (const auto& message : messages) {
            if (message.to == local_peer_id) continue;
            const auto endpoint = raft_endpoints.find(message.to);
            if (endpoint == raft_endpoints.end()) {
                throw std::runtime_error("Raft generated a message for a node without a configured endpoint");
            }
            auto& counters = peer_counters[message.to];
            if (!endpoint->second.enabled) {
                ++outbound_dropped;
                ++counters.dropped;
                continue;
            }
            const std::size_t encoded_size = message.serialize().size();
            auto& queue = outbound_queues[message.to];
            const auto total_peer_items = [&] {
                return queued_for_peer(queue) + (in_flight_peers.contains(message.to) ? 1U : 0U);
            };
            const auto discard_heartbeat = [&]() -> bool {
                const auto heartbeat = std::find_if(queue.raft.begin(), queue.raft.end(), [](const OutboundMessage& queued) {
                    return queued.raft_message.type == consensus::RaftMessageType::AppendEntries &&
                           queued.raft_message.entries.empty();
                });
                if (heartbeat == queue.raft.end()) return false;
                outbound_bytes -= heartbeat->encoded_size;
                --outbound_count;
                queue.raft.erase(heartbeat);
                ++outbound_dropped;
                ++counters.dropped;
                return true;
            };

            bool capacity_available = total_peer_items() < raft_configuration->maximum_queued_messages_per_peer &&
                outbound_count < kMaximumRaftOutboundMessages &&
                outbound_bytes <= kMaximumRaftOutboundBytes &&
                encoded_size <= kMaximumRaftOutboundBytes - outbound_bytes;
            // Best-effort gossip is evicted before consensus messages. The
            // independent gossip ceilings normally leave this capacity unused.
            while (!capacity_available && discard_any_gossip()) {
                capacity_available = total_peer_items() < raft_configuration->maximum_queued_messages_per_peer &&
                    outbound_count < kMaximumRaftOutboundMessages &&
                    outbound_bytes <= kMaximumRaftOutboundBytes &&
                    encoded_size <= kMaximumRaftOutboundBytes - outbound_bytes;
            }
            if (!capacity_available) {
                const bool incoming_heartbeat = message.type == consensus::RaftMessageType::AppendEntries &&
                                                message.entries.empty();
                if (incoming_heartbeat || !discard_heartbeat()) {
                    ++outbound_dropped;
                    ++counters.dropped;
                    continue;
                }
                capacity_available = total_peer_items() < raft_configuration->maximum_queued_messages_per_peer &&
                    outbound_count < kMaximumRaftOutboundMessages &&
                    outbound_bytes <= kMaximumRaftOutboundBytes &&
                    encoded_size <= kMaximumRaftOutboundBytes - outbound_bytes;
            }
            if (!capacity_available) {
                ++outbound_dropped;
                ++counters.dropped;
                continue;
            }
            OutboundMessage item;
            item.kind = OutboundMessage::Kind::Raft;
            item.raft_message = message;
            item.encoded_size = encoded_size;
            queue.raft.push_back(std::move(item));
            ++outbound_count;
            outbound_bytes += encoded_size;
        }
        outbound_condition.notify_all();
    }

    void enqueue_transaction(const ledger::Transaction& transaction, const std::string& exclude_peer = {}) {
        const std::vector<ledger::Transaction> single{transaction};
        enqueue_transactions(single, exclude_peer);
    }

    void enqueue_transactions(const std::vector<ledger::Transaction>& transactions,
                              const std::string& exclude_peer = {}) {
        if (!raft_configuration.has_value() || transactions.empty()) return;
        std::vector<std::pair<std::string, Bytes>> encoded_transactions;
        encoded_transactions.reserve(transactions.size());
        for (const auto& transaction : transactions) {
            Bytes payload = transaction.serialize();
            if (payload.empty() || payload.size() > 1U * 1024U * 1024U) continue;
            const Hash transaction_hash = transaction.id();
            encoded_transactions.emplace_back(crypto::hex(transaction_hash.data(), transaction_hash.size()),
                                               std::move(payload));
        }
        if (encoded_transactions.empty()) return;

        std::lock_guard queue_lock(outbound_mutex);
        std::lock_guard endpoint_lock(endpoint_mutex);
        for (const auto& [peer_id, endpoint] : raft_endpoints) {
            if (peer_id == exclude_peer || !endpoint.enabled) continue;
            auto& queue = outbound_queues[peer_id];
            auto& pending = pending_gossip_ids[peer_id];
            auto& delivered = delivered_gossip_ids[peer_id];
            for (const auto& [transaction_id, payload] : encoded_transactions) {
                if (pending.contains(transaction_id) || delivered.contains(transaction_id)) continue;
                const std::size_t peer_total = queued_for_peer(queue) +
                    (in_flight_peers.contains(peer_id) ? 1U : 0U);
                const bool capacity_available = pending.size() < raft_configuration->maximum_queued_gossip_messages_per_peer &&
                    peer_total < raft_configuration->maximum_queued_messages_per_peer &&
                    outbound_gossip_count < kMaximumGossipOutboundMessages &&
                    outbound_gossip_bytes <= kMaximumGossipOutboundBytes &&
                    payload.size() <= kMaximumGossipOutboundBytes - outbound_gossip_bytes &&
                    outbound_count < kMaximumRaftOutboundMessages &&
                    outbound_bytes <= kMaximumRaftOutboundBytes &&
                    payload.size() <= kMaximumRaftOutboundBytes - outbound_bytes;
                // Queue pressure defers this durable mempool transaction. The
                // periodic pump retries admission; it is not counted as lost.
                if (!capacity_available) break;

                OutboundMessage item;
                item.kind = OutboundMessage::Kind::Transaction;
                item.transaction_payload = payload;
                item.transaction_id = transaction_id;
                item.encoded_size = payload.size();
                queue.gossip.push_back(std::move(item));
                pending.insert(transaction_id);
                ++outbound_count;
                ++outbound_gossip_count;
                outbound_bytes += payload.size();
                outbound_gossip_bytes += payload.size();
            }
        }
        outbound_condition.notify_all();
    }

    void pump_pending_gossip_if_due() {
        if (!raft_configuration.has_value()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_gossip_scan < std::chrono::milliseconds(250)) return;
        last_gossip_scan = now;
        // Mempool admission is durable and already bounded to 10,000 entries /
        // 32 MiB. Re-scanning it applies backpressure without making the RAM
        // dispatch queue proportional to the full mempool size.
        enqueue_transactions(ledger.mempool().transactions());
    }

    void mark_gossip_delivered(const std::string& peer_id, const std::string& transaction_id) {
        auto& delivered = delivered_gossip_ids[peer_id];
        auto& order = delivered_gossip_order[peer_id];
        if (delivered.insert(transaction_id).second) order.push_back(transaction_id);
        constexpr std::size_t kMaximumDeliveredIdsPerPeer = ledger::kMaximumMempoolTransactions * 2U;
        while (order.size() > kMaximumDeliveredIdsPerPeer) {
            delivered.erase(order.front());
            order.pop_front();
        }
    }

    void start_dispatcher() {
        if (!raft_configuration.has_value()) return;
        std::lock_guard lifecycle_lock(dispatcher_lifecycle_mutex);
        if (!dispatchers.empty()) return;
        {
            std::lock_guard queue_lock(outbound_mutex);
            outbound_stopping = false;
        }
        if (!startup_gossip_enqueued) {
            startup_gossip_enqueued = true;
            // The durable mempool is the restart recovery source for any
            // transaction whose previous process-local dispatch queue vanished.
            enqueue_transactions(ledger.mempool().transactions());
        }
        const std::size_t worker_count = std::min(raft_configuration->outbound_worker_count, raft_endpoints.size());
        for (std::size_t index = 0; index < worker_count; ++index) {
            dispatchers.emplace_back([this] { dispatcher_loop(); });
        }
    }

    void stop_dispatcher() noexcept {
        std::lock_guard lifecycle_lock(dispatcher_lifecycle_mutex);
        if (dispatchers.empty()) return;
        {
            std::lock_guard queue_lock(outbound_mutex);
            outbound_stopping = true;
            for (auto& [peer_id, queue] : outbound_queues) {
                auto& counters = peer_counters[peer_id];
                for (const auto& item : queue.raft) {
                    outbound_bytes -= item.encoded_size;
                    --outbound_count;
                    ++counters.dropped;
                    ++outbound_dropped;
                }
                for (const auto& item : queue.gossip) {
                    outbound_bytes -= item.encoded_size;
                    outbound_gossip_bytes -= item.encoded_size;
                    --outbound_count;
                    --outbound_gossip_count;
                    pending_gossip_ids[peer_id].erase(item.transaction_id);
                    ++counters.dropped;
                    ++outbound_dropped;
                }
                queue.raft.clear();
                queue.gossip.clear();
            }
        }
        outbound_condition.notify_all();
        for (auto& worker : dispatchers) {
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        }
        dispatchers.clear();
        in_flight_peers.clear();
    }

    std::optional<DispatchWork> take_dispatch_work(std::unique_lock<std::mutex>& lock) {
        for (;;) {
            if (outbound_stopping) return std::nullopt;
            const auto now = std::chrono::steady_clock::now();
            std::optional<std::chrono::steady_clock::time_point> earliest_retry;
            std::vector<std::string> order;
            order.reserve(outbound_queues.size());
            auto begin = outbound_queues.begin();
            if (!scheduler_cursor.empty()) {
                begin = outbound_queues.upper_bound(scheduler_cursor);
                if (begin == outbound_queues.end()) begin = outbound_queues.begin();
            }
            for (auto it = begin; it != outbound_queues.end(); ++it) order.push_back(it->first);
            for (auto it = outbound_queues.begin(); it != begin; ++it) order.push_back(it->first);
            for (const auto& peer_id : order) {
                auto queue = outbound_queues.find(peer_id);
                if (queue == outbound_queues.end() ||
                    (queue->second.raft.empty() && queue->second.gossip.empty()) ||
                    in_flight_peers.contains(peer_id)) continue;
                RaftPeerEndpoint endpoint;
                {
                    std::lock_guard endpoint_lock(endpoint_mutex);
                    const auto found = raft_endpoints.find(peer_id);
                    if (found == raft_endpoints.end() || !found->second.enabled) continue;
                    endpoint = found->second;
                }

                const bool raft_waiting = !queue->second.raft.empty();
                const bool gossip_waiting = !queue->second.gossip.empty();
                auto lifecycle = peer_lifecycle.find(peer_id);
                if (lifecycle == peer_lifecycle.end()) continue;
                if (!lifecycle->second.can_attempt(now)) {
                    const auto state = lifecycle->second.snapshot(now);
                    if (state.retry_delay_ms > 0) {
                        const auto retry = now + std::chrono::milliseconds(state.retry_delay_ms);
                        if (!earliest_retry || retry < *earliest_retry) earliest_retry = retry;
                    }
                    continue;
                }
                // Serve up to eight consecutive Raft messages before allowing
                // one gossip message for this peer. One peer-level circuit
                // breaker gates both classes, so Raft and gossip cannot probe
                // an unhealthy destination independently.
                constexpr std::size_t kMaximumRaftDispatchStreak = 8;
                const bool take_raft = raft_waiting &&
                    (!gossip_waiting || queue->second.consecutive_raft_dispatches < kMaximumRaftDispatchStreak);
                DispatchWork work;
                work.peer_id = peer_id;
                work.endpoint = std::move(endpoint);
                work.started_at = now;
                if (take_raft) {
                    work.item = std::move(queue->second.raft.front());
                    queue->second.raft.pop_front();
                    ++queue->second.consecutive_raft_dispatches;
                } else {
                    work.item = std::move(queue->second.gossip.front());
                    queue->second.gossip.pop_front();
                    queue->second.consecutive_raft_dispatches = 0;
                }
                // The scheduler and lifecycle map are protected by the same
                // mutex; begin_attempt must succeed after the eligibility test.
                if (!lifecycle->second.begin_attempt(now)) {
                    if (take_raft) queue->second.raft.push_front(std::move(work.item));
                    else queue->second.gossip.push_front(std::move(work.item));
                    continue;
                }
                // Counts and bytes include in-flight work. This reserves the
                // capacity needed to requeue a failed send without losing it.
                in_flight_peers.insert(peer_id);
                scheduler_cursor = peer_id;
                ++peer_counters[peer_id].attempts;
                return work;
            }
            if (earliest_retry) outbound_condition.wait_until(lock, *earliest_retry);
            else outbound_condition.wait(lock);
        }
    }

    void dispatcher_loop() noexcept {
        for (;;) {
            std::optional<DispatchWork> work;
            {
                std::unique_lock lock(outbound_mutex);
                work = take_dispatch_work(lock);
            }
            if (!work) return;
            bool succeeded = false;
            bool retryable_failure = true;
            std::string failure;
            try {
                if (!authentication.has_value()) throw std::logic_error("Raft outbound dispatcher has no peer authentication");
                PeerAuthConfig client_auth;
                client_auth.identity = authentication->identity;
                {
                    std::lock_guard trust_lock(trust_store_mutex);
                    client_auth.trusted_peers = authentication->trusted_peers;
                }
                client_auth.expected_peer_id = work->peer_id;
                PeerClient peer(work->endpoint.host, work->endpoint.port,
                                std::move(client_auth), raft_configuration->outbound_timeout);
                if (work->item.kind == OutboundMessage::Kind::Raft) {
                    peer.send_raft_message(chain_parameters, work->item.raft_message);
                    succeeded = true;
                } else {
                    const auto transaction = ledger::Transaction::deserialize(work->item.transaction_payload);
                    if (!peer.send_gossip_transaction(chain_parameters, transaction, &failure)) {
                        retryable_failure = false;
                    } else {
                        succeeded = true;
                    }
                }
            } catch (const std::exception& exception) {
                failure = exception.what();
            } catch (...) {
                failure = "unknown outbound peer failure";
            }

            {
                std::lock_guard lock(outbound_mutex);
                in_flight_peers.erase(work->peer_id);
                auto& counters = peer_counters[work->peer_id];
                bool endpoint_enabled = false;
                {
                    std::lock_guard endpoint_lock(endpoint_mutex);
                    const auto endpoint = raft_endpoints.find(work->peer_id);
                    endpoint_enabled = endpoint != raft_endpoints.end() && endpoint->second.enabled;
                }

                const auto completed_at = std::chrono::steady_clock::now();
                const auto latency_us = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
                    std::chrono::duration_cast<std::chrono::microseconds>(completed_at - work->started_at).count()));
                auto lifecycle = peer_lifecycle.find(work->peer_id);
                if (lifecycle == peer_lifecycle.end()) {
                    // The roster is immutable for the lifetime of the node;
                    // this is defensive and should never occur.
                    outbound_stopping = true;
                    continue;
                }

                if (succeeded) {
                    ++counters.successes;
                    lifecycle->second.record_success(completed_at, unix_time_ms(), latency_us);
                    const auto state = lifecycle->second.snapshot(completed_at);
                    counters.consecutive_failures = state.consecutive_failures;
                    counters.last_success_unix_ms = state.last_success_unix_ms;
                    counters.last_error.clear();
                    outbound_bytes -= work->item.encoded_size;
                    --outbound_count;
                    if (work->item.kind == OutboundMessage::Kind::Transaction) {
                        outbound_gossip_bytes -= work->item.encoded_size;
                        --outbound_gossip_count;
                        pending_gossip_ids[work->peer_id].erase(work->item.transaction_id);
                        mark_gossip_delivered(work->peer_id, work->item.transaction_id);
                    }
                } else if (!retryable_failure) {
                    // The remote endpoint completed an authenticated request
                    // and returned a terminal application rejection. The
                    // transport is reachable, so close the circuit while
                    // counting/dropping the rejected message separately.
                    ++counters.successes;
                    lifecycle->second.record_success(completed_at, unix_time_ms(), latency_us);
                    counters.consecutive_failures = 0;
                    counters.last_success_unix_ms = unix_time_ms();
                    counters.last_error = failure.substr(0, 256);
                    outbound_bytes -= work->item.encoded_size;
                    --outbound_count;
                    ++counters.dropped;
                    ++outbound_dropped;
                    if (work->item.kind == OutboundMessage::Kind::Transaction) {
                        outbound_gossip_bytes -= work->item.encoded_size;
                        --outbound_gossip_count;
                        pending_gossip_ids[work->peer_id].erase(work->item.transaction_id);
                        mark_gossip_delivered(work->peer_id, work->item.transaction_id);
                    }
                } else {
                    ++counters.failures;
                    (void)lifecycle->second.record_failure(completed_at, unix_time_ms());
                    counters.consecutive_failures = lifecycle->second.snapshot(completed_at).consecutive_failures;
                    counters.last_failure_unix_ms = unix_time_ms();
                    counters.last_error = failure.substr(0, 256);
                    if (outbound_stopping || !endpoint_enabled) {
                        outbound_bytes -= work->item.encoded_size;
                        --outbound_count;
                        ++counters.dropped;
                        ++outbound_dropped;
                        if (work->item.kind == OutboundMessage::Kind::Transaction) {
                            outbound_gossip_bytes -= work->item.encoded_size;
                            --outbound_gossip_count;
                            pending_gossip_ids[work->peer_id].erase(work->item.transaction_id);
                        }
                    } else {
                        auto& queue = outbound_queues[work->peer_id];
                        if (work->item.kind == OutboundMessage::Kind::Raft) {
                            queue.raft.push_front(std::move(work->item));
                        } else {
                            queue.gossip.push_front(std::move(work->item));
                        }
                    }
                }
            }
            outbound_condition.notify_all();
        }
    }

    RaftPeerHealthReport peer_health_report() {
        std::lock_guard queue_lock(outbound_mutex);
        std::lock_guard endpoint_lock(endpoint_mutex);
        RaftPeerHealthReport result;
        result.endpoint_registry_generation = endpoint_registry_generation;
        result.outbound_dropped_total = outbound_dropped;
        result.worker_count = raft_configuration
            ? std::min(raft_configuration->outbound_worker_count, raft_endpoints.size()) : 0U;
        result.peers.reserve(raft_endpoints.size());
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [peer_id, endpoint] : raft_endpoints) {
            RaftPeerHealth peer;
            peer.peer_id = peer_id;
            peer.host = endpoint.host;
            peer.port = endpoint.port;
            peer.enabled = endpoint.enabled;
            const auto lifecycle = peer_lifecycle.find(peer_id);
            PeerLifecycleSnapshot lifecycle_state;
            if (lifecycle != peer_lifecycle.end()) {
                lifecycle_state = lifecycle->second.snapshot(now);
                peer.lifecycle_state = lifecycle_state.state;
                peer.consecutive_failures = lifecycle_state.consecutive_failures;
                peer.last_success_unix_ms = lifecycle_state.last_success_unix_ms;
                peer.last_failure_unix_ms = lifecycle_state.last_failure_unix_ms;
                peer.retry_delay_ms = lifecycle_state.retry_delay_ms;
            }
            peer.in_flight = in_flight_peers.contains(peer_id);
            const auto queue = outbound_queues.find(peer_id);
            peer.queued_messages = queue == outbound_queues.end() ? 0U :
                queue->second.raft.size() + queue->second.gossip.size() +
                (in_flight_peers.contains(peer_id) ? 1U : 0U);
            const auto counter = peer_counters.find(peer_id);
            if (counter != peer_counters.end()) {
                peer.attempts = counter->second.attempts;
                peer.successes = counter->second.successes;
                peer.failures = counter->second.failures;
                peer.dropped = counter->second.dropped;
                peer.last_error = counter->second.last_error;
            }
            result.peers.push_back(std::move(peer));
        }
        return result;
    }

    RaftPeerEndpointUpdateResult update_peer_endpoint(const std::string& peer_id,
                                                       const std::string& host,
                                                       std::uint16_t port,
                                                       bool enabled) {
        if (peer_id.empty() || peer_id == local_peer_id || !raft_members.contains(peer_id)) {
            throw std::invalid_argument("Raft endpoint update target must be a remote pre-provisioned cluster member");
        }
        if (!valid_endpoint_host(host) || port == 0) throw std::invalid_argument("Raft endpoint host or port is invalid");
        std::lock_guard queue_lock(outbound_mutex);
        std::lock_guard endpoint_lock(endpoint_mutex);
        auto found = raft_endpoints.find(peer_id);
        if (found == raft_endpoints.end()) throw std::invalid_argument("Raft endpoint update target is not configured");
        if (!enabled && raft_adapter && raft_adapter->node().membership().is_voter(peer_id)) {
            throw std::runtime_error("cannot disable transport for an active Raft voter; first demote it through joint consensus");
        }
        if (found->second.host == host && found->second.port == port && found->second.enabled == enabled) {
            return {endpoint_registry_generation, peer_id, host, port, enabled};
        }
        if (endpoint_registry_generation == UINT64_MAX) throw std::overflow_error("peer endpoint registry generation exhausted");
        auto candidate = raft_endpoints;
        candidate.at(peer_id).host = host;
        candidate.at(peer_id).port = port;
        candidate.at(peer_id).enabled = enabled;
        const std::uint64_t next_generation = endpoint_registry_generation + 1U;
        save_endpoint_registry_atomic(endpoint_registry_path,
            raft_configuration->consensus.cluster_id, next_generation, candidate);
        raft_endpoints = std::move(candidate);
        endpoint_registry_generation = next_generation;
        peer_lifecycle.at(peer_id).set_enabled(enabled);
        if (!enabled) {
            auto queue = outbound_queues.find(peer_id);
            if (queue != outbound_queues.end()) {
                const auto removed_raft = queue->second.raft.size();
                const auto removed_gossip = queue->second.gossip.size();
                for (const auto& item : queue->second.raft) outbound_bytes -= item.encoded_size;
                for (const auto& item : queue->second.gossip) {
                    outbound_bytes -= item.encoded_size;
                    outbound_gossip_bytes -= item.encoded_size;
                    pending_gossip_ids[peer_id].erase(item.transaction_id);
                }
                outbound_count -= removed_raft + removed_gossip;
                outbound_gossip_count -= removed_gossip;
                queue->second.raft.clear();
                queue->second.gossip.clear();
                peer_counters[peer_id].dropped += removed_raft + removed_gossip;
                outbound_dropped += removed_raft + removed_gossip;
            }
        } else {
            peer_counters[peer_id].consecutive_failures = 0;
            peer_counters[peer_id].last_error.clear();
        }
        outbound_condition.notify_all();
        const auto& updated = raft_endpoints.at(peer_id);
        return {endpoint_registry_generation, peer_id, updated.host, updated.port, updated.enabled};
    }

    ledger::Ledger& ledger;
    std::chrono::milliseconds timeout;
    std::optional<PeerAuthConfig> authentication;
    std::optional<RaftNetworkConfig> raft_configuration;
    InboundAdmissionConfig inbound_admission;
    std::mutex admission_mutex;
    // Network I/O is allowed to overlap, but every operation touching Ledger,
    // Raft adapter, and the mutable trust policy is serialized here.
    std::mutex state_mutex;
    double global_inbound_tokens{0.0};
    std::chrono::steady_clock::time_point global_inbound_updated{std::chrono::steady_clock::now()};
    std::map<std::string, InboundTokenBucket> inbound_address_buckets;
    std::mutex trust_store_mutex;
    std::unique_ptr<consensus::RaftLedgerAdapter> raft_adapter;
    std::set<std::string> raft_members;
    std::map<std::string, RaftPeerEndpoint> raft_endpoints;
    std::string endpoint_registry_path;
    std::uint64_t endpoint_registry_generation{0};
    std::mutex endpoint_mutex;
    std::string local_peer_id;
    PeerChainParameters chain_parameters{};
    std::mutex outbound_mutex;
    std::condition_variable outbound_condition;
    std::map<std::string, PeerQueues> outbound_queues;
    std::map<std::string, std::set<std::string>> pending_gossip_ids;
    std::map<std::string, std::set<std::string>> delivered_gossip_ids;
    std::map<std::string, std::deque<std::string>> delivered_gossip_order;
    std::chrono::steady_clock::time_point last_gossip_scan{};
    std::map<std::string, PeerCounters> peer_counters;
    std::map<std::string, PeerLifecycle> peer_lifecycle;
    std::set<std::string> in_flight_peers;
    std::string scheduler_cursor;
    std::size_t outbound_count{0};
    std::size_t outbound_bytes{0};
    std::size_t outbound_gossip_count{0};
    std::size_t outbound_gossip_bytes{0};
    std::uint64_t outbound_dropped{0};
    bool outbound_stopping{false};
    bool startup_gossip_enqueued{false};
    std::mutex dispatcher_lifecycle_mutex;
    std::vector<std::thread> dispatchers;
    boost::asio::io_context io;
    tcp::acceptor acceptor;
    std::mutex inbound_lifecycle_mutex;
    std::mutex inbound_queue_mutex;
    std::condition_variable inbound_condition;
    std::deque<InboundConnection> inbound_queue;
    std::vector<std::thread> inbound_workers;
    bool inbound_stopping{false};
    std::uint16_t bound_port{0};
    enum class RunState : std::uint8_t { Created, Running, StopRequested, Stopped };
    std::atomic<RunState> run_state{RunState::Created};
};
NodeServer::NodeServer(ledger::Ledger& ledger, const std::string& bind_address,
                       std::uint16_t port, std::chrono::milliseconds timeout,
                       InboundAdmissionConfig admission)
    : impl_(std::make_unique<Impl>(ledger, bind_address, port, timeout, std::nullopt,
                                   std::nullopt, std::move(admission))) {}

NodeServer::NodeServer(ledger::Ledger& ledger, const std::string& bind_address,
                       std::uint16_t port, PeerAuthConfig authentication,
                       std::chrono::milliseconds timeout, InboundAdmissionConfig admission)
    : impl_(std::make_unique<Impl>(ledger, bind_address, port, timeout,
                                   std::move(authentication), std::nullopt, std::move(admission))) {
    if (!impl_->authentication->identity) throw std::invalid_argument("authenticated node server requires a node identity");
    if (impl_->authentication->trusted_peers.size() == 0) {
        throw std::invalid_argument("authenticated node server requires at least one trusted peer");
    }
}

NodeServer::NodeServer(ledger::Ledger& ledger, const std::string& bind_address,
                       std::uint16_t port, PeerAuthConfig authentication,
                       RaftNetworkConfig raft_configuration,
                       std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(ledger, bind_address, port, timeout,
                                   std::move(authentication),
                                   std::move(raft_configuration))) {
    if (!impl_->authentication->identity) {
        throw std::invalid_argument("Raft-enabled node server requires a node identity");
    }
    impl_->start_dispatcher();
}

NodeServer::~NodeServer() { stop(); }

std::uint16_t NodeServer::port() const noexcept { return impl_->bound_port; }

void NodeServer::serve_one() {
    impl_->start_dispatcher();
    impl_->start_inbound_workers();
    {
        std::lock_guard state_lock(impl_->state_mutex);
        impl_->pump_pending_gossip_if_due();
    }
    // Accept on the listener's io_context first. Rebind the native descriptor
    // into a per-connection context only after accept completes; accepting
    // directly into a socket owned by another io_context is not portable.
    tcp::socket accepted_socket(impl_->io);
    ErrorCode error;
    impl_->acceptor.accept(accepted_socket, error);
    if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) return;
    if (error) throw boost::system::system_error(error, "accept peer connection");
    if (!impl_->admit_connection(accepted_socket)) {
        ErrorCode ignored;
        accepted_socket.shutdown(tcp::socket::shutdown_both, ignored);
        accepted_socket.close(ignored);
        return;
    }
    const auto protocol = accepted_socket.local_endpoint(error).protocol();
    if (error) {
        accepted_socket.close(error);
        return;
    }
    const auto native_socket = accepted_socket.release(error);
    if (error) throw boost::system::system_error(error, "release accepted peer socket");
    if (!impl_->enqueue_inbound_socket(Impl::InboundConnection{native_socket, protocol})) {
        // The bounded queue is deliberately fail-fast. A peer must retry
        // rather than allowing inbound sockets to accumulate without limit.
        // Admission tokens are not refunded: repeated saturation is load.
        return;
    }
}

void NodeServer::run(const volatile std::sig_atomic_t* external_stop) {
    using RunState = Impl::RunState;
    RunState expected = RunState::Created;
    if (!impl_->run_state.compare_exchange_strong(
            expected, RunState::Running, std::memory_order_acq_rel, std::memory_order_acquire)) {
        if (expected == RunState::StopRequested || expected == RunState::Stopped) return;
        throw std::logic_error("NodeServer::run may only be entered once and may not run concurrently");
    }

    // stop() may race immediately after the Created -> Running transition.
    // Never clear a stop request here: doing so loses stop-before-run requests.
    if (impl_->run_state.load(std::memory_order_acquire) != RunState::Running) {
        impl_->stop_dispatcher();
        impl_->run_state.store(RunState::Stopped, std::memory_order_release);
        return;
    }

    impl_->start_dispatcher();
    auto previous_tick = std::chrono::steady_clock::now();
    try {
        while (impl_->run_state.load(std::memory_order_acquire) == RunState::Running &&
               (external_stop == nullptr || *external_stop == 0)) {
            if (impl_->raft_adapter) {
                const auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - previous_tick);
                if (elapsed <= std::chrono::milliseconds::zero()) elapsed = std::chrono::milliseconds(1);
                previous_tick = now;
                std::lock_guard state_lock(impl_->state_mutex);
                impl_->enqueue_raft_messages(impl_->raft_adapter->tick(elapsed));
            }
            serve_one();
            if (impl_->run_state.load(std::memory_order_acquire) == RunState::Running &&
                (external_stop == nullptr || *external_stop == 0)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    } catch (...) {
        impl_->run_state.store(RunState::Stopped, std::memory_order_release);
        impl_->stop_inbound_workers();
        impl_->stop_dispatcher();
        throw;
    }
    impl_->run_state.store(RunState::Stopped, std::memory_order_release);
    impl_->stop_inbound_workers();
    impl_->stop_dispatcher();
}

void NodeServer::stop() noexcept {
    if (!impl_) return;
    auto state = impl_->run_state.load(std::memory_order_acquire);
    while (state == Impl::RunState::Created || state == Impl::RunState::Running) {
        if (impl_->run_state.compare_exchange_weak(
                state, Impl::RunState::StopRequested,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
    impl_->stop_inbound_workers();
    impl_->stop_dispatcher();
}

} // namespace feinstat::network
