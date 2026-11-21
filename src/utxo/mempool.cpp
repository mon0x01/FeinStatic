#include "feinstat/utxo/mempool.hpp"

#include "feinstat/common/bytes.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace feinstat::utxo {

namespace {

// Persistence format: magic (8 bytes), format version (u32), transaction
// count (u64), then per transaction: byte length (u64) + encoded transaction.
// Native integer encoding is used because save and load happen on the same
// node. Transactions are written sorted by id (std::map order) so the saved
// file is deterministic across saves.
constexpr std::array<Byte, 8> kMempoolFileMagic{{'F', 'S', 'T', 'M', 'E', 'M', '0', '1'}};
constexpr std::uint32_t kMempoolFileFormatVersion = 1;

// View adapter: a chain UTXO view with mempool-spent outpoints treated as
// spent, so pooled transactions cannot double-spend each other's inputs.
class MempoolAdaptingView : public UtxoView {
public:
    MempoolAdaptingView(const UtxoView& chain_view,
                        const std::map<Outpoint, TransactionId>& spent_by)
        : chain_view_(chain_view), spent_by_(spent_by) {}

    [[nodiscard]] bool contains(const Outpoint& outpoint) const override {
        return spent_by_.find(outpoint) == spent_by_.end() &&
               chain_view_.contains(outpoint);
    }

    [[nodiscard]] const Output& get(const Outpoint& outpoint) const override {
        return chain_view_.get(outpoint);
    }

private:
    const UtxoView& chain_view_;
    const std::map<Outpoint, TransactionId>& spent_by_;
};

std::size_t transaction_serialized_bytes(const Transaction& transaction) {
    return encode_transaction(transaction).size();
}

} // namespace

Mempool::Mempool(std::size_t max_transaction_count,
                 std::size_t max_total_bytes)
    : max_transaction_count_(max_transaction_count),
      max_total_bytes_(max_total_bytes) {}

bool Mempool::add(const ChainId& chain_id, const UtxoView& chain_view,
                  const Transaction& transaction, std::string* error) {
    const TransactionId tx_id = transaction_id(chain_id, transaction);

    if (entries_.find(tx_id) != entries_.end()) {
        if (error) *error = "transaction already in mempool";
        return false;
    }

    const std::size_t tx_bytes = transaction_serialized_bytes(transaction);
    if (tx_bytes > kMaxTransactionSizeBytes) {
        if (error) *error = "transaction exceeds maximum size";
        return false;
    }

    // Detect conflicts: inputs already spent by a pooled transaction.
    std::vector<TransactionId> conflicts;
    conflicts.reserve(transaction.inputs.size());
    for (const auto& input : transaction.inputs) {
        const auto it = spent_by_.find(input.outpoint);
        if (it != spent_by_.end()) {
            conflicts.push_back(it->second);
        }
    }
    std::sort(conflicts.begin(), conflicts.end());
    conflicts.erase(std::unique(conflicts.begin(), conflicts.end()),
                    conflicts.end());

    Amount new_fee = 0;
    bool replace = false;

    if (!conflicts.empty()) {
        // Replacement policy (RBF/nonce): a single conflicting pooled
        // transaction may be replaced only when the new transaction spends
        // the exact same input set and pays a strictly higher fee.
        if (conflicts.size() != 1) {
            if (error)
                *error = "transaction conflicts with multiple pooled transactions";
            return false;
        }
        const TransactionId replaced_id = conflicts[0];
        const Entry& replaced = entries_.at(replaced_id);
        if (!same_input_set(transaction, replaced.transaction)) {
            if (error)
                *error = "replacement must spend the same input set";
            return false;
        }
        // Validate the replacement against a view in which the replaced
        // transaction's inputs are available again, so its own inputs are
        // not treated as spent by itself.
        std::map<Outpoint, TransactionId> replacement_spent_by = spent_by_;
        for (const auto& input : replaced.transaction.inputs) {
            replacement_spent_by.erase(input.outpoint);
        }
        MempoolAdaptingView replacement_view(chain_view, replacement_spent_by);
        const auto result =
            validate_transaction(chain_id, transaction, replacement_view);
        if (!result.valid) {
            if (error) *error = result.error;
            return false;
        }
        if (result.fee <= replaced.fee) {
            if (error)
                *error = "replacement must pay a strictly higher fee";
            return false;
        }
        new_fee = result.fee;
        replace = true;
    } else {
        MempoolAdaptingView view(chain_view, spent_by_);
        const auto result = validate_transaction(chain_id, transaction, view);
        if (!result.valid) {
            if (error) *error = result.error;
            return false;
        }
        new_fee = result.fee;
    }

    if (replace) {
        remove(conflicts[0]);
    }

    // Record spent outpoints and store the transaction.
    for (const auto& input : transaction.inputs) {
        spent_by_.emplace(input.outpoint, tx_id);
    }
    Entry entry{transaction, tx_bytes, next_sequence_++, new_fee};
    entries_.emplace(tx_id, std::move(entry));
    insertion_order_.push_back(tx_id);
    total_bytes_ += tx_bytes;

    evict_to_limits();
    return true;
}

void Mempool::remove(const TransactionId& transaction_id) {
    const auto it = entries_.find(transaction_id);
    if (it == entries_.end()) {
        return;
    }
    // Copy everything needed before erasing the map node, so no reference
    // outlives the erase.
    Entry entry = it->second;
    entries_.erase(it);
    total_bytes_ -= entry.bytes;
    for (const auto& input : entry.transaction.inputs) {
        spent_by_.erase(input.outpoint);
    }
    insertion_order_.erase(
        std::remove(insertion_order_.begin(), insertion_order_.end(),
                    transaction_id),
        insertion_order_.end());
}

void Mempool::remove_spent(const Outpoint& outpoint) {
    const auto it = spent_by_.find(outpoint);
    if (it == spent_by_.end()) {
        return;
    }
    // Copy the identifier before erasing the node it references.
    const TransactionId transaction_id = it->second;
    remove(transaction_id);
}

std::vector<Transaction> Mempool::transactions() const {
    // Deterministic order: sorted by transaction id.
    std::vector<Transaction> result;
    result.reserve(entries_.size());
    for (const auto& [id, entry] : entries_) {
        (void)id;
        result.push_back(entry.transaction);
    }
    return result;
}

bool Mempool::contains(const TransactionId& transaction_id) const {
    return entries_.find(transaction_id) != entries_.end();
}

std::size_t Mempool::size() const noexcept {
    return entries_.size();
}

std::size_t Mempool::total_bytes() const noexcept {
    return total_bytes_;
}

std::size_t Mempool::max_transaction_count() const noexcept {
    return max_transaction_count_;
}

std::size_t Mempool::max_total_bytes() const noexcept {
    return max_total_bytes_;
}

bool Mempool::admits_outpoint(const Outpoint& outpoint) const {
    return spent_by_.find(outpoint) == spent_by_.end();
}

void Mempool::evict_to_limits() {
    while (entries_.size() > max_transaction_count_ ||
           total_bytes_ > max_total_bytes_) {
        if (insertion_order_.empty()) {
            break;
        }
        const TransactionId oldest = insertion_order_.front();
        insertion_order_.erase(insertion_order_.begin());
        remove(oldest);
    }
}

bool Mempool::same_input_set(const Transaction& a,
                             const Transaction& b) const noexcept {
    if (a.inputs.size() != b.inputs.size()) {
        return false;
    }
    std::vector<Outpoint> aa, bb;
    aa.reserve(a.inputs.size());
    bb.reserve(b.inputs.size());
    for (const auto& input : a.inputs) aa.push_back(input.outpoint);
    for (const auto& input : b.inputs) bb.push_back(input.outpoint);
    std::sort(aa.begin(), aa.end());
    std::sort(bb.begin(), bb.end());
    return aa == bb;
}

bool Mempool::save_to_disk(const std::string& path,
                           std::string* error) const {
    ByteWriter writer;
    writer.raw(kMempoolFileMagic.data(), kMempoolFileMagic.size());
    writer.u32(kMempoolFileFormatVersion);
    writer.u64(static_cast<std::uint64_t>(entries_.size()));
    for (const auto& [tx_id, entry] : entries_) {
        (void)tx_id;
        const Bytes tx_bytes = encode_transaction(entry.transaction);
        writer.u64(static_cast<std::uint64_t>(tx_bytes.size()));
        writer.raw(tx_bytes.data(), tx_bytes.size());
    }
    const Bytes data = std::move(writer).take();

    const std::string temp_path = path + ".tmp";
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = "cannot open mempool file for writing: " + path;
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.flush();
    if (!out) {
        if (error)
            *error = "failed writing mempool data to: " + path;
        return false;
    }
    out.close();

    // Atomic replace: rename on the same directory is atomic on POSIX.
    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        if (error)
            *error = "failed to finalize mempool file: " +
                     std::string(std::strerror(errno));
        return false;
    }
    return true;
}

std::size_t Mempool::load_from_disk(const std::string& path,
                                    const ChainId& chain_id,
                                    const UtxoView& chain_view,
                                    std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // No saved mempool yet (first startup). Not an error.
        return 0;
    }
    Bytes data((std::istreambuf_iterator<char>(in)),
               std::istreambuf_iterator<char>());
    if (in.bad()) {
        if (error)
            *error = "failed reading mempool file: " + path;
        return 0;
    }

    ByteReader reader(data);
    try {
        const std::array<Byte, 8> magic = reader.fixed<8>();
        if (magic != kMempoolFileMagic) {
            if (error)
                *error = "unsupported mempool file format";
            return 0;
        }
        const std::uint32_t version = reader.u32();
        if (version != kMempoolFileFormatVersion) {
            if (error)
                *error = "unsupported mempool format version";
            return 0;
        }
        const std::uint64_t count = reader.u64();

        std::size_t loaded = 0;
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t tx_len = reader.u64();
            const Bytes tx_bytes = reader.bytes(tx_len);
            const Transaction tx = decode_transaction(tx_bytes);
            std::string err;
            if (add(chain_id, chain_view, tx, &err)) {
                ++loaded;
            }
            // Transactions no longer valid on chain (inputs already spent)
            // are skipped during recovery.
        }
        return loaded;
    } catch (const std::exception& e) {
        if (error)
            *error = "corrupt mempool file: " + std::string(e.what());
        return 0;
    }
}

} // namespace feinstat::utxo
