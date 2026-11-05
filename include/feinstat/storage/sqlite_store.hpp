#pragma once

#include "feinstat/crypto/crypto.hpp"
#include "feinstat/consensus/raft.hpp"
#include "feinstat/ledger/block.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace feinstat::storage {

struct WalletRecord {
    Address address;
    PublicKey public_key{};
    crypto::EncryptedSecret encrypted_secret;
};

struct FinancialStateRecord {
    std::uint64_t revision{0};
    Bytes payload;
};

struct RaftStateRecord {
    std::uint64_t revision{0};
    Bytes payload;
};

class SqliteStore {
public:
    explicit SqliteStore(const std::string& path);
    ~SqliteStore();

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    SqliteStore(SqliteStore&& other) noexcept;
    SqliteStore& operator=(SqliteStore&& other) noexcept;

    void initialize_schema();
    void save_genesis(const ledger::Block& genesis);
    void append_block(const ledger::Block& block);
    void append_blocks(const std::vector<ledger::Block>& blocks);
    // Commits a block batch and the resulting durable mempool snapshot in one
    // SQLite transaction. Either both transitions become durable or neither does.
    void append_blocks_and_replace_mempool(
        const std::vector<ledger::Block>& blocks,
        const std::vector<ledger::Transaction>& mempool_transactions);
    // Atomically commits Ledger blocks, the resulting durable mempool, and a
    // Raft snapshot with the updated last_applied index. This is the crash
    // boundary between consensus commitment and deterministic Ledger effects.
    [[nodiscard]] std::uint64_t append_blocks_mempool_and_save_raft_state(
        const std::vector<ledger::Block>& blocks,
        const std::vector<ledger::Transaction>& mempool_transactions,
        const Bytes& raft_payload,
        std::uint64_t expected_raft_revision);
    // Atomically replace the canonical non-genesis chain and durable mempool.
    // The existing genesis row must exactly match the candidate genesis.
    void replace_chain_and_mempool(
        const std::vector<ledger::Block>& complete_chain,
        const std::vector<ledger::Transaction>& mempool_transactions);
    [[nodiscard]] std::vector<ledger::Block> load_blocks() const;

    void save_mempool_transaction(const ledger::Transaction& transaction);
    [[nodiscard]] std::vector<ledger::Transaction> load_mempool_transactions() const;

    void save_wallet(const WalletRecord& wallet);
    [[nodiscard]] std::optional<WalletRecord> load_wallet(const Address& address) const;
    [[nodiscard]] std::vector<std::pair<Address, PublicKey>> list_wallets() const;

    // A single checksummed snapshot row makes platform state (shared balances,
    // Match book and Flow pools) one atomic SQLite commit. expected_revision is
    // an optimistic concurrency guard against stale concurrent processes.
    [[nodiscard]] std::optional<FinancialStateRecord> load_financial_state() const;
    [[nodiscard]] std::uint64_t save_financial_state(const Bytes& payload,
                                                      std::uint64_t expected_revision);

    // A single checksummed Raft snapshot atomically stores term, vote, log,
    // commit index and applied index. This is intentionally separate from the
    // financial platform snapshot and protected by its own optimistic revision.
    [[nodiscard]] std::optional<RaftStateRecord> load_raft_state() const;
    [[nodiscard]] std::uint64_t save_raft_state(const Bytes& payload,
                                                std::uint64_t expected_revision);
    void save_raft_snapshot(std::uint64_t snapshot_index, const Bytes& payload);
    [[nodiscard]] std::optional<Bytes> load_raft_snapshot(std::uint64_t snapshot_index) const;
    void prune_raft_snapshots_before(std::uint64_t snapshot_index);
    // Durably stage InstallSnapshot chunks before acknowledging their offsets.
    // A single transfer is staged per node; a new metadata tuple replaces the
    // old partial transfer only when its first chunk (offset zero) arrives.
    [[nodiscard]] std::optional<consensus::RaftSnapshotTransfer> load_raft_snapshot_staging() const;
    void append_raft_snapshot_staging_chunk(const consensus::RaftSnapshotTransfer& transfer,
                                             std::uint64_t offset,
                                             const Bytes& chunk);
    void clear_raft_snapshot_staging();
    [[nodiscard]] std::uint64_t replace_chain_mempool_and_save_raft_snapshot(
        const std::vector<ledger::Block>& complete_chain,
        const std::vector<ledger::Transaction>& mempool_transactions,
        const Bytes& raft_payload,
        std::uint64_t expected_raft_revision,
        std::uint64_t snapshot_index,
        const Bytes& application_snapshot);

private:
    [[nodiscard]] std::uint64_t append_blocks_and_replace_mempool_impl(
        const std::vector<ledger::Block>& blocks,
        const std::vector<ledger::Transaction>& mempool_transactions,
        const Bytes* raft_payload,
        std::uint64_t expected_raft_revision);
    void restrict_file_permissions() const;

    std::string path_;
    sqlite3* db_{nullptr};
};

} // namespace feinstat::storage
