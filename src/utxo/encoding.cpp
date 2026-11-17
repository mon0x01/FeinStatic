#include "feinstat/utxo/types.hpp"

#include "feinstat/crypto/crypto.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace feinstat::utxo {

namespace {

void encode_outpoint(ByteWriter& writer, const Outpoint& outpoint) {
    writer.raw(outpoint.transaction_hash.data(), outpoint.transaction_hash.size());
    writer.u32(outpoint.output_index);
}

void encode_output(ByteWriter& writer, const Output& output) {
    if (output.amount > kMaxAmount) {
        throw std::length_error("output amount exceeds maximum");
    }
    writer.u64(output.amount);
    writer.raw(output.spending_key.data(), output.spending_key.size());
}

void encode_input(ByteWriter& writer, const Input& input) {
    encode_outpoint(writer, input.outpoint);
    writer.raw(input.witness.data(), input.witness.size());
}

Outpoint decode_outpoint(ByteReader& reader) {
    Outpoint outpoint;
    outpoint.transaction_hash = reader.fixed<32>();
    outpoint.output_index = reader.u32();
    return outpoint;
}

Output decode_output(ByteReader& reader) {
    Output output;
    output.amount = reader.u64();
    if (output.amount > kMaxAmount) {
        throw std::runtime_error("output amount exceeds maximum");
    }
    output.spending_key = reader.fixed<32>();
    return output;
}

Input decode_input(ByteReader& reader) {
    Input input;
    input.outpoint = decode_outpoint(reader);
    input.witness = reader.fixed<64>();
    return input;
}

} // namespace

Bytes encode_transaction(const Transaction& transaction) {
    ByteWriter writer;
    writer.u8(0x01); // transaction format version
    if (transaction.inputs.size() > kMaxInputsPerTransaction) {
        throw std::length_error("too many inputs");
    }
    if (transaction.outputs.size() > kMaxOutputsPerTransaction) {
        throw std::length_error("too many outputs");
    }
    writer.u32(static_cast<std::uint32_t>(transaction.inputs.size()));
    for (const auto& input : transaction.inputs) {
        encode_input(writer, input);
    }
    writer.u32(static_cast<std::uint32_t>(transaction.outputs.size()));
    for (const auto& output : transaction.outputs) {
        encode_output(writer, output);
    }
    return std::move(writer).take();
}

Bytes encode_transaction_for_digest(const Transaction& transaction) {
    Transaction sanitized = transaction;
    for (auto& input : sanitized.inputs) {
        input.witness.fill(0x00);
    }
    return encode_transaction(sanitized);
}

Transaction decode_transaction(const Bytes& data) {
    ByteReader reader(data);
    const std::uint8_t version = reader.u8();
    if (version != 0x01) {
        throw std::runtime_error("unsupported transaction format version");
    }
    Transaction transaction;
    const std::uint32_t input_count = reader.u32();
    if (input_count > kMaxInputsPerTransaction) {
        throw std::runtime_error("too many inputs");
    }
    transaction.inputs.reserve(input_count);
    for (std::uint32_t i = 0; i < input_count; ++i) {
        transaction.inputs.push_back(decode_input(reader));
    }
    const std::uint32_t output_count = reader.u32();
    if (output_count > kMaxOutputsPerTransaction) {
        throw std::runtime_error("too many outputs");
    }
    transaction.outputs.reserve(output_count);
    for (std::uint32_t i = 0; i < output_count; ++i) {
        transaction.outputs.push_back(decode_output(reader));
    }
    if (!reader.empty()) {
        throw std::runtime_error("trailing bytes in canonical transaction");
    }
    return transaction;
}

TransactionId transaction_digest(const ChainId& chain_id,
                                 const Transaction& transaction) {
    const Bytes encoding = encode_transaction_for_digest(transaction);
    ByteWriter writer;
    // Domain separation: chain identifier || domain tag || canonical encoding.
    writer.raw(chain_id.data(), chain_id.size());
    writer.string(kTransactionDomain);
    writer.raw(encoding.data(), encoding.size());
    return crypto::sha256(writer.data());
}

} // namespace feinstat::utxo
