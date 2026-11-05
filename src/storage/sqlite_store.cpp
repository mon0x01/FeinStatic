#include "feinstat/storage/sqlite_store.hpp"

#include "feinstat/ledger/mempool.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <climits>
#include <cerrno>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <sys/stat.h>

namespace feinstat::storage {
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
    check_sqlite(sqlite3_bind_blob(statement, index, data, static_cast<int>(size), SQLITE_TRANSIENT),
                 db, "bind blob");
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

template <std::size_t N>
std::array<Byte, N> fixed_column_blob(sqlite3_stmt* statement, int column) {
    const Bytes bytes = column_blob(statement, column);
    if (bytes.size() != N) throw std::runtime_error("unexpected fixed-size wallet field in database");
    std::array<Byte, N> result{};
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

class SqlTransaction {
public:
    explicit SqlTransaction(sqlite3* db) : db_(db) { execute(db_, "BEGIN IMMEDIATE"); }
    ~SqlTransaction() { if (!committed_) sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); }
    void commit() { execute(db_, "COMMIT"); committed_ = true; }
private:
    sqlite3* db_;
    bool committed_{false};
};

} // namespace

SqliteStore::SqliteStore(const std::string& path) : path_(path) {
    const int result = sqlite3_open_v2(path.c_str(), &db_,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                       nullptr);
    if (result != SQLITE_OK) {
        const std::string detail = db_ != nullptr ? sqlite3_errmsg(db_) : sqlite3_errstr(result);
        if (db_ != nullptr) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("unable to open SQLite database: " + detail);
    }
    try {
        sqlite3_busy_timeout(db_, 5000);
        execute(db_, "PRAGMA foreign_keys = ON;");
        execute(db_, "PRAGMA journal_mode = WAL;");
        execute(db_, "PRAGMA synchronous = FULL;");
        restrict_file_permissions();

        // Detect structurally damaged SQLite files before interpreting
        // application records. Logical block validity is checked separately
        // during replay.
        Statement integrity(db_, "PRAGMA quick_check(1);");
        const int integrity_step = sqlite3_step(integrity.get());
        check_sqlite(integrity_step, db_, "integrity check");
        const auto* integrity_text = reinterpret_cast<const char*>(sqlite3_column_text(integrity.get(), 0));
        if (integrity_text == nullptr || std::string(integrity_text) != "ok") {
            throw std::runtime_error("SQLite database integrity check failed");
        }
    } catch (...) {
        // A destructor is not invoked when this constructor throws. Release
        // the SQLite handle explicitly on every partially initialized path.
        if (db_ != nullptr) {
            (void)sqlite3_close_v2(db_);
            db_ = nullptr;
        }
        throw;
    }
}

SqliteStore::~SqliteStore() {
    if (db_ != nullptr) sqlite3_close(db_);
}

SqliteStore::SqliteStore(SqliteStore&& other) noexcept
    : path_(std::move(other.path_)), db_(std::exchange(other.db_, nullptr)) {}

SqliteStore& SqliteStore::operator=(SqliteStore&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) sqlite3_close(db_);
        path_ = std::move(other.path_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void SqliteStore::initialize_schema() {
    Statement version_query(db_, "PRAGMA user_version;");
    const int step = sqlite3_step(version_query.get());
    check_sqlite(step, db_, "read schema version");
    const int version = sqlite3_column_int(version_query.get(), 0);
    if (version > 6) throw std::runtime_error("database schema is newer than this FeinStatic build");
    if (version == 6) return;

    SqlTransaction transaction(db_);
    if (version == 0) {
        execute(db_,
            "CREATE TABLE IF NOT EXISTS blocks ("
            "height INTEGER PRIMARY KEY CHECK(height >= 0),"
            "hash BLOB NOT NULL UNIQUE CHECK(length(hash) = 32),"
            "payload BLOB NOT NULL);"
            "CREATE TABLE IF NOT EXISTS wallets ("
            "address TEXT PRIMARY KEY,"
            "public_key BLOB NOT NULL CHECK(length(public_key) = 32),"
            "salt BLOB NOT NULL CHECK(length(salt) = 16),"
            "nonce BLOB NOT NULL CHECK(length(nonce) = 12),"
            "ciphertext BLOB NOT NULL CHECK(length(ciphertext) = 32),"
            "tag BLOB NOT NULL CHECK(length(tag) = 16),"
            "iterations INTEGER NOT NULL CHECK(iterations >= 100000));");
    }
    // Version 1 -> 2 adds the shared financial snapshot. Version 2 -> 3 adds
    // the durable mempool. Version 3 -> 4 adds the independent Raft consensus
    // snapshot; version 5 adds completed application snapshots; version 6 adds
    // crash-resumable incoming snapshot staging.
    execute(db_,
        "CREATE TABLE IF NOT EXISTS financial_state ("
        "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
        "revision INTEGER NOT NULL CHECK(revision >= 1),"
        "checksum BLOB NOT NULL CHECK(length(checksum) = 32),"
        "payload BLOB NOT NULL);");
    execute(db_,
        "CREATE TABLE IF NOT EXISTS mempool_transactions ("
        "sequence INTEGER PRIMARY KEY,"
        "transaction_id TEXT NOT NULL UNIQUE CHECK(length(transaction_id) = 64),"
        "payload BLOB NOT NULL CHECK(length(payload) > 0 AND length(payload) <= 1048576));"
        "CREATE TABLE IF NOT EXISTS raft_state ("
        "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
        "revision INTEGER NOT NULL CHECK(revision >= 1),"
        "checksum BLOB NOT NULL CHECK(length(checksum) = 32),"
        "payload BLOB NOT NULL CHECK(length(payload) > 0 AND length(payload) <= 67108864));"
        "CREATE TABLE IF NOT EXISTS raft_snapshots ("
        "snapshot_index INTEGER PRIMARY KEY CHECK(snapshot_index > 0),"
        "checksum BLOB NOT NULL CHECK(length(checksum) = 32),"
        "payload BLOB NOT NULL CHECK(length(payload) > 0 AND length(payload) <= 67108864));"
        "CREATE TABLE IF NOT EXISTS raft_snapshot_staging ("
        "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
        "source_id TEXT NOT NULL CHECK(length(source_id) BETWEEN 1 AND 128),"
        "snapshot_index INTEGER NOT NULL CHECK(snapshot_index > 0),"
        "snapshot_term INTEGER NOT NULL CHECK(snapshot_term > 0),"
        "total_size INTEGER NOT NULL CHECK(total_size > 0 AND total_size <= 67108864),"
        "checksum BLOB NOT NULL CHECK(length(checksum) = 32),"
        "membership BLOB NOT NULL CHECK(length(membership) <= 8192),"
        "next_offset INTEGER NOT NULL CHECK(next_offset >= 0 AND next_offset <= total_size));"
        "CREATE TABLE IF NOT EXISTS raft_snapshot_staging_chunks ("
        "chunk_offset INTEGER PRIMARY KEY CHECK(chunk_offset >= 0),"
        "payload BLOB NOT NULL CHECK(length(payload) > 0 AND length(payload) <= 524288));");
    execute(db_, "PRAGMA user_version = 6;");
    transaction.commit();
    restrict_file_permissions();
}

void SqliteStore::save_genesis(const ledger::Block& genesis) {
    if (genesis.height != 0) throw std::invalid_argument("save_genesis requires block height zero");
    SqlTransaction transaction(db_);
    Statement count(db_, "SELECT COUNT(*) FROM blocks;");
    const int step = sqlite3_step(count.get());
    check_sqlite(step, db_, "count blocks");
    if (sqlite3_column_int64(count.get(), 0) != 0) throw std::runtime_error("genesis already exists");

    Statement insert(db_, "INSERT INTO blocks(height, hash, payload) VALUES(?1, ?2, ?3);");
    check_sqlite(sqlite3_bind_int64(insert.get(), 1, 0), db_, "bind genesis height");
    bind_blob(insert.get(), 2, genesis.hash.data(), genesis.hash.size(), db_);
    const Bytes payload = genesis.serialize();
    bind_blob(insert.get(), 3, payload.data(), payload.size(), db_);
    insert.step_done();
    transaction.commit();
    restrict_file_permissions();
}

void SqliteStore::append_block(const ledger::Block& block) {
    append_blocks(std::vector<ledger::Block>{block});
}

void SqliteStore::append_blocks(const std::vector<ledger::Block>& blocks) {
    if (blocks.empty()) return;

    // Serialize and validate storage-level bounds before opening the SQL
    // transaction. Domain validation is performed by Ledger::accept_blocks.
    std::vector<Bytes> payloads;
    payloads.reserve(blocks.size());
    for (const auto& block : blocks) {
        if (block.height == 0 || block.height > static_cast<std::uint64_t>(INT64_MAX)) {
            throw std::invalid_argument("invalid persisted block height");
        }
        payloads.push_back(block.serialize());
    }

    SqlTransaction transaction(db_);
    Statement previous(db_, "SELECT COALESCE(MAX(height), -1) FROM blocks;");
    const int step = sqlite3_step(previous.get());
    check_sqlite(step, db_, "read chain height");
    const sqlite3_int64 current_height = sqlite3_column_int64(previous.get(), 0);
    if (current_height < 0) throw std::runtime_error("database has no genesis block");

    Statement insert(db_, "INSERT INTO blocks(height, hash, payload) VALUES(?1, ?2, ?3);");
    std::uint64_t expected_height = static_cast<std::uint64_t>(current_height) + 1;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        if (block.height != expected_height) {
            throw std::runtime_error("database block height is not contiguous with imported batch");
        }
        check_sqlite(sqlite3_bind_int64(insert.get(), 1, static_cast<sqlite3_int64>(block.height)),
                     db_, "bind block height");
        bind_blob(insert.get(), 2, block.hash.data(), block.hash.size(), db_);
        bind_blob(insert.get(), 3, payloads[i].data(), payloads[i].size(), db_);
        insert.step_done();
        if (i + 1 < blocks.size()) {
            sqlite3_reset(insert.get());
            sqlite3_clear_bindings(insert.get());
        }
        if (expected_height == static_cast<std::uint64_t>(INT64_MAX) && i + 1 < blocks.size()) {
            throw std::overflow_error("persisted block height exhausted");
        }
        ++expected_height;
    }
    transaction.commit();
    restrict_file_permissions();
}

void SqliteStore::append_blocks_and_replace_mempool(
    const std::vector<ledger::Block>& blocks,
    const std::vector<ledger::Transaction>& mempool_transactions) {
    (void)append_blocks_and_replace_mempool_impl(blocks, mempool_transactions, nullptr, 0);
}

std::uint64_t SqliteStore::append_blocks_mempool_and_save_raft_state(
    const std::vector<ledger::Block>& blocks,
    const std::vector<ledger::Transaction>& mempool_transactions,
    const Bytes& raft_payload,
    std::uint64_t expected_raft_revision) {
    return append_blocks_and_replace_mempool_impl(
        blocks, mempool_transactions, &raft_payload, expected_raft_revision);
}

std::uint64_t SqliteStore::append_blocks_and_replace_mempool_impl(
    const std::vector<ledger::Block>& blocks,
    const std::vector<ledger::Transaction>& mempool_transactions,
    const Bytes* raft_payload,
    std::uint64_t expected_raft_revision) {
    std::uint64_t next_raft_revision = 0;
    if (raft_payload != nullptr) {
        constexpr std::size_t kMaximumRaftStateBytes = 64U * 1024U * 1024U;
        if (raft_payload->empty() || raft_payload->size() > kMaximumRaftStateBytes) {
            throw std::length_error("Raft state snapshot is empty or exceeds 64 MiB");
        }
        if (expected_raft_revision >= static_cast<std::uint64_t>(INT64_MAX)) {
            throw std::overflow_error("Raft state revision exhausted");
        }
    }
    if (mempool_transactions.size() > ledger::kMaximumMempoolTransactions) {
        throw std::length_error("durable mempool exceeds transaction-count limit");
    }

    std::vector<Bytes> block_payloads;
    block_payloads.reserve(blocks.size());
    for (const auto& block : blocks) {
        if (block.height == 0 || block.height > static_cast<std::uint64_t>(INT64_MAX)) {
            throw std::invalid_argument("invalid persisted block height");
        }
        block_payloads.push_back(block.serialize());
    }

    struct EncodedTransaction {
        std::string id;
        Bytes payload;
    };
    std::vector<EncodedTransaction> encoded_transactions;
    encoded_transactions.reserve(mempool_transactions.size());
    std::set<std::string> transaction_ids;
    std::size_t total_bytes = 0;
    for (const auto& transaction : mempool_transactions) {
        if (!transaction.verify_signature()) {
            throw std::invalid_argument("cannot persist an invalid signed mempool transaction");
        }
        const Bytes payload = transaction.serialize();
        if (payload.empty() || payload.size() > 1024U * 1024U ||
            payload.size() > ledger::kMaximumMempoolSerializedBytes - total_bytes) {
            throw std::length_error("durable mempool exceeds serialized-byte limit");
        }
        total_bytes += payload.size();
        const Hash hash = transaction.id();
        const std::string id = crypto::hex(hash.data(), hash.size());
        if (!transaction_ids.insert(id).second) {
            throw std::invalid_argument("durable mempool contains duplicate transaction IDs");
        }
        encoded_transactions.push_back({id, payload});
    }

    SqlTransaction transaction(db_);
    if (!blocks.empty()) {
        Statement previous(db_, "SELECT COALESCE(MAX(height), -1) FROM blocks;");
        const int step = sqlite3_step(previous.get());
        check_sqlite(step, db_, "read chain height");
        const sqlite3_int64 current_height = sqlite3_column_int64(previous.get(), 0);
        if (current_height < 0) throw std::runtime_error("database has no genesis block");

        Statement insert_block(db_, "INSERT INTO blocks(height, hash, payload) VALUES(?1, ?2, ?3);");
        std::uint64_t expected_height = static_cast<std::uint64_t>(current_height) + 1;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const auto& block = blocks[i];
            if (block.height != expected_height) {
                throw std::runtime_error("database block height is not contiguous with imported batch");
            }
            check_sqlite(sqlite3_bind_int64(insert_block.get(), 1,
                                           static_cast<sqlite3_int64>(block.height)),
                         db_, "bind block height");
            bind_blob(insert_block.get(), 2, block.hash.data(), block.hash.size(), db_);
            bind_blob(insert_block.get(), 3, block_payloads[i].data(), block_payloads[i].size(), db_);
            insert_block.step_done();
            if (i + 1 < blocks.size()) {
                check_sqlite(sqlite3_reset(insert_block.get()), db_, "reset block insert");
                check_sqlite(sqlite3_clear_bindings(insert_block.get()), db_, "clear block bindings");
            }
            if (expected_height == static_cast<std::uint64_t>(INT64_MAX) && i + 1 < blocks.size()) {
                throw std::overflow_error("persisted block height exhausted");
            }
            ++expected_height;
        }
    }

    execute(db_, "DELETE FROM mempool_transactions;");
    Statement insert_mempool(db_,
        "INSERT INTO mempool_transactions(transaction_id, payload) VALUES(?1, ?2);");
    for (std::size_t i = 0; i < encoded_transactions.size(); ++i) {
        const auto& encoded = encoded_transactions[i];
        bind_text(insert_mempool.get(), 1, encoded.id, db_);
        bind_blob(insert_mempool.get(), 2, encoded.payload.data(), encoded.payload.size(), db_);
        insert_mempool.step_done();
        if (i + 1 < encoded_transactions.size()) {
            check_sqlite(sqlite3_reset(insert_mempool.get()), db_, "reset mempool insert");
            check_sqlite(sqlite3_clear_bindings(insert_mempool.get()), db_, "clear mempool bindings");
        }
    }
    if (raft_payload != nullptr) {
        Statement current_raft(db_, "SELECT revision FROM raft_state WHERE singleton=1;");
        const int raft_step = sqlite3_step(current_raft.get());
        std::uint64_t actual_raft_revision = 0;
        if (raft_step == SQLITE_ROW) {
            const sqlite3_int64 stored = sqlite3_column_int64(current_raft.get(), 0);
            if (stored < 1) throw std::runtime_error("invalid current Raft state revision");
            actual_raft_revision = static_cast<std::uint64_t>(stored);
        } else if (raft_step != SQLITE_DONE) {
            check_sqlite(raft_step, db_, "read Raft state revision during Ledger application");
        }
        if (actual_raft_revision != expected_raft_revision) {
            throw std::runtime_error("Raft state revision conflict during atomic Ledger application");
        }
        next_raft_revision = actual_raft_revision + 1;
        const Hash checksum = crypto::sha256(*raft_payload);
        Statement upsert_raft(db_,
            "INSERT INTO raft_state(singleton, revision, checksum, payload) VALUES(1, ?1, ?2, ?3) "
            "ON CONFLICT(singleton) DO UPDATE SET revision=excluded.revision, "
            "checksum=excluded.checksum, payload=excluded.payload;");
        check_sqlite(sqlite3_bind_int64(upsert_raft.get(), 1,
                                       static_cast<sqlite3_int64>(next_raft_revision)),
                     db_, "bind atomic Raft revision");
        bind_blob(upsert_raft.get(), 2, checksum.data(), checksum.size(), db_);
        bind_blob(upsert_raft.get(), 3, raft_payload->data(), raft_payload->size(), db_);
        upsert_raft.step_done();
    }

    transaction.commit();
    restrict_file_permissions();
    return next_raft_revision;
}

void SqliteStore::replace_chain_and_mempool(
    const std::vector<ledger::Block>& complete_chain,
    const std::vector<ledger::Transaction>& mempool_transactions) {
    if (complete_chain.empty() || complete_chain.front().height != 0) {
        throw std::invalid_argument("replacement chain must contain its genesis block");
    }
    if (complete_chain.size() - 1 > static_cast<std::size_t>(INT64_MAX)) {
        throw std::length_error("replacement chain exceeds SQLite height range");
    }
    if (mempool_transactions.size() > ledger::kMaximumMempoolTransactions) {
        throw std::length_error("replacement mempool exceeds transaction-count limit");
    }

    std::vector<Bytes> block_payloads;
    block_payloads.reserve(complete_chain.size());
    for (std::size_t i = 0; i < complete_chain.size(); ++i) {
        if (complete_chain[i].height != static_cast<std::uint64_t>(i)) {
            throw std::invalid_argument("replacement chain heights are not contiguous");
        }
        block_payloads.push_back(complete_chain[i].serialize());
    }

    struct EncodedTransaction { std::string id; Bytes payload; };
    std::vector<EncodedTransaction> encoded_transactions;
    encoded_transactions.reserve(mempool_transactions.size());
    std::set<std::string> transaction_ids;
    std::size_t total_bytes = 0;
    for (const auto& transaction_value : mempool_transactions) {
        if (!transaction_value.verify_signature()) {
            throw std::invalid_argument("cannot persist an invalid replacement mempool transaction");
        }
        Bytes payload = transaction_value.serialize();
        if (payload.empty() || payload.size() > 1024U * 1024U ||
            payload.size() > ledger::kMaximumMempoolSerializedBytes - total_bytes) {
            throw std::length_error("replacement mempool exceeds serialized-byte limit");
        }
        total_bytes += payload.size();
        const Hash hash = transaction_value.id();
        const std::string id = crypto::hex(hash.data(), hash.size());
        if (!transaction_ids.insert(id).second) {
            throw std::invalid_argument("replacement mempool contains duplicate transaction IDs");
        }
        encoded_transactions.push_back({id, std::move(payload)});
    }

    SqlTransaction transaction(db_);
    Statement genesis_query(db_, "SELECT hash, payload FROM blocks WHERE height = 0;");
    const int genesis_step = sqlite3_step(genesis_query.get());
    check_sqlite(genesis_step, db_, "read canonical genesis before chain replacement");
    if (genesis_step != SQLITE_ROW) throw std::runtime_error("database has no canonical genesis block");
    const Bytes stored_genesis_payload = column_blob(genesis_query.get(), 1);
    const Bytes stored_genesis_hash = column_blob(genesis_query.get(), 0);
    const auto& candidate_genesis = complete_chain.front();
    if (stored_genesis_payload != block_payloads.front() ||
        stored_genesis_hash.size() != candidate_genesis.hash.size() ||
        !std::equal(stored_genesis_hash.begin(), stored_genesis_hash.end(), candidate_genesis.hash.begin())) {
        throw std::runtime_error("replacement chain genesis does not match the database genesis");
    }

    execute(db_, "DELETE FROM blocks WHERE height > 0;");
    Statement insert_block(db_, "INSERT INTO blocks(height, hash, payload) VALUES(?1, ?2, ?3);");
    for (std::size_t i = 1; i < complete_chain.size(); ++i) {
        const auto height = complete_chain[i].height;
        check_sqlite(sqlite3_bind_int64(insert_block.get(), 1, static_cast<sqlite3_int64>(height)),
                     db_, "bind replacement block height");
        bind_blob(insert_block.get(), 2, complete_chain[i].hash.data(), complete_chain[i].hash.size(), db_);
        bind_blob(insert_block.get(), 3, block_payloads[i].data(), block_payloads[i].size(), db_);
        insert_block.step_done();
        if (i + 1 < complete_chain.size()) {
            check_sqlite(sqlite3_reset(insert_block.get()), db_, "reset replacement block insert");
            check_sqlite(sqlite3_clear_bindings(insert_block.get()), db_, "clear replacement block bindings");
        }
    }

    execute(db_, "DELETE FROM mempool_transactions;");
    Statement insert_mempool(db_,
        "INSERT INTO mempool_transactions(transaction_id, payload) VALUES(?1, ?2);");
    for (std::size_t i = 0; i < encoded_transactions.size(); ++i) {
        const auto& encoded = encoded_transactions[i];
        bind_text(insert_mempool.get(), 1, encoded.id, db_);
        bind_blob(insert_mempool.get(), 2, encoded.payload.data(), encoded.payload.size(), db_);
        insert_mempool.step_done();
        if (i + 1 < encoded_transactions.size()) {
            check_sqlite(sqlite3_reset(insert_mempool.get()), db_, "reset replacement mempool insert");
            check_sqlite(sqlite3_clear_bindings(insert_mempool.get()), db_, "clear replacement mempool bindings");
        }
    }
    transaction.commit();
    restrict_file_permissions();
}

void SqliteStore::save_mempool_transaction(const ledger::Transaction& transaction) {
    if (!transaction.verify_signature()) {
        throw std::invalid_argument("cannot persist an invalid signed mempool transaction");
    }
    const Bytes payload = transaction.serialize();
    if (payload.empty() || payload.size() > 1024U * 1024U) {
        throw std::length_error("mempool transaction exceeds storage size limit");
    }
    const Hash hash = transaction.id();
    const std::string id = crypto::hex(hash.data(), hash.size());

    SqlTransaction sql_transaction(db_);
    Statement count(db_, "SELECT COUNT(*), COALESCE(SUM(length(payload)), 0) FROM mempool_transactions;");
    const int step = sqlite3_step(count.get());
    check_sqlite(step, db_, "count durable mempool");
    const sqlite3_int64 current_count = sqlite3_column_int64(count.get(), 0);
    const sqlite3_int64 current_bytes = sqlite3_column_int64(count.get(), 1);
    if (current_count < 0 || current_bytes < 0 ||
        static_cast<std::uint64_t>(current_count) >= ledger::kMaximumMempoolTransactions ||
        static_cast<std::uint64_t>(current_bytes) > ledger::kMaximumMempoolSerializedBytes ||
        payload.size() > ledger::kMaximumMempoolSerializedBytes - static_cast<std::size_t>(current_bytes)) {
        throw std::length_error("durable mempool capacity reached");
    }
    Statement insert(db_,
        "INSERT INTO mempool_transactions(transaction_id, payload) VALUES(?1, ?2);");
    bind_text(insert.get(), 1, id, db_);
    bind_blob(insert.get(), 2, payload.data(), payload.size(), db_);
    insert.step_done();
    sql_transaction.commit();
    restrict_file_permissions();
}

std::vector<ledger::Transaction> SqliteStore::load_mempool_transactions() const {
    Statement query(db_,
        "SELECT transaction_id, payload FROM mempool_transactions ORDER BY sequence ASC;");
    std::vector<ledger::Transaction> result;
    std::size_t total_bytes = 0;
    for (;;) {
        const int step = sqlite3_step(query.get());
        if (step == SQLITE_DONE) break;
        check_sqlite(step, db_, "load durable mempool");
        if (result.size() >= ledger::kMaximumMempoolTransactions) {
            throw std::runtime_error("persisted mempool exceeds transaction-count limit");
        }
        const auto* id_text = reinterpret_cast<const char*>(sqlite3_column_text(query.get(), 0));
        if (id_text == nullptr) throw std::runtime_error("persisted mempool transaction ID is NULL");
        const Bytes payload = column_blob(query.get(), 1);
        if (payload.empty() || payload.size() > 1024U * 1024U ||
            payload.size() > ledger::kMaximumMempoolSerializedBytes - total_bytes) {
            throw std::runtime_error("persisted mempool exceeds serialized-byte limit");
        }
        total_bytes += payload.size();
        ledger::Transaction decoded = ledger::Transaction::deserialize(payload);
        const Hash hash = decoded.id();
        const std::string calculated_id = crypto::hex(hash.data(), hash.size());
        if (calculated_id != id_text) {
            throw std::runtime_error("persisted mempool transaction ID does not match its payload");
        }
        result.push_back(std::move(decoded));
    }
    return result;
}

std::vector<ledger::Block> SqliteStore::load_blocks() const {
    Statement query(db_, "SELECT height, hash, payload FROM blocks ORDER BY height ASC;");
    std::vector<ledger::Block> result;
    std::uint64_t expected_height = 0;
    for (;;) {
        const int step = sqlite3_step(query.get());
        if (step == SQLITE_DONE) break;
        check_sqlite(step, db_, "read blocks");

        const sqlite3_int64 stored_height = sqlite3_column_int64(query.get(), 0);
        if (stored_height < 0 || static_cast<std::uint64_t>(stored_height) != expected_height) {
            throw std::runtime_error("persisted block heights are negative, duplicated, or non-contiguous");
        }
        const Bytes stored_hash = column_blob(query.get(), 1);
        const ledger::Block block = ledger::Block::deserialize(column_blob(query.get(), 2));
        const Bytes calculated_hash(block.hash.begin(), block.hash.end());
        if (block.height != expected_height || stored_hash != calculated_hash) {
            throw std::runtime_error("persisted block index metadata does not match canonical block payload");
        }
        result.push_back(block);
        if (expected_height == UINT64_MAX) throw std::overflow_error("persisted chain height exhausted");
        ++expected_height;
    }
    return result;
}

void SqliteStore::save_wallet(const WalletRecord& wallet) {
    if (!crypto::is_valid_address(wallet.address) ||
        crypto::address_from_public_key(wallet.public_key) != wallet.address) {
        throw std::invalid_argument("wallet address/public key mismatch");
    }
    if (wallet.encrypted_secret.ciphertext.size() != 32 ||
        wallet.encrypted_secret.iterations < 100'000 ||
        wallet.encrypted_secret.iterations > 5'000'000) {
        throw std::invalid_argument("wallet encrypted-secret metadata is invalid");
    }
    Statement statement(db_,
        "INSERT INTO wallets(address, public_key, salt, nonce, ciphertext, tag, iterations) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7) "
        "ON CONFLICT(address) DO UPDATE SET public_key=excluded.public_key, salt=excluded.salt, "
        "nonce=excluded.nonce, ciphertext=excluded.ciphertext, tag=excluded.tag, iterations=excluded.iterations;");
    bind_text(statement.get(), 1, wallet.address, db_);
    bind_blob(statement.get(), 2, wallet.public_key.data(), wallet.public_key.size(), db_);
    bind_blob(statement.get(), 3, wallet.encrypted_secret.salt.data(), wallet.encrypted_secret.salt.size(), db_);
    bind_blob(statement.get(), 4, wallet.encrypted_secret.nonce.data(), wallet.encrypted_secret.nonce.size(), db_);
    bind_blob(statement.get(), 5, wallet.encrypted_secret.ciphertext.data(), wallet.encrypted_secret.ciphertext.size(), db_);
    bind_blob(statement.get(), 6, wallet.encrypted_secret.tag.data(), wallet.encrypted_secret.tag.size(), db_);
    check_sqlite(sqlite3_bind_int64(statement.get(), 7,
                                   static_cast<sqlite3_int64>(wallet.encrypted_secret.iterations)),
                 db_, "bind PBKDF2 iterations");
    statement.step_done();
    restrict_file_permissions();
}

std::optional<WalletRecord> SqliteStore::load_wallet(const Address& address) const {
    Statement statement(db_,
        "SELECT address, public_key, salt, nonce, ciphertext, tag, iterations FROM wallets WHERE address=?1;");
    bind_text(statement.get(), 1, address, db_);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return std::nullopt;
    check_sqlite(step, db_, "load wallet");

    WalletRecord result;
    const auto* address_text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
    if (address_text == nullptr) throw std::runtime_error("wallet address is NULL");
    result.address = address_text;
    result.public_key = fixed_column_blob<32>(statement.get(), 1);
    result.encrypted_secret.salt = fixed_column_blob<16>(statement.get(), 2);
    result.encrypted_secret.nonce = fixed_column_blob<12>(statement.get(), 3);
    result.encrypted_secret.ciphertext = column_blob(statement.get(), 4);
    result.encrypted_secret.tag = fixed_column_blob<16>(statement.get(), 5);
    const sqlite3_int64 iterations = sqlite3_column_int64(statement.get(), 6);
    if (iterations < 0 || iterations > UINT32_MAX) throw std::runtime_error("invalid wallet KDF iteration count");
    result.encrypted_secret.iterations = static_cast<std::uint32_t>(iterations);
    if (!crypto::is_valid_address(result.address) ||
        crypto::address_from_public_key(result.public_key) != result.address ||
        result.encrypted_secret.ciphertext.size() != 32 ||
        result.encrypted_secret.iterations < 100'000 ||
        result.encrypted_secret.iterations > 5'000'000) {
        throw std::runtime_error("stored wallet identity or encryption metadata is invalid");
    }
    return result;
}

std::vector<std::pair<Address, PublicKey>> SqliteStore::list_wallets() const {
    Statement statement(db_, "SELECT address, public_key FROM wallets ORDER BY address ASC;");
    std::vector<std::pair<Address, PublicKey>> result;
    for (;;) {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        check_sqlite(step, db_, "list wallets");
        const auto* address_text = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
        if (address_text == nullptr) throw std::runtime_error("wallet address is NULL");
        const PublicKey public_key = fixed_column_blob<32>(statement.get(), 1);
        const Address address = address_text;
        if (!crypto::is_valid_address(address) || crypto::address_from_public_key(public_key) != address) {
            throw std::runtime_error("stored wallet address does not match its public key");
        }
        result.emplace_back(address, public_key);
    }
    return result;
}

std::optional<FinancialStateRecord> SqliteStore::load_financial_state() const {
    Statement statement(db_, "SELECT revision, checksum, payload FROM financial_state WHERE singleton=1;");
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return std::nullopt;
    check_sqlite(step, db_, "load financial state");

    const sqlite3_int64 revision = sqlite3_column_int64(statement.get(), 0);
    if (revision < 1) throw std::runtime_error("invalid financial state revision");
    const Bytes checksum = column_blob(statement.get(), 1);
    const Bytes payload = column_blob(statement.get(), 2);
    if (checksum.size() != 32 || payload.empty()) throw std::runtime_error("invalid persisted financial state record");
    const Hash calculated = crypto::sha256(payload);
    if (!std::equal(checksum.begin(), checksum.end(), calculated.begin(), calculated.end())) {
        throw std::runtime_error("persisted financial state checksum mismatch");
    }
    return FinancialStateRecord{static_cast<std::uint64_t>(revision), payload};
}

std::uint64_t SqliteStore::save_financial_state(const Bytes& payload,
                                                std::uint64_t expected_revision) {
    constexpr std::size_t kMaximumSnapshotBytes = 64U * 1024U * 1024U;
    if (payload.empty() || payload.size() > kMaximumSnapshotBytes) {
        throw std::length_error("financial state snapshot is empty or exceeds 64 MiB");
    }
    if (expected_revision >= static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::overflow_error("financial state revision exhausted");
    }

    SqlTransaction transaction(db_);
    Statement current(db_, "SELECT revision FROM financial_state WHERE singleton=1;");
    const int step = sqlite3_step(current.get());
    std::uint64_t actual_revision = 0;
    if (step == SQLITE_ROW) {
        const sqlite3_int64 stored = sqlite3_column_int64(current.get(), 0);
        if (stored < 1) throw std::runtime_error("invalid current financial state revision");
        actual_revision = static_cast<std::uint64_t>(stored);
    } else if (step != SQLITE_DONE) {
        check_sqlite(step, db_, "read financial state revision");
    }
    if (actual_revision != expected_revision) {
        throw std::runtime_error("financial state revision conflict; another process committed a newer state");
    }

    const std::uint64_t next_revision = actual_revision + 1;
    const Hash checksum = crypto::sha256(payload);
    Statement upsert(db_,
        "INSERT INTO financial_state(singleton, revision, checksum, payload) VALUES(1, ?1, ?2, ?3) "
        "ON CONFLICT(singleton) DO UPDATE SET revision=excluded.revision, "
        "checksum=excluded.checksum, payload=excluded.payload;");
    check_sqlite(sqlite3_bind_int64(upsert.get(), 1, static_cast<sqlite3_int64>(next_revision)), db_, "bind financial revision");
    bind_blob(upsert.get(), 2, checksum.data(), checksum.size(), db_);
    bind_blob(upsert.get(), 3, payload.data(), payload.size(), db_);
    upsert.step_done();
    transaction.commit();
    restrict_file_permissions();
    return next_revision;
}

std::optional<RaftStateRecord> SqliteStore::load_raft_state() const {
    Statement statement(db_, "SELECT revision, checksum, payload FROM raft_state WHERE singleton=1;");
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return std::nullopt;
    check_sqlite(step, db_, "load Raft state");

    const sqlite3_int64 revision = sqlite3_column_int64(statement.get(), 0);
    if (revision < 1) throw std::runtime_error("invalid Raft state revision");
    const Bytes checksum = column_blob(statement.get(), 1);
    const Bytes payload = column_blob(statement.get(), 2);
    if (checksum.size() != 32 || payload.empty()) throw std::runtime_error("invalid persisted Raft state record");
    const Hash calculated = crypto::sha256(payload);
    if (!std::equal(checksum.begin(), checksum.end(), calculated.begin(), calculated.end())) {
        throw std::runtime_error("persisted Raft state checksum mismatch");
    }
    return RaftStateRecord{static_cast<std::uint64_t>(revision), payload};
}

std::uint64_t SqliteStore::save_raft_state(const Bytes& payload,
                                          std::uint64_t expected_revision) {
    constexpr std::size_t kMaximumRaftStateBytes = 64U * 1024U * 1024U;
    if (payload.empty() || payload.size() > kMaximumRaftStateBytes) {
        throw std::length_error("Raft state snapshot is empty or exceeds 64 MiB");
    }
    if (expected_revision >= static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::overflow_error("Raft state revision exhausted");
    }

    SqlTransaction transaction(db_);
    Statement current(db_, "SELECT revision FROM raft_state WHERE singleton=1;");
    const int step = sqlite3_step(current.get());
    std::uint64_t actual_revision = 0;
    if (step == SQLITE_ROW) {
        const sqlite3_int64 stored = sqlite3_column_int64(current.get(), 0);
        if (stored < 1) throw std::runtime_error("invalid current Raft state revision");
        actual_revision = static_cast<std::uint64_t>(stored);
    } else if (step != SQLITE_DONE) {
        check_sqlite(step, db_, "read Raft state revision");
    }
    if (actual_revision != expected_revision) {
        throw std::runtime_error("Raft state revision conflict; another process committed a newer consensus state");
    }

    const std::uint64_t next_revision = actual_revision + 1;
    const Hash checksum = crypto::sha256(payload);
    Statement upsert(db_,
        "INSERT INTO raft_state(singleton, revision, checksum, payload) VALUES(1, ?1, ?2, ?3) "
        "ON CONFLICT(singleton) DO UPDATE SET revision=excluded.revision, "
        "checksum=excluded.checksum, payload=excluded.payload;");
    check_sqlite(sqlite3_bind_int64(upsert.get(), 1, static_cast<sqlite3_int64>(next_revision)),
                 db_, "bind Raft revision");
    bind_blob(upsert.get(), 2, checksum.data(), checksum.size(), db_);
    bind_blob(upsert.get(), 3, payload.data(), payload.size(), db_);
    upsert.step_done();
    transaction.commit();
    restrict_file_permissions();
    return next_revision;
}


void SqliteStore::save_raft_snapshot(std::uint64_t snapshot_index, const Bytes& payload) {
    if (snapshot_index == 0 || snapshot_index > static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft snapshot index is outside SQLite storage range");
    }
    if (payload.empty() || payload.size() > 64U * 1024U * 1024U) {
        throw std::length_error("Raft application snapshot is empty or exceeds 64 MiB");
    }
    const Hash checksum = crypto::sha256(payload);
    SqlTransaction transaction(db_);
    Statement existing(db_, "SELECT checksum, payload FROM raft_snapshots WHERE snapshot_index=?1;");
    check_sqlite(sqlite3_bind_int64(existing.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind Raft snapshot lookup index");
    const int step = sqlite3_step(existing.get());
    if (step == SQLITE_ROW) {
        const Bytes stored_checksum = column_blob(existing.get(), 0);
        const Bytes stored_payload = column_blob(existing.get(), 1);
        if (stored_checksum.size() != checksum.size() ||
            !std::equal(stored_checksum.begin(), stored_checksum.end(), checksum.begin()) ||
            stored_payload != payload) {
            throw std::runtime_error("Raft snapshot index already exists with different content");
        }
        transaction.commit();
        return;
    }
    check_sqlite(step, db_, "read Raft snapshot lookup");
    Statement insert(db_,
        "INSERT INTO raft_snapshots(snapshot_index, checksum, payload) VALUES(?1, ?2, ?3);");
    check_sqlite(sqlite3_bind_int64(insert.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind Raft snapshot index");
    bind_blob(insert.get(), 2, checksum.data(), checksum.size(), db_);
    bind_blob(insert.get(), 3, payload.data(), payload.size(), db_);
    insert.step_done();
    transaction.commit();
    restrict_file_permissions();
}

std::optional<Bytes> SqliteStore::load_raft_snapshot(std::uint64_t snapshot_index) const {
    if (snapshot_index == 0 || snapshot_index > static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft snapshot index is outside SQLite storage range");
    }
    Statement statement(db_,
        "SELECT checksum, payload FROM raft_snapshots WHERE snapshot_index=?1;");
    check_sqlite(sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind Raft snapshot load index");
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE) return std::nullopt;
    check_sqlite(step, db_, "load Raft application snapshot");
    const Bytes checksum = column_blob(statement.get(), 0);
    const Bytes payload = column_blob(statement.get(), 1);
    if (checksum.size() != 32 || payload.empty()) {
        throw std::runtime_error("persisted Raft application snapshot record is malformed");
    }
    const Hash calculated = crypto::sha256(payload);
    if (!std::equal(checksum.begin(), checksum.end(), calculated.begin(), calculated.end())) {
        throw std::runtime_error("persisted Raft application snapshot checksum mismatch");
    }
    return payload;
}

void SqliteStore::prune_raft_snapshots_before(std::uint64_t snapshot_index) {
    if (snapshot_index == 0 || snapshot_index > static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft snapshot pruning index is outside SQLite storage range");
    }
    SqlTransaction transaction(db_);
    Statement remove(db_, "DELETE FROM raft_snapshots WHERE snapshot_index < ?1;");
    check_sqlite(sqlite3_bind_int64(remove.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind Raft snapshot pruning index");
    remove.step_done();
    transaction.commit();
    restrict_file_permissions();
}

std::optional<consensus::RaftSnapshotTransfer> SqliteStore::load_raft_snapshot_staging() const {
    Statement metadata(db_,
        "SELECT source_id, snapshot_index, snapshot_term, total_size, checksum, membership, next_offset "
        "FROM raft_snapshot_staging WHERE singleton=1;");
    const int step = sqlite3_step(metadata.get());
    if (step == SQLITE_DONE) return std::nullopt;
    check_sqlite(step, db_, "load Raft snapshot staging metadata");

    consensus::RaftSnapshotTransfer transfer;
    const auto* source = reinterpret_cast<const char*>(sqlite3_column_text(metadata.get(), 0));
    const int source_size = sqlite3_column_bytes(metadata.get(), 0);
    if (source == nullptr || source_size <= 0 || source_size > 128) {
        throw std::runtime_error("Raft snapshot staging source identity is malformed");
    }
    transfer.source_id.assign(source, static_cast<std::size_t>(source_size));
    const sqlite3_int64 index = sqlite3_column_int64(metadata.get(), 1);
    const sqlite3_int64 term = sqlite3_column_int64(metadata.get(), 2);
    const sqlite3_int64 total = sqlite3_column_int64(metadata.get(), 3);
    const sqlite3_int64 next = sqlite3_column_int64(metadata.get(), 6);
    if (index <= 0 || term <= 0 || total <= 0 || total > 64LL * 1024LL * 1024LL ||
        next < 0 || next > total) {
        throw std::runtime_error("Raft snapshot staging metadata is outside configured bounds");
    }
    transfer.snapshot_index = static_cast<std::uint64_t>(index);
    transfer.snapshot_term = static_cast<std::uint64_t>(term);
    transfer.total_size = static_cast<std::uint64_t>(total);
    transfer.checksum = fixed_column_blob<32>(metadata.get(), 4);
    transfer.membership = column_blob(metadata.get(), 5);
    if (transfer.membership.size() > 8192U) {
        throw std::runtime_error("Raft snapshot staging membership metadata is oversized");
    }

    Statement chunks(db_, "SELECT chunk_offset, payload FROM raft_snapshot_staging_chunks ORDER BY chunk_offset;");
    std::uint64_t expected_offset = 0;
    while (true) {
        const int chunk_step = sqlite3_step(chunks.get());
        if (chunk_step == SQLITE_DONE) break;
        check_sqlite(chunk_step, db_, "read Raft snapshot staging chunk");
        const sqlite3_int64 stored_offset = sqlite3_column_int64(chunks.get(), 0);
        const Bytes chunk = column_blob(chunks.get(), 1);
        if (stored_offset < 0 || static_cast<std::uint64_t>(stored_offset) != expected_offset ||
            chunk.empty() || chunk.size() > 512U * 1024U ||
            chunk.size() > transfer.total_size - expected_offset) {
            throw std::runtime_error("Raft snapshot staging chunks are not contiguous or exceed bounds");
        }
        transfer.payload.insert(transfer.payload.end(), chunk.begin(), chunk.end());
        expected_offset += static_cast<std::uint64_t>(chunk.size());
    }
    if (expected_offset != static_cast<std::uint64_t>(next)) {
        throw std::runtime_error("Raft snapshot staging offset does not match durable chunk contents");
    }
    return transfer;
}

void SqliteStore::append_raft_snapshot_staging_chunk(
    const consensus::RaftSnapshotTransfer& transfer,
    std::uint64_t offset,
    const Bytes& chunk) {
    constexpr std::uint64_t maximum_snapshot = 64ULL * 1024ULL * 1024ULL;
    constexpr std::size_t maximum_chunk = 512U * 1024U;
    if (transfer.source_id.empty() || transfer.source_id.size() > 128U ||
        transfer.snapshot_index == 0 || transfer.snapshot_index > static_cast<std::uint64_t>(INT64_MAX) ||
        transfer.snapshot_term == 0 || transfer.snapshot_term > static_cast<std::uint64_t>(INT64_MAX) ||
        transfer.total_size == 0 || transfer.total_size > maximum_snapshot ||
        transfer.membership.size() > 8192U || chunk.empty() || chunk.size() > maximum_chunk ||
        offset > transfer.total_size || chunk.size() > transfer.total_size - offset ||
        offset > static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft snapshot staging chunk metadata or payload is outside configured bounds");
    }

    SqlTransaction transaction(db_);
    Statement current(db_,
        "SELECT source_id, snapshot_index, snapshot_term, total_size, checksum, membership, next_offset "
        "FROM raft_snapshot_staging WHERE singleton=1;");
    const int current_step = sqlite3_step(current.get());
    bool same_transfer = false;
    std::uint64_t expected_offset = 0;
    if (current_step == SQLITE_ROW) {
        const auto* source = reinterpret_cast<const char*>(sqlite3_column_text(current.get(), 0));
        const int source_size = sqlite3_column_bytes(current.get(), 0);
        const sqlite3_int64 index = sqlite3_column_int64(current.get(), 1);
        const sqlite3_int64 term = sqlite3_column_int64(current.get(), 2);
        const sqlite3_int64 total = sqlite3_column_int64(current.get(), 3);
        const Bytes checksum = column_blob(current.get(), 4);
        const Bytes membership = column_blob(current.get(), 5);
        const sqlite3_int64 next = sqlite3_column_int64(current.get(), 6);
        same_transfer = source != nullptr && source_size > 0 &&
            std::string(source, static_cast<std::size_t>(source_size)) == transfer.source_id &&
            index == static_cast<sqlite3_int64>(transfer.snapshot_index) &&
            term == static_cast<sqlite3_int64>(transfer.snapshot_term) &&
            total == static_cast<sqlite3_int64>(transfer.total_size) &&
            checksum.size() == transfer.checksum.size() &&
            std::equal(checksum.begin(), checksum.end(), transfer.checksum.begin()) &&
            membership == transfer.membership;
        if (next < 0) throw std::runtime_error("negative durable Raft snapshot staging offset");
        expected_offset = static_cast<std::uint64_t>(next);
    } else if (current_step != SQLITE_DONE) {
        check_sqlite(current_step, db_, "inspect current Raft snapshot staging transfer");
    }

    if (!same_transfer) {
        if (offset != 0) {
            throw std::runtime_error("new Raft snapshot staging transfer must begin at offset zero");
        }
        execute(db_, "DELETE FROM raft_snapshot_staging_chunks; DELETE FROM raft_snapshot_staging;");
        Statement insert_metadata(db_,
            "INSERT INTO raft_snapshot_staging(singleton, source_id, snapshot_index, snapshot_term, "
            "total_size, checksum, membership, next_offset) VALUES(1, ?1, ?2, ?3, ?4, ?5, ?6, 0);");
        bind_text(insert_metadata.get(), 1, transfer.source_id, db_);
        check_sqlite(sqlite3_bind_int64(insert_metadata.get(), 2,
            static_cast<sqlite3_int64>(transfer.snapshot_index)), db_, "bind staged snapshot index");
        check_sqlite(sqlite3_bind_int64(insert_metadata.get(), 3,
            static_cast<sqlite3_int64>(transfer.snapshot_term)), db_, "bind staged snapshot term");
        check_sqlite(sqlite3_bind_int64(insert_metadata.get(), 4,
            static_cast<sqlite3_int64>(transfer.total_size)), db_, "bind staged snapshot size");
        bind_blob(insert_metadata.get(), 5, transfer.checksum.data(), transfer.checksum.size(), db_);
        if (transfer.membership.empty()) {
            check_sqlite(sqlite3_bind_zeroblob(insert_metadata.get(), 6, 0),
                         db_, "bind empty staged snapshot membership");
        } else {
            bind_blob(insert_metadata.get(), 6, transfer.membership.data(), transfer.membership.size(), db_);
        }
        insert_metadata.step_done();
        expected_offset = 0;
    }
    if (offset != expected_offset) {
        throw std::runtime_error("Raft snapshot chunk is not at the next durable staging offset");
    }

    Statement insert_chunk(db_, "INSERT INTO raft_snapshot_staging_chunks(chunk_offset, payload) VALUES(?1, ?2);");
    check_sqlite(sqlite3_bind_int64(insert_chunk.get(), 1, static_cast<sqlite3_int64>(offset)),
                 db_, "bind Raft snapshot chunk offset");
    bind_blob(insert_chunk.get(), 2, chunk.data(), chunk.size(), db_);
    insert_chunk.step_done();

    Statement update_offset(db_, "UPDATE raft_snapshot_staging SET next_offset=?1 WHERE singleton=1 AND next_offset=?2;");
    check_sqlite(sqlite3_bind_int64(update_offset.get(), 1,
        static_cast<sqlite3_int64>(offset + static_cast<std::uint64_t>(chunk.size()))),
        db_, "bind updated snapshot staging offset");
    check_sqlite(sqlite3_bind_int64(update_offset.get(), 2, static_cast<sqlite3_int64>(offset)),
                 db_, "bind expected snapshot staging offset");
    update_offset.step_done();
    if (sqlite3_changes(db_) != 1) {
        throw std::runtime_error("Raft snapshot staging offset changed concurrently");
    }
    transaction.commit();
    restrict_file_permissions();
}

void SqliteStore::clear_raft_snapshot_staging() {
    SqlTransaction transaction(db_);
    execute(db_, "DELETE FROM raft_snapshot_staging_chunks; DELETE FROM raft_snapshot_staging;");
    transaction.commit();
    restrict_file_permissions();
}

std::uint64_t SqliteStore::replace_chain_mempool_and_save_raft_snapshot(
    const std::vector<ledger::Block>& complete_chain,
    const std::vector<ledger::Transaction>& mempool_transactions,
    const Bytes& raft_payload,
    std::uint64_t expected_raft_revision,
    std::uint64_t snapshot_index,
    const Bytes& application_snapshot) {
    if (complete_chain.empty() || complete_chain.front().height != 0 ||
        complete_chain.size() - 1 > static_cast<std::size_t>(INT64_MAX)) {
        throw std::invalid_argument("Raft replacement chain must contain a bounded genesis-based chain");
    }
    if (snapshot_index == 0 || snapshot_index > static_cast<std::uint64_t>(INT64_MAX) ||
        raft_payload.empty() || raft_payload.size() > 64U * 1024U * 1024U ||
        application_snapshot.empty() || application_snapshot.size() > 64U * 1024U * 1024U) {
        throw std::length_error("Raft snapshot installation payload or index is outside configured limits");
    }
    if (mempool_transactions.size() > ledger::kMaximumMempoolTransactions) {
        throw std::length_error("replacement mempool exceeds transaction-count limit");
    }

    std::vector<Bytes> block_payloads;
    block_payloads.reserve(complete_chain.size());
    for (std::size_t i = 0; i < complete_chain.size(); ++i) {
        if (complete_chain[i].height != static_cast<std::uint64_t>(i)) {
            throw std::invalid_argument("replacement chain heights are not contiguous");
        }
        block_payloads.push_back(complete_chain[i].serialize());
    }

    struct EncodedTransaction { std::string id; Bytes payload; };
    std::vector<EncodedTransaction> encoded_transactions;
    encoded_transactions.reserve(mempool_transactions.size());
    std::set<std::string> transaction_ids;
    std::size_t total_bytes = 0;
    for (const auto& transaction_value : mempool_transactions) {
        if (!transaction_value.verify_signature()) {
            throw std::invalid_argument("cannot persist an invalid replacement mempool transaction");
        }
        Bytes payload = transaction_value.serialize();
        if (payload.empty() || payload.size() > 1024U * 1024U ||
            payload.size() > ledger::kMaximumMempoolSerializedBytes - total_bytes) {
            throw std::length_error("replacement mempool exceeds serialized-byte limit");
        }
        total_bytes += payload.size();
        const Hash hash = transaction_value.id();
        const std::string id = crypto::hex(hash.data(), hash.size());
        if (!transaction_ids.insert(id).second) {
            throw std::invalid_argument("replacement mempool contains duplicate transaction IDs");
        }
        encoded_transactions.push_back({id, std::move(payload)});
    }

    const Hash raft_checksum = crypto::sha256(raft_payload);
    const Hash app_checksum = crypto::sha256(application_snapshot);
    SqlTransaction transaction(db_);
    Statement genesis_query(db_, "SELECT hash, payload FROM blocks WHERE height = 0;");
    const int genesis_step = sqlite3_step(genesis_query.get());
    check_sqlite(genesis_step, db_, "read canonical genesis before Raft snapshot installation");
    if (genesis_step != SQLITE_ROW) throw std::runtime_error("database has no canonical genesis block");
    const Bytes stored_genesis_payload = column_blob(genesis_query.get(), 1);
    const Bytes stored_genesis_hash = column_blob(genesis_query.get(), 0);
    if (stored_genesis_payload != block_payloads.front() ||
        stored_genesis_hash.size() != complete_chain.front().hash.size() ||
        !std::equal(stored_genesis_hash.begin(), stored_genesis_hash.end(),
                    complete_chain.front().hash.begin())) {
        throw std::runtime_error("installed Raft snapshot has a different Ledger genesis");
    }

    execute(db_, "DELETE FROM blocks WHERE height > 0;");
    Statement insert_block(db_, "INSERT INTO blocks(height, hash, payload) VALUES(?1, ?2, ?3);");
    for (std::size_t i = 1; i < complete_chain.size(); ++i) {
        check_sqlite(sqlite3_bind_int64(insert_block.get(), 1,
            static_cast<sqlite3_int64>(complete_chain[i].height)), db_, "bind installed block height");
        bind_blob(insert_block.get(), 2, complete_chain[i].hash.data(), complete_chain[i].hash.size(), db_);
        bind_blob(insert_block.get(), 3, block_payloads[i].data(), block_payloads[i].size(), db_);
        insert_block.step_done();
        if (i + 1 < complete_chain.size()) {
            check_sqlite(sqlite3_reset(insert_block.get()), db_, "reset installed block insert");
            check_sqlite(sqlite3_clear_bindings(insert_block.get()), db_, "clear installed block bindings");
        }
    }

    execute(db_, "DELETE FROM mempool_transactions;");
    Statement insert_mempool(db_,
        "INSERT INTO mempool_transactions(transaction_id, payload) VALUES(?1, ?2);");
    for (std::size_t i = 0; i < encoded_transactions.size(); ++i) {
        bind_text(insert_mempool.get(), 1, encoded_transactions[i].id, db_);
        bind_blob(insert_mempool.get(), 2, encoded_transactions[i].payload.data(),
                  encoded_transactions[i].payload.size(), db_);
        insert_mempool.step_done();
        if (i + 1 < encoded_transactions.size()) {
            check_sqlite(sqlite3_reset(insert_mempool.get()), db_, "reset installed mempool insert");
            check_sqlite(sqlite3_clear_bindings(insert_mempool.get()), db_, "clear installed mempool bindings");
        }
    }

    Statement current_raft(db_, "SELECT revision FROM raft_state WHERE singleton=1;");
    const int raft_step = sqlite3_step(current_raft.get());
    std::uint64_t actual_revision = 0;
    if (raft_step == SQLITE_ROW) {
        const sqlite3_int64 stored = sqlite3_column_int64(current_raft.get(), 0);
        if (stored < 1) throw std::runtime_error("invalid current Raft state revision");
        actual_revision = static_cast<std::uint64_t>(stored);
    } else if (raft_step != SQLITE_DONE) {
        check_sqlite(raft_step, db_, "read Raft revision during snapshot installation");
    }
    if (actual_revision != expected_raft_revision || actual_revision >= static_cast<std::uint64_t>(INT64_MAX)) {
        throw std::runtime_error("Raft state revision conflict or exhaustion during snapshot installation");
    }
    const std::uint64_t next_revision = actual_revision + 1U;
    Statement upsert_raft(db_,
        "INSERT INTO raft_state(singleton, revision, checksum, payload) VALUES(1, ?1, ?2, ?3) "
        "ON CONFLICT(singleton) DO UPDATE SET revision=excluded.revision, "
        "checksum=excluded.checksum, payload=excluded.payload;");
    check_sqlite(sqlite3_bind_int64(upsert_raft.get(), 1, static_cast<sqlite3_int64>(next_revision)),
                 db_, "bind installed Raft revision");
    bind_blob(upsert_raft.get(), 2, raft_checksum.data(), raft_checksum.size(), db_);
    bind_blob(upsert_raft.get(), 3, raft_payload.data(), raft_payload.size(), db_);
    upsert_raft.step_done();

    Statement snapshot_insert(db_,
        "INSERT INTO raft_snapshots(snapshot_index, checksum, payload) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(snapshot_index) DO UPDATE SET checksum=excluded.checksum, payload=excluded.payload;");
    check_sqlite(sqlite3_bind_int64(snapshot_insert.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind installed Raft snapshot index");
    bind_blob(snapshot_insert.get(), 2, app_checksum.data(), app_checksum.size(), db_);
    bind_blob(snapshot_insert.get(), 3, application_snapshot.data(), application_snapshot.size(), db_);
    snapshot_insert.step_done();
    Statement prune(db_, "DELETE FROM raft_snapshots WHERE snapshot_index < ?1;");
    check_sqlite(sqlite3_bind_int64(prune.get(), 1, static_cast<sqlite3_int64>(snapshot_index)),
                 db_, "bind installed Raft snapshot pruning index");
    prune.step_done();

    transaction.commit();
    restrict_file_permissions();
    return next_revision;
}

void SqliteStore::restrict_file_permissions() const {
    if (path_.empty() || path_ == ":memory:") return;
    const auto secure_file = [](const std::string& file) {
        if (::chmod(file.c_str(), S_IRUSR | S_IWUSR) != 0 && errno != ENOENT) {
            throw std::system_error(errno, std::generic_category(), "unable to restrict permissions for " + file);
        }
    };
    secure_file(path_);
    secure_file(path_ + "-wal");
    secure_file(path_ + "-shm");
}

} // namespace feinstat::storage
