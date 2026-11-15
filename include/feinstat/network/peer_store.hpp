#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"
#include "feinstat/network/identity.hpp"
#include "feinstat/network/node_record.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace feinstat::network {

// Discovery source tags for tracking how a peer record was learned.
enum class DiscoverySource : std::uint8_t {
    Unknown = 0,
    Bootstrap = 1,
    PeerExchange = 2,
    Cached = 3,
    Configured = 4,
};

// Connection/verification state for a peer.
struct PeerConnectionState {
    bool connected{false};
    bool verified{false};
    std::uint64_t connection_attempts{0};
    std::uint64_t connection_successes{0};
    std::uint64_t connection_failures{0};
    std::uint64_t last_success_unix_ms{0};
    std::uint64_t last_failure_unix_ms{0};
};

// Full peer store entry. The NodeId is the primary key.
struct PeerEntry {
    NodeId node_id;
    SignedNodeRecord record;
    DiscoverySource discovery_source{DiscoverySource::Unknown};
    std::uint64_t first_seen_unix_ms{0};
    std::uint64_t last_seen_unix_ms{0};
    std::uint64_t last_verified_unix_ms{0};
    PeerConnectionState connection_state;
};

// Result of an upsert operation.
enum class UpsertResult {
    Inserted,       // New peer added
    Updated,        // Existing peer updated with a higher-sequence record
    Unchanged,      // Record identical to existing (duplicate)
    RejectedStale,  // Sequence not greater than existing (rollback attempt)
    RejectedConflict, // Same node ID but different public key (identity conflict)
    RejectedStoreFull, // Bounded storage limit reached
};

// Persistent peer store with SQLite-backed durability.
//
// Node identity is the primary peer key. Validated signed records are stored
// with monotonic sequence tracking. Unvalidated records never overwrite verified
// records. Peer updates preserve monotonic record sequence rules and prevent
// rollback. Storage is bounded; admission rejects entries beyond the limit.
class PeerStore {
public:
    struct Config {
        std::size_t max_peers{4096};
        std::uint64_t stale_record_ttl_ms{7ULL * 24ULL * 60ULL * 60ULL * 1000ULL}; // 7 days
        std::uint64_t expired_record_ttl_ms{24ULL * 60ULL * 60ULL * 1000ULL};     // 24 hours
    };

    explicit PeerStore(Config config);
    ~PeerStore();

    PeerStore(const PeerStore&) = delete;
    PeerStore& operator=(const PeerStore&) = delete;
    PeerStore(PeerStore&&) noexcept;
    PeerStore& operator=(PeerStore&&) noexcept;

    // Open or create the SQLite-backed peer store at the given path.
    void open(const std::string& path);

    // Upsert a validated record. Monotonic sequence is enforced: a record with
    // sequence <= the stored sequence for the same node ID is rejected as stale.
    // A record with the same node ID but a different public key is rejected as a
    // conflict. Unvalidated records never overwrite verified records.
    UpsertResult upsert(const SignedNodeRecord& record, DiscoverySource source,
                        std::uint64_t now_unix_ms);

    // Look up a peer by node ID.
    [[nodiscard]] std::optional<PeerEntry> get(const NodeId& node_id) const noexcept;

    // List all peers (optionally filtered by verification state).
    [[nodiscard]] std::vector<PeerEntry> list(bool verified_only = false) const;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t max_peers() const noexcept { return config_.max_peers; }

    // Mark connection state for a peer.
    void mark_connected(const NodeId& node_id, bool connected) noexcept;
    void record_connection_attempt(const NodeId& node_id, bool success,
                                    std::uint64_t now_unix_ms) noexcept;

    // Touch last-seen time.
    void touch(const NodeId& node_id, std::uint64_t now_unix_ms) noexcept;

    // Mark a peer as verified (record cryptographically validated).
    void mark_verified(const NodeId& node_id, std::uint64_t now_unix_ms) noexcept;

    // Remove records that have expired (past expiry_unix_ms) or are stale
    // (not seen within the stale TTL). Returns the number of removed entries.
    std::size_t cleanup_expired(std::uint64_t now_unix_ms);

    // Persist the in-memory store to SQLite (called automatically on upsert when
    // durable persistence is enabled). Explicit save flushes pending writes.
    void save();

    // Reload from disk, discarding in-memory changes.
    void reload();

private:
    struct PeerEntryImpl {
        PeerEntry entry;
        bool dirty{false};
    };

    void ensure_schema();
    void persist_entry(const PeerEntryImpl& impl);
    void load_all();
    static std::string discovery_source_tag(DiscoverySource source);
    static DiscoverySource parse_discovery_source(const std::string& tag);

    Config config_;
    std::string path_;
    sqlite3* db_{nullptr};
    bool opened_{false};
    std::map<NodeId, PeerEntryImpl> peers_;
    std::size_t size_{0};
};

} // namespace feinstat::network
