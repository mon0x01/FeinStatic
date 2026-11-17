#include "feinstat/network/peer_store.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace feinstat::network {
namespace {

void check_sqlite(int result, sqlite3* db, const char* operation) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
        const std::string detail = db != nullptr ? sqlite3_errmsg(db) : sqlite3_errstr(result);
        throw std::runtime_error(std::string("SQLite ") + operation + " failed: " + detail);
    }
}

void execute(sqlite3* db, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        const std::string detail = message != nullptr ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        throw std::runtime_error(std::string("SQLite execution failed: ") + detail);
    }
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        check_sqlite(sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr), db_, "prepare");
    }
    ~Statement() { if (statement_ != nullptr) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() noexcept { return statement_; }
    void step_done() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_DONE) check_sqlite(result, db_, "step");
    }

private:
    sqlite3* db_;
    sqlite3_stmt* statement_{nullptr};
};

void bind_text(sqlite3_stmt* statement, int index, const std::string& value, sqlite3* db) {
    if (value.size() > static_cast<std::size_t>(INT_MAX)) throw std::length_error("SQLite text value too large");
    check_sqlite(sqlite3_bind_text(statement, index, value.data(),
                                   static_cast<int>(value.size()), SQLITE_TRANSIENT), db, "bind text");
}

void bind_blob(sqlite3_stmt* statement, int index, const Byte* data,
               std::size_t size, sqlite3* db) {
    if (size > static_cast<std::size_t>(INT_MAX)) throw std::length_error("SQLite blob too large");
    check_sqlite(sqlite3_bind_blob(statement, index, data, static_cast<int>(size),
                                   SQLITE_TRANSIENT), db, "bind blob");
}

Bytes column_blob(sqlite3_stmt* statement, int column) {
    const auto* data = static_cast<const Byte*>(sqlite3_column_blob(statement, column));
    const int size = sqlite3_column_bytes(statement, column);
    constexpr int maximum_blob_size = 64 * 1024 * 1024;
    if (size < 0 || size > maximum_blob_size || (size > 0 && data == nullptr)) {
        throw std::runtime_error("invalid or oversized SQLite blob");
    }
    return data == nullptr ? Bytes{} : Bytes(data, data + size);
}

std::string column_text(sqlite3_stmt* statement, int column) {
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    const int size = sqlite3_column_bytes(statement, column);
    return data == nullptr ? std::string{} : std::string(data, static_cast<std::size_t>(size));
}

std::uint64_t column_u64(sqlite3_stmt* statement, int column) {
    return static_cast<std::uint64_t>(sqlite3_column_int64(statement, column));
}

int column_int(sqlite3_stmt* statement, int column) {
    return sqlite3_column_int(statement, column);
}

} // namespace

//------------------------------------------------------------------------------
// PeerStore
//------------------------------------------------------------------------------

PeerStore::PeerStore(Config config) : config_(std::move(config)) {}

PeerStore::~PeerStore() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

PeerStore::PeerStore(PeerStore&& other) noexcept
    : config_(std::move(other.config_)),
      path_(std::move(other.path_)),
      db_(other.db_),
      opened_(other.opened_),
      peers_(std::move(other.peers_)),
      size_(other.size_) {
    other.db_ = nullptr;
    other.opened_ = false;
    other.size_ = 0;
}

PeerStore& PeerStore::operator=(PeerStore&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) sqlite3_close(db_);
        config_ = std::move(other.config_);
        path_ = std::move(other.path_);
        db_ = other.db_;
        opened_ = other.opened_;
        peers_ = std::move(other.peers_);
        size_ = other.size_;
        other.db_ = nullptr;
        other.opened_ = false;
        other.size_ = 0;
    }
    return *this;
}

void PeerStore::open(const std::string& path) {
    path_ = path;
    const int result = sqlite3_open_v2(path.c_str(), &db_,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                       nullptr);
    if (result != SQLITE_OK) {
        const std::string detail = db_ != nullptr ? sqlite3_errmsg(db_) : sqlite3_errstr(result);
        if (db_ != nullptr) { sqlite3_close(db_); db_ = nullptr; }
        throw std::runtime_error("unable to open peer store database: " + detail);
    }
    sqlite3_busy_timeout(db_, 5000);
    execute(db_, "PRAGMA foreign_keys = ON;");
    execute(db_, "PRAGMA journal_mode = WAL;");
    execute(db_, "PRAGMA synchronous = FULL;");
    opened_ = true;
    ensure_schema();
    load_all();
}

void PeerStore::ensure_schema() {
    execute(db_,
        "CREATE TABLE IF NOT EXISTS peers ("
        "node_id TEXT PRIMARY KEY,"
        "public_key BLOB NOT NULL,"
        "sequence INTEGER NOT NULL,"
        "record BLOB NOT NULL,"
        "discovery_source TEXT NOT NULL,"
        "first_seen_unix_ms INTEGER NOT NULL,"
        "last_seen_unix_ms INTEGER NOT NULL,"
        "last_verified_unix_ms INTEGER NOT NULL,"
        "connection_attempts INTEGER NOT NULL DEFAULT 0,"
        "connection_successes INTEGER NOT NULL DEFAULT 0,"
        "connection_failures INTEGER NOT NULL DEFAULT 0,"
        "last_success_unix_ms INTEGER NOT NULL DEFAULT 0,"
        "last_failure_unix_ms INTEGER NOT NULL DEFAULT 0,"
        "verified INTEGER NOT NULL DEFAULT 0,"
        "connected INTEGER NOT NULL DEFAULT 0"
        ");");
    execute(db_,
        "CREATE INDEX IF NOT EXISTS idx_peers_last_seen ON peers(last_seen_unix_ms);");
    execute(db_,
        "CREATE INDEX IF NOT EXISTS idx_peers_verified ON peers(verified);");
}

void PeerStore::load_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.clear();
    size_ = 0;
    Statement stmt(db_,
        "SELECT node_id, public_key, sequence, record, discovery_source, "
        "first_seen_unix_ms, last_seen_unix_ms, last_verified_unix_ms, "
        "connection_attempts, connection_successes, connection_failures, "
        "last_success_unix_ms, last_failure_unix_ms, verified, connected "
        "FROM peers;");
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const std::string node_id_str = column_text(stmt.get(), 0);
        const Bytes public_key_bytes = column_blob(stmt.get(), 1);
        const Bytes record_blob = column_blob(stmt.get(), 3);
        const std::string source_tag = column_text(stmt.get(), 4);
        const std::uint64_t first_seen = column_u64(stmt.get(), 5);
        const std::uint64_t last_seen = column_u64(stmt.get(), 6);
        const std::uint64_t last_verified = column_u64(stmt.get(), 7);
        const std::uint64_t attempts = column_u64(stmt.get(), 8);
        const std::uint64_t successes = column_u64(stmt.get(), 9);
        const std::uint64_t failures = column_u64(stmt.get(), 10);
        const std::uint64_t last_success = column_u64(stmt.get(), 11);
        const std::uint64_t last_failure = column_u64(stmt.get(), 12);
        const int verified = column_int(stmt.get(), 13);
        const int connected = column_int(stmt.get(), 14);

        PublicKey public_key{};
        if (public_key_bytes.size() != 32) continue;
        std::copy(public_key_bytes.begin(), public_key_bytes.end(), public_key.begin());

        try {
            SignedNodeRecord record = SignedNodeRecord::parse(record_blob);
            NodeId node_id = NodeId::parse(node_id_str);
            if (!node_id.matches(public_key)) continue;

            PeerEntry entry{};
            entry.node_id = node_id;
            entry.record = record;
            entry.discovery_source = parse_discovery_source(source_tag);
            entry.first_seen_unix_ms = first_seen;
            entry.last_seen_unix_ms = last_seen;
            entry.last_verified_unix_ms = last_verified;
            entry.connection_state.connected = connected != 0;
            entry.connection_state.verified = verified != 0;
            entry.connection_state.connection_attempts = attempts;
            entry.connection_state.connection_successes = successes;
            entry.connection_state.connection_failures = failures;
            entry.connection_state.last_success_unix_ms = last_success;
            entry.connection_state.last_failure_unix_ms = last_failure;

            peers_.emplace(node_id, PeerEntryImpl{std::move(entry), false});
            ++size_;
        } catch (...) {
            // Skip malformed entries on load.
            continue;
        }
    }
}

std::string PeerStore::discovery_source_tag(DiscoverySource source) {
    switch (source) {
        case DiscoverySource::Bootstrap: return "bootstrap";
        case DiscoverySource::PeerExchange: return "peer_exchange";
        case DiscoverySource::Cached: return "cached";
        case DiscoverySource::Configured: return "configured";
        case DiscoverySource::Unknown:
        default: return "unknown";
    }
}

DiscoverySource PeerStore::parse_discovery_source(const std::string& tag) {
    if (tag == "bootstrap") return DiscoverySource::Bootstrap;
    if (tag == "peer_exchange") return DiscoverySource::PeerExchange;
    if (tag == "cached") return DiscoverySource::Cached;
    if (tag == "configured") return DiscoverySource::Configured;
    return DiscoverySource::Unknown;
}

UpsertResult PeerStore::upsert(const SignedNodeRecord& record, DiscoverySource source,
                                std::uint64_t now_unix_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) throw std::runtime_error("peer store is not open");

    const NodeId& node_id = record.node_id;

    // Check for existing entry.
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
        const PeerEntry& existing = it->second.entry;

        // Conflict: same node ID, different public key.
        if (existing.record.public_key != record.public_key) {
            return UpsertResult::RejectedConflict;
        }

        // Duplicate: identical record (same sequence and content).
        if (record.sequence == existing.record.sequence &&
            record.expiry_unix_ms == existing.record.expiry_unix_ms &&
            record.endpoints == existing.record.endpoints &&
            record.capabilities == existing.record.capabilities &&
            record.protocol_versions == existing.record.protocol_versions) {
            return UpsertResult::Unchanged;
        }

        // Stale: sequence not greater than existing (rollback attempt).
        if (record.sequence <= existing.record.sequence) {
            return UpsertResult::RejectedStale;
        }

        // Update with higher-sequence record.
        PeerEntry updated = existing;
        updated.record = record;
        updated.discovery_source = source;
        updated.last_seen_unix_ms = now_unix_ms;
        updated.last_verified_unix_ms = now_unix_ms;
        it->second.entry = std::move(updated);
        it->second.dirty = true;
        persist_entry(it->second);
        return UpsertResult::Updated;
    }

    // New entry: check bounded storage.
    if (size_ >= config_.max_peers) {
        return UpsertResult::RejectedStoreFull;
    }

    PeerEntry entry{};
    entry.node_id = node_id;
    entry.record = record;
    entry.discovery_source = source;
    entry.first_seen_unix_ms = now_unix_ms;
    entry.last_seen_unix_ms = now_unix_ms;
    entry.last_verified_unix_ms = now_unix_ms;

    peers_.emplace(node_id, PeerEntryImpl{std::move(entry), true});
    ++size_;
    persist_entry(peers_.find(node_id)->second);
    return UpsertResult::Inserted;
}

std::optional<PeerEntry> PeerStore::get(const NodeId& node_id) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) return std::nullopt;
    return it->second.entry;
}

std::vector<PeerEntry> PeerStore::list(bool verified_only) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PeerEntry> result;
    result.reserve(peers_.size());
    for (const auto& [id, impl] : peers_) {
        (void)id;
        if (verified_only && !impl.entry.connection_state.verified) continue;
        result.push_back(impl.entry);
    }
    return result;
}

void PeerStore::mark_connected(const NodeId& node_id, bool connected) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) return;
    it->second.entry.connection_state.connected = connected;
    it->second.dirty = true;
}

void PeerStore::record_connection_attempt(const NodeId& node_id, bool success,
                                           std::uint64_t now_unix_ms) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) return;
    auto& state = it->second.entry.connection_state;
    ++state.connection_attempts;
    if (success) {
        ++state.connection_successes;
        state.last_success_unix_ms = now_unix_ms;
    } else {
        ++state.connection_failures;
        state.last_failure_unix_ms = now_unix_ms;
    }
    it->second.dirty = true;
}

void PeerStore::touch(const NodeId& node_id, std::uint64_t now_unix_ms) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) return;
    it->second.entry.last_seen_unix_ms = now_unix_ms;
    it->second.dirty = true;
}

void PeerStore::mark_verified(const NodeId& node_id, std::uint64_t now_unix_ms) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(node_id);
    if (it == peers_.end()) return;
    it->second.entry.connection_state.verified = true;
    it->second.entry.last_verified_unix_ms = now_unix_ms;
    it->second.dirty = true;
}

std::size_t PeerStore::cleanup_expired(std::uint64_t now_unix_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t removed = 0;
    for (auto it = peers_.begin(); it != peers_.end();) {
        const PeerEntry& entry = it->second.entry;
        bool remove = false;

        // Remove records whose expiry has passed.
        if (entry.record.expiry_unix_ms != 0 && entry.record.expiry_unix_ms <= now_unix_ms) {
            remove = true;
        }
        // Remove stale records not seen within the TTL.
        else if (now_unix_ms > entry.last_seen_unix_ms &&
                 now_unix_ms - entry.last_seen_unix_ms > config_.stale_record_ttl_ms) {
            remove = true;
        }

        if (remove) {
            Statement stmt(db_, "DELETE FROM peers WHERE node_id = ?;");
            bind_text(stmt.get(), 1, entry.node_id.value(), db_);
            stmt.step_done();
            it = peers_.erase(it);
            --size_;
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void PeerStore::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) return;
    for (auto& [id, impl] : peers_) {
        (void)id;
        if (impl.dirty) {
            persist_entry(impl);
            impl.dirty = false;
        }
    }
}

void PeerStore::reload() {
    load_all();
}

void PeerStore::persist_entry(const PeerEntryImpl& impl) {
    const PeerEntry& entry = impl.entry;
    const Bytes record_bytes = entry.record.serialize();
    const std::uint64_t sequence = entry.record.sequence;

    Statement stmt(db_,
        "INSERT OR REPLACE INTO peers ("
        "node_id, public_key, sequence, record, discovery_source, "
        "first_seen_unix_ms, last_seen_unix_ms, last_verified_unix_ms, "
        "connection_attempts, connection_successes, connection_failures, "
        "last_success_unix_ms, last_failure_unix_ms, verified, connected"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    bind_text(stmt.get(), 1, entry.node_id.value(), db_);
    bind_blob(stmt.get(), 2, entry.record.public_key.data(),
              entry.record.public_key.size(), db_);
    check_sqlite(sqlite3_bind_int64(stmt.get(), 3,
                                     static_cast<sqlite3_int64>(sequence)), db_, "bind sequence");
    bind_blob(stmt.get(), 4, record_bytes.data(), record_bytes.size(), db_);
    bind_text(stmt.get(), 5, discovery_source_tag(entry.discovery_source), db_);
    check_sqlite(sqlite3_bind_int64(stmt.get(), 6,
                                     static_cast<sqlite3_int64>(entry.first_seen_unix_ms)), db_, "bind first_seen");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 7,
                                     static_cast<sqlite3_int64>(entry.last_seen_unix_ms)), db_, "bind last_seen");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 8,
                                     static_cast<sqlite3_int64>(entry.last_verified_unix_ms)), db_, "bind last_verified");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 9,
                                     static_cast<sqlite3_int64>(entry.connection_state.connection_attempts)), db_, "bind attempts");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 10,
                                     static_cast<sqlite3_int64>(entry.connection_state.connection_successes)), db_, "bind successes");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 11,
                                     static_cast<sqlite3_int64>(entry.connection_state.connection_failures)), db_, "bind failures");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 12,
                                     static_cast<sqlite3_int64>(entry.connection_state.last_success_unix_ms)), db_, "bind last_success");
    check_sqlite(sqlite3_bind_int64(stmt.get(), 13,
                                     static_cast<sqlite3_int64>(entry.connection_state.last_failure_unix_ms)), db_, "bind last_failure");
    check_sqlite(sqlite3_bind_int(stmt.get(), 14,
                                   entry.connection_state.verified ? 1 : 0), db_, "bind verified");
    check_sqlite(sqlite3_bind_int(stmt.get(), 15,
                                   entry.connection_state.connected ? 1 : 0), db_, "bind connected");
    stmt.step_done();
}

} // namespace feinstat::network
