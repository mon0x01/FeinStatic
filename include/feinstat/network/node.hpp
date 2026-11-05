#pragma once

#include "feinstat/consensus/raft_ledger_adapter.hpp"
#include "feinstat/ledger/ledger.hpp"
#include "feinstat/network/peer_identity.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <memory>
#include <vector>
#include <optional>
#include <string>

namespace feinstat::network {

inline constexpr std::uint32_t kProtocolVersion = 8;
inline constexpr std::size_t kMaximumFrameBytes = 16U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumBlocksPerBatch = 32;

struct PeerStatus {
    std::uint64_t height{0};
    Hash tip_hash{};
    std::size_t pending_transactions{0};
};

struct PeerAuthConfig {
    std::shared_ptr<const NodeIdentity> identity;
    PeerTrustStore trusted_peers;
    // Required by clients to prevent connecting to an unexpected trusted peer.
    // Servers leave this empty and authorize clients through trusted_peers.
    std::string expected_peer_id;
};

// Immutable chain identity copied before background peer dispatch starts.
// It allows outbound Raft RPC workers to perform the normal authenticated
// handshake without reading a Ledger that the server thread may be updating.
struct PeerChainParameters {
    Hash genesis_hash{};
    std::uint32_t difficulty{0};
    Amount block_reward{0};
};

struct RaftPeerEndpoint {
    std::string peer_id;
    std::string host;
    std::uint16_t port{0};
    bool enabled{true};
};

struct RaftPeerHealth {
    std::string peer_id;
    std::string host;
    std::uint16_t port{0};
    bool enabled{true};
    bool in_flight{false};
    std::size_t queued_messages{0};
    std::uint64_t attempts{0};
    std::uint64_t successes{0};
    std::uint64_t failures{0};
    std::uint64_t dropped{0};
    std::uint32_t consecutive_failures{0};
    std::uint64_t last_success_unix_ms{0};
    std::uint64_t last_failure_unix_ms{0};
    std::uint64_t retry_delay_ms{0};
    std::string last_error;
};

struct RaftPeerHealthReport {
    std::uint64_t endpoint_registry_generation{0};
    std::uint64_t outbound_dropped_total{0};
    std::size_t worker_count{0};
    std::vector<RaftPeerHealth> peers;
};

struct RaftPeerEndpointUpdateResult {
    std::uint64_t endpoint_registry_generation{0};
    std::string peer_id;
    std::string host;
    std::uint16_t port{0};
    bool enabled{true};
};

// Token-bucket admission policy applied before the peer handshake is parsed.
// The defaults tolerate a busy nine-member local cluster; operators can lower
// them for internet-facing deployments.
struct InboundAdmissionConfig {
    double global_refill_per_second{2048.0};
    std::size_t global_burst{8192};
    double per_address_refill_per_second{512.0};
    std::size_t per_address_burst{2048};
    std::size_t maximum_tracked_addresses{4096};
    std::chrono::seconds address_idle_expiry{300};
};

struct RaftNetworkConfig {
    std::string database_path;
    consensus::RaftConfig consensus;
    std::vector<RaftPeerEndpoint> peers; // Remote members only; local node is implicit.
    std::chrono::milliseconds outbound_timeout{250};
    std::string trust_store_path;
    // Defaults to database_path + ".endpoints". The registry is a checksummed,
    // owner-only file updated atomically by authenticated operators.
    std::string endpoint_registry_path;
    std::size_t outbound_worker_count{4};
    std::size_t maximum_queued_messages_per_peer{128};
    // Best-effort transaction gossip is bounded separately so it cannot consume
    // the capacity reserved for Raft control traffic.
    std::size_t maximum_queued_gossip_messages_per_peer{64};
    InboundAdmissionConfig inbound_admission{};
};

struct RaftPeerStatus {
    std::string node_id;
    std::string leader_id;
    consensus::RaftRole role{consensus::RaftRole::Follower};
    std::uint64_t current_term{0};
    std::uint64_t commit_index{0};
    std::uint64_t last_applied{0};
    std::uint64_t last_log_index{0};
    std::uint64_t ledger_height{0};
    Hash ledger_tip_hash{};
    consensus::MembershipPhase membership_phase{consensus::MembershipPhase::Stable};
    std::vector<std::string> voters;
    std::vector<std::string> learners;
    std::vector<std::string> joint_voters;
};

struct RaftBlockProposalResult {
    std::uint64_t index{0};
    std::uint64_t term{0};
    Hash block_hash{};
};

struct TrustPolicyApplyResult { std::uint64_t policy_epoch{0}; std::size_t active_peers{0}; std::size_t total_entries{0}; };

struct RaftMembershipProposalResult {
    std::uint64_t commit_index{0};
    std::uint64_t log_index{0};
    std::uint64_t term{0};
};

struct SyncResult {
    std::size_t blocks_received{0};
    std::uint64_t final_height{0};
    std::uint64_t peer_height{0};
    std::uint64_t common_ancestor_height{0};
    std::size_t requeued_transactions{0};
    std::size_t dropped_detached_transactions{0};
    bool already_current{false};
    bool chain_adopted{false};
    bool chain_reorganized{false};
    bool local_chain_preferred{false};
};

// A synchronous, one-request-per-connection client. Every connection performs
// a genesis/consensus-parameter handshake before any state-changing request.
class PeerClient {
public:
    PeerClient(std::string host,
               std::uint16_t port,
               std::chrono::milliseconds timeout = std::chrono::seconds(5));
    PeerClient(std::string host,
               std::uint16_t port,
               PeerAuthConfig authentication,
               std::chrono::milliseconds timeout = std::chrono::seconds(5));

    [[nodiscard]] PeerStatus status(const ledger::Ledger& local_ledger) const;
    [[nodiscard]] bool submit_transaction(const ledger::Ledger& local_ledger,
                                          const ledger::Transaction& transaction,
                                          std::string* error = nullptr) const;
    void submit_block(const ledger::Ledger& local_ledger,
                      const ledger::Block& block) const;
    // protected_transactions are durable external Ledger references. The
    // CLI obtains them from FinancialPlatform so Bridge history cannot be
    // orphaned or moved by a peer-triggered reorganization.
    [[nodiscard]] SyncResult synchronize(
        ledger::Ledger& local_ledger,
        std::size_t maximum_batches = 4096,
        const std::vector<std::pair<std::string, std::uint64_t>>& protected_transactions = {}) const;
    [[nodiscard]] RaftPeerStatus raft_status(const ledger::Ledger& local_ledger) const;
    [[nodiscard]] RaftBlockProposalResult propose_raft_block(
        const ledger::Ledger& local_ledger, const Address& miner) const;
    // Transport worker entry point. The parameters are immutable and may be
    // safely shared while the server's Ledger is applying committed entries.
    [[nodiscard]] RaftMembershipProposalResult change_raft_membership(
        const ledger::Ledger& local_ledger, const std::vector<std::string>& new_members) const;
    [[nodiscard]] TrustPolicyApplyResult push_trust_policy(
        const ledger::Ledger& local_ledger, const Bytes& signed_policy_bundle) const;
    [[nodiscard]] RaftPeerHealthReport raft_peer_health(const ledger::Ledger& local_ledger) const;
    [[nodiscard]] RaftPeerEndpointUpdateResult update_raft_peer_endpoint(
        const ledger::Ledger& local_ledger, const std::string& peer_id,
        const std::string& host, std::uint16_t port, bool enabled = true) const;
    void send_raft_message(const PeerChainParameters& parameters,
                           const consensus::RaftMessage& message) const;
    // Internal-style transport entry point used by the bounded peer dispatcher.
    // A duplicate is a successful gossip delivery; invalid transactions are not.
    [[nodiscard]] bool send_gossip_transaction(const PeerChainParameters& parameters,
                                               const ledger::Transaction& transaction,
                                               std::string* error = nullptr) const;

private:
    std::string host_;
    std::uint16_t port_;
    std::chrono::milliseconds timeout_;
    std::shared_ptr<const NodeIdentity> identity_;
    PeerTrustStore trusted_peers_;
    std::string expected_peer_id_;
    PeerAuthConfig auth_config_;
};

// Single-threaded server: one connection is processed at a time, keeping the
// existing Ledger API's non-concurrent mutation model explicit and safe.
class NodeServer {
public:
    NodeServer(ledger::Ledger& ledger,
               const std::string& bind_address,
               std::uint16_t port,
               std::chrono::milliseconds timeout = std::chrono::seconds(5),
               InboundAdmissionConfig admission = {});
    NodeServer(ledger::Ledger& ledger,
               const std::string& bind_address,
               std::uint16_t port,
               PeerAuthConfig authentication,
               std::chrono::milliseconds timeout = std::chrono::seconds(5),
               InboundAdmissionConfig admission = {});
    NodeServer(ledger::Ledger& ledger,
               const std::string& bind_address,
               std::uint16_t port,
               PeerAuthConfig authentication,
               RaftNetworkConfig raft_configuration,
               std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
    ~NodeServer();

    NodeServer(const NodeServer&) = delete;
    NodeServer& operator=(const NodeServer&) = delete;
    NodeServer(NodeServer&&) = delete;
    NodeServer& operator=(NodeServer&&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;
    void serve_one();
    void run(const volatile std::sig_atomic_t* external_stop = nullptr);
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace feinstat::network
