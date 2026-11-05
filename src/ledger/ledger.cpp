#include "feinstat/ledger/ledger.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <utility>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace feinstat::ledger {

Ledger::Ledger(storage::SqliteStore store, Chain chain, Mempool mempool)
    : store_(std::move(store)), chain_(std::move(chain)), mempool_(std::move(mempool)) {}

Ledger Ledger::initialize(const std::string& database_path,
                          const std::vector<GenesisAllocation>& allocations,
                          std::uint32_t difficulty,
                          Amount block_reward) {
    storage::SqliteStore store(database_path);
    store.initialize_schema();
    if (!store.load_blocks().empty()) {
        throw std::runtime_error("database already contains a ledger; open it instead of initializing");
    }
    Chain chain(allocations, difficulty, block_reward);
    store.save_genesis(chain.blocks().front());
    return Ledger(std::move(store), std::move(chain));
}

Ledger Ledger::open(const std::string& database_path,
                    std::uint32_t difficulty,
                    Amount block_reward) {
    // Opening is intentionally non-creating: a typo must not silently leave
    // behind a new empty database that looks like an attempted recovery.
    if (database_path != ":memory:" && !std::filesystem::is_regular_file(database_path)) {
        throw std::runtime_error("ledger database does not exist or is not a regular file: " + database_path);
    }
    storage::SqliteStore store(database_path);
    store.initialize_schema();
    const auto blocks = store.load_blocks();
    if (blocks.empty()) throw std::runtime_error("database has no genesis block; initialize it first");
    Chain chain = Chain::from_blocks(blocks, difficulty, block_reward);
    Mempool mempool;
    for (const auto& transaction : store.load_mempool_transactions()) {
        std::string error;
        if (!mempool.add(transaction, chain, &error)) {
            throw std::runtime_error("persisted mempool contains an invalid transaction: " + error);
        }
    }
    return Ledger(std::move(store), std::move(chain), std::move(mempool));
}

Wallet Ledger::create_wallet(const std::string& passphrase) {
    Wallet wallet = Wallet::generate();
    register_wallet(wallet, passphrase);
    return wallet;
}

void Ledger::register_wallet(const Wallet& wallet, const std::string& passphrase) {
    storage::WalletRecord record;
    record.address = wallet.address();
    record.public_key = wallet.public_key();
    record.encrypted_secret = crypto::encrypt_secret(wallet.secret_key(), passphrase);
    store_.save_wallet(record);
}

Wallet Ledger::unlock_wallet(const Address& address, const std::string& passphrase) const {
    const auto record = store_.load_wallet(address);
    if (!record.has_value()) throw std::runtime_error("wallet is not stored in this ledger");
    SecretKey secret = crypto::decrypt_secret(record->encrypted_secret, passphrase);
    try {
        Wallet wallet = Wallet::from_secret(secret);
        OPENSSL_cleanse(secret.data(), secret.size());
        if (wallet.address() != record->address || wallet.public_key() != record->public_key) {
            throw std::runtime_error("wallet key material does not match stored public identity");
        }
        return wallet;
    } catch (...) {
        OPENSSL_cleanse(secret.data(), secret.size());
        throw;
    }
}

std::vector<std::pair<Address, PublicKey>> Ledger::wallets() const {
    return store_.list_wallets();
}

Transaction Ledger::create_transfer(const Wallet& sender,
                                    const Address& recipient,
                                    Amount amount,
                                    Amount fee) const {
    const auto records = store_.list_wallets();
    bool known_wallet = false;
    for (const auto& [address, public_key] : records) {
        if (address == sender.address() && public_key == sender.public_key()) {
            known_wallet = true;
            break;
        }
    }
    if (!known_wallet) throw std::runtime_error("sender wallet is not registered in this ledger");
    const auto current_nonce = mempool_.next_nonce(sender.address(), chain_.nonce_of(sender.address()));
    if (current_nonce == UINT64_MAX) throw std::overflow_error("sender nonce exhausted");
    return Transaction::create_signed(sender, recipient, amount, fee, current_nonce + 1);
}

bool Ledger::submit(const Transaction& transaction, std::string* error) {
    // Validate a candidate first. Persist it before publishing it in memory so
    // a failed SQLite commit cannot leave a transaction visible only in RAM.
    Mempool candidate = mempool_;
    if (!candidate.add(transaction, chain_, error)) return false;
    store_.save_mempool_transaction(transaction);
    mempool_ = std::move(candidate);
    return true;
}

bool Ledger::can_submit(const Transaction& transaction, std::string* error) const {
    Mempool candidate = mempool_;
    return candidate.add(transaction, chain_, error);
}

bool Ledger::has_pending_transaction(const std::string& transaction_id) const {
    return mempool_.contains(transaction_id);
}

std::optional<ConfirmedTransaction> Ledger::find_confirmed_transaction(
    const std::string& transaction_id) const {
    return chain_.find_transaction(transaction_id);
}

bool Ledger::owns_wallet(const Wallet& wallet) const {
    const auto records = store_.list_wallets();
    return std::any_of(records.begin(), records.end(), [&](const auto& record) {
        return record.first == wallet.address() && record.second == wallet.public_key();
    });
}

Block Ledger::mine(const Address& miner) {
    if (!crypto::is_valid_address(miner)) throw std::invalid_argument("invalid miner address");
    Block block = chain_.make_next_block(mempool_.transactions(), miner);
    // Prepare the complete next in-memory chain before the durable commit. If
    // validation or allocation fails, neither durable nor live state advances.
    Chain updated_chain = chain_;
    updated_chain.append(block);
    const Mempool empty_mempool;
    store_.append_blocks_and_replace_mempool({block}, empty_mempool.transactions());
    chain_ = std::move(updated_chain);
    mempool_ = empty_mempool;
    return block;
}

void Ledger::accept_blocks(const std::vector<Block>& blocks) {
    if (blocks.empty()) throw std::invalid_argument("peer block batch cannot be empty");

    // Stage the complete chain transition first. A malformed later block must
    // not leave earlier blocks from the same network batch committed.
    Chain candidate_chain = chain_;
    for (const auto& block : blocks) candidate_chain.append(block);

    // Revalidate the current durable mempool against the candidate chain
    // before committing. Transactions included in the imported blocks, replayed
    // transactions, and transactions invalidated by new nonces are discarded.
    Mempool candidate_mempool;
    for (const auto& transaction : mempool_.transactions()) {
        const auto id = transaction.id();
        const std::string id_text = crypto::hex(id.data(), id.size());
        if (candidate_chain.transaction_ids().contains(id_text)) continue;
        (void)candidate_mempool.add(transaction, candidate_chain, nullptr);
    }

    // One SQLite transaction for the whole batch. The in-memory state changes
    // only after the durable append succeeds.
    store_.append_blocks_and_replace_mempool(blocks, candidate_mempool.transactions());
    chain_ = std::move(candidate_chain);
    mempool_ = std::move(candidate_mempool);
}


std::uint64_t Ledger::commit_raft_application(
    const std::vector<Block>& blocks,
    const Bytes& raft_snapshot,
    std::uint64_t expected_raft_revision) {
    // Validate and stage all deterministic Ledger effects before the SQL
    // transaction. A bad consensus command must not partially alter memory or
    // durable state. The Raft snapshot is committed with those effects below.
    Chain candidate_chain = chain_;
    for (const auto& block : blocks) candidate_chain.append(block);

    Mempool candidate_mempool = mempool_;
    if (!blocks.empty()) {
        candidate_mempool = Mempool{};
        for (const auto& pending : mempool_.transactions()) {
            const Hash hash = pending.id();
            const std::string id = crypto::hex(hash.data(), hash.size());
            if (candidate_chain.transaction_ids().contains(id)) continue;
            (void)candidate_mempool.add(pending, candidate_chain, nullptr);
        }
    }

    const std::uint64_t next_revision = store_.append_blocks_mempool_and_save_raft_state(
        blocks, candidate_mempool.transactions(), raft_snapshot, expected_raft_revision);
    chain_ = std::move(candidate_chain);
    mempool_ = std::move(candidate_mempool);
    return next_revision;
}


std::uint64_t Ledger::install_raft_snapshot(
    const std::vector<Block>& complete_chain,
    const Bytes& raft_state,
    std::uint64_t expected_raft_revision,
    std::uint64_t snapshot_index,
    const Bytes& application_snapshot) {
    if (complete_chain.empty()) throw std::invalid_argument("Raft application snapshot contains no Ledger blocks");
    Chain candidate_chain = Chain::from_blocks(complete_chain, chain_.difficulty(), chain_.block_reward());
    if (candidate_chain.blocks().front().serialize() != chain_.blocks().front().serialize()) {
        throw std::runtime_error("Raft application snapshot belongs to a different Ledger genesis");
    }
    const Mempool candidate_mempool;
    const std::uint64_t next_revision = store_.replace_chain_mempool_and_save_raft_snapshot(
        candidate_chain.blocks(), candidate_mempool.transactions(), raft_state,
        expected_raft_revision, snapshot_index, application_snapshot);
    chain_ = std::move(candidate_chain);
    mempool_ = candidate_mempool;
    return next_revision;
}


ChainAdoptionResult Ledger::consider_chain(
    const std::vector<Block>& complete_chain,
    const std::vector<std::pair<std::string, std::uint64_t>>& protected_transactions) {
    if (complete_chain.empty()) throw std::invalid_argument("candidate chain cannot be empty");

    // Replay every candidate block before it is allowed to influence local
    // state. Chain::from_blocks checks genesis canonicality, links, PoW,
    // signatures, balances, nonces, supply and transaction replay.
    Chain candidate_chain = Chain::from_blocks(complete_chain, chain_.difficulty(), chain_.block_reward());
    const auto comparison = consensus::DeterministicForkChoice::compare(chain_, candidate_chain);

    ChainAdoptionResult result;
    result.previous_height = chain_.height();
    result.resulting_height = chain_.height();
    result.common_ancestor_height = comparison.common_ancestor_height;
    result.same_chain = comparison.same_chain;
    if (comparison.preference != consensus::ChainPreference::AdoptCandidate) return result;

    // Transactions already pending locally retain priority. Transactions from
    // the displaced branch are reconsidered in original block/transaction
    // order after the candidate chain is known. Conflicts and transactions
    // rendered invalid by the new canonical history are dropped.
    Mempool candidate_mempool;
    std::size_t dropped_existing_pending = 0;
    for (const auto& transaction : mempool_.transactions()) {
        const Hash hash = transaction.id();
        const std::string id = crypto::hex(hash.data(), hash.size());
        if (candidate_chain.transaction_ids().contains(id)) continue;
        if (!candidate_mempool.add(transaction, candidate_chain, nullptr)) {
            ++dropped_existing_pending;
        }
    }

    // Protect Bridge history across reorganization. Height-pinned deposits
    // and confirmed withdrawals must remain at the same canonical height.
    // A pending withdrawal (height zero) may either be confirmed on the new
    // branch or remain in the reconciled durable mempool; silently dropping
    // its exact signed transaction would strand the off-chain debit.
    for (const auto& [transaction_id, expected_height] : protected_transactions) {
        const auto confirmed = candidate_chain.find_transaction(transaction_id);
        if (confirmed) {
            if (expected_height != 0 && confirmed->block_height != expected_height) {
                throw std::runtime_error("candidate reorganization would move an externally referenced transaction to a different block height: " +
                                         transaction_id);
            }
            continue;
        }
        if (expected_height == 0 && candidate_mempool.contains(transaction_id)) continue;
        throw std::runtime_error("candidate reorganization would orphan an externally referenced transaction: " +
                                 transaction_id);
    }

    std::size_t requeued = 0;
    std::size_t dropped_detached = 0;
    const std::size_t first_detached = static_cast<std::size_t>(comparison.common_ancestor_height) + 1;
    for (std::size_t index = first_detached; index < chain_.blocks().size(); ++index) {
        for (const auto& transaction : chain_.blocks()[index].transactions) {
            const Hash hash = transaction.id();
            const std::string id = crypto::hex(hash.data(), hash.size());
            if (candidate_chain.transaction_ids().contains(id)) continue;
            if (candidate_mempool.add(transaction, candidate_chain, nullptr)) ++requeued;
            else ++dropped_detached;
        }
    }

    // The database replacement, including mempool reconciliation, is one
    // SQLite transaction. Publish the candidate in memory only after commit.
    store_.replace_chain_and_mempool(candidate_chain.blocks(), candidate_mempool.transactions());
    chain_ = std::move(candidate_chain);
    mempool_ = std::move(candidate_mempool);

    result.adopted = true;
    result.resulting_height = chain_.height();
    result.requeued_transactions = requeued;
    result.dropped_detached_transactions = dropped_detached + dropped_existing_pending;
    return result;
}

} // namespace feinstat::ledger
