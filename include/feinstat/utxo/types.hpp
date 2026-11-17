#pragma once

#include "feinstat/common/bytes.hpp"
#include "feinstat/common/types.hpp"

#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

namespace feinstat::utxo {

// Network / chain identifier used for domain separation and replay
// protection across forks (D-036 fork protection requirements).
using ChainId = Hash;

// Unique reference to a spendable transaction output.
struct Outpoint {
    Hash transaction_hash{};
    std::uint32_t output_index{0};

    [[nodiscard]] bool operator==(const Outpoint&) const = default;
    [[nodiscard]] bool operator!=(const Outpoint&) const = default;
    [[nodiscard]] bool operator<(const Outpoint& other) const noexcept {
        return std::tie(transaction_hash, output_index) <
               std::tie(other.transaction_hash, other.output_index);
    }
};

// A spendable output: an amount and the public key authorized to spend it.
// The spending condition for the initial core protocol is a single ed25519
// signature over the domain-separated transaction digest.
struct Output {
    Amount amount{0};
    PublicKey spending_key{};
};

// A transaction input consumes a single outpoint and carries a witness
// signature authorizing the consumption.
struct Input {
    Outpoint outpoint{};
    Signature witness{};
};

// A native FST payment transaction: consumes inputs, produces outputs.
// Canonical encoding is big-endian and deterministic. The transaction
// identifier is a domain-separated hash bound to a ChainId, providing fork
// replay protection.
struct Transaction {
    std::vector<Input> inputs{};
    std::vector<Output> outputs{};
};

using TransactionId = Hash;

// Resource limits for the initial core protocol (testnet-labelled values;
// not production policy until justified by simulation).
inline constexpr std::uint32_t kMaxInputsPerTransaction = 1'000U;
inline constexpr std::uint32_t kMaxOutputsPerTransaction = 1'000U;
inline constexpr std::size_t kMaxTransactionSizeBytes = 100'000ULL;
inline constexpr Amount kMaxAmount = (Amount{1} << 62) - 1; // overflow guard

// Domain separation tag for transaction digests and signatures.
inline constexpr const char* kTransactionDomain = "FST-Transaction/v0";

// Canonical serialization of a transaction. Returns big-endian bytes with no
// length prefix on fixed-width fields.
[[nodiscard]] Bytes encode_transaction(const Transaction& transaction);

// Canonical serialization used for the transaction digest: identical to
// encode_transaction except all witness signatures are blanked (zeroed), so
// the digest a signature covers is independent of the signature value itself.
[[nodiscard]] Bytes encode_transaction_for_digest(const Transaction& transaction);

// Canonical deserialization. Throws std::runtime_error on truncated or
// malformed data.
[[nodiscard]] Transaction decode_transaction(const Bytes& data);

// Domain-separated transaction digest, bound to a chain identifier. Used both
// as the transaction identifier and as the message covered by input witness
// signatures, providing replay protection across chains/forks.
[[nodiscard]] TransactionId transaction_digest(const ChainId& chain_id,
                                               const Transaction& transaction);

// Convenience: transaction identifier for a chain.
[[nodiscard]] inline TransactionId transaction_id(const ChainId& chain_id,
                                                  const Transaction& transaction) {
    return transaction_digest(chain_id, transaction);
}

} // namespace feinstat::utxo
