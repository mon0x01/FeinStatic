#include "feinstat/ledger/block.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <limits>
#include <stdexcept>

namespace feinstat::ledger {
namespace {

Bytes header_bytes(const Block& block) {
    ByteWriter writer;
    writer.u8(1); // block encoding version
    writer.u64(block.height);
    writer.u64(block.timestamp);
    writer.fixed(block.previous_hash);
    writer.u64(block.nonce);
    writer.u32(block.difficulty);
    writer.string(block.miner);
    writer.u64(block.reward);

    if (block.genesis_allocations.size() > UINT32_MAX || block.transactions.size() > UINT32_MAX) {
        throw std::length_error("block contains too many entries");
    }
    writer.u32(static_cast<std::uint32_t>(block.genesis_allocations.size()));
    for (const auto& allocation : block.genesis_allocations) {
        writer.string(allocation.address);
        writer.u64(allocation.amount);
    }
    writer.u32(static_cast<std::uint32_t>(block.transactions.size()));
    for (const auto& transaction : block.transactions) {
        const auto id = transaction.id();
        writer.fixed(id);
    }
    return std::move(writer).take();
}

bool has_zero_prefix(const Hash& hash, std::uint32_t nibbles) {
    if (nibbles > 8) return false;
    for (std::uint32_t i = 0; i < nibbles; ++i) {
        const Byte value = hash[i / 2];
        const Byte nibble = (i % 2 == 0) ? static_cast<Byte>(value >> 4U)
                                         : static_cast<Byte>(value & 0x0fU);
        if (nibble != 0) return false;
    }
    return true;
}

} // namespace

Hash Block::calculate_hash() const {
    return crypto::sha256(header_bytes(*this));
}

bool Block::has_valid_proof_of_work() const {
    if (difficulty > 8) return false;
    return hash == calculate_hash() && has_zero_prefix(hash, difficulty);
}

void Block::mine(std::uint32_t target_difficulty) {
    if (target_difficulty > 8) throw std::invalid_argument("PoW difficulty must be between 0 and 8 hex zeroes");
    difficulty = target_difficulty;
    nonce = 0;
    for (;;) {
        hash = calculate_hash();
        if (has_zero_prefix(hash, difficulty)) return;
        if (nonce == UINT64_MAX) throw std::overflow_error("proof-of-work nonce exhausted");
        ++nonce;
    }
}

Bytes Block::serialize() const {
    auto result = header_bytes(*this);
    ByteWriter writer;
    writer.raw(result.data(), result.size());
    for (const auto& transaction : transactions) writer.blob(transaction.serialize());
    writer.fixed(hash);
    return std::move(writer).take();
}

Block Block::deserialize(const Bytes& data) {
    ByteReader reader(data);
    if (reader.u8() != 1) throw std::runtime_error("unsupported block encoding version");
    Block result;
    result.height = reader.u64();
    result.timestamp = reader.u64();
    result.previous_hash = reader.fixed<32>();
    result.nonce = reader.u64();
    result.difficulty = reader.u32();
    result.miner = reader.string(128);
    result.reward = reader.u64();

    const auto allocation_count = reader.u32();
    if (allocation_count > 100'000) throw std::runtime_error("unreasonable genesis allocation count");
    result.genesis_allocations.reserve(allocation_count);
    for (std::uint32_t i = 0; i < allocation_count; ++i) {
        result.genesis_allocations.push_back({reader.string(128), reader.u64()});
    }

    const auto transaction_count = reader.u32();
    if (transaction_count > 10'000) throw std::runtime_error("unreasonable transaction count");
    result.transactions.reserve(transaction_count);
    // The header commits to transaction IDs, which are checked after the full records are decoded.
    std::vector<Hash> committed_ids;
    committed_ids.reserve(transaction_count);
    for (std::uint32_t i = 0; i < transaction_count; ++i) committed_ids.push_back(reader.fixed<32>());
    for (std::uint32_t i = 0; i < transaction_count; ++i) {
        // The serialized transaction bodies follow the committed transaction IDs.
        const auto transaction = Transaction::deserialize(reader.blob(4096));
        if (transaction.id() != committed_ids[i]) throw std::runtime_error("transaction ID does not match block commitment");
        result.transactions.push_back(transaction);
    }
    result.hash = reader.fixed<32>();
    if (!reader.empty()) throw std::runtime_error("trailing bytes in block encoding");
    return result;
}

Block Block::make_genesis(const std::vector<GenesisAllocation>& allocations) {
    Block genesis;
    genesis.height = 0;
    genesis.timestamp = 0;
    genesis.genesis_allocations = allocations;
    genesis.hash = genesis.calculate_hash();
    return genesis;
}

} // namespace feinstat::ledger
