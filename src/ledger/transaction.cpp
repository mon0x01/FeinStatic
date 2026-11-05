#include "feinstat/ledger/transaction.hpp"

#include "feinstat/common/bytes.hpp"
#include "feinstat/crypto/crypto.hpp"

#include <stdexcept>

namespace feinstat::ledger {

Bytes Transaction::signing_bytes() const {
    ByteWriter writer;
    writer.u8(1); // canonical transaction encoding version
    writer.string(sender);
    writer.string(recipient);
    writer.string(asset);
    writer.u64(amount);
    writer.u64(fee);
    writer.u64(nonce);
    writer.fixed(public_key);
    return std::move(writer).take();
}

Bytes Transaction::serialize() const {
    auto bytes = signing_bytes();
    bytes.insert(bytes.end(), signature.begin(), signature.end());
    return bytes;
}

Hash Transaction::id() const {
    return crypto::sha256(serialize());
}

bool Transaction::verify_signature() const {
    if (!crypto::is_valid_address(sender) || !crypto::is_valid_address(recipient)) return false;
    if (sender != crypto::address_from_public_key(public_key)) return false;
    if (asset != kNativeAsset || amount == 0 || nonce == 0) return false;
    if (amount > UINT64_MAX - fee) return false;
    return crypto::verify_ed25519(public_key, signing_bytes(), signature);
}

Transaction Transaction::create_signed(const Wallet& wallet,
                                        const Address& recipient_address,
                                        Amount transfer_amount,
                                        Amount transaction_fee,
                                        std::uint64_t transaction_nonce,
                                        std::string asset_id) {
    if (!crypto::is_valid_address(recipient_address)) throw std::invalid_argument("invalid recipient address");
    if (transfer_amount == 0) throw std::invalid_argument("transfer amount must be positive");
    if (transfer_amount > UINT64_MAX - transaction_fee) throw std::overflow_error("amount plus fee overflows");
    if (transaction_nonce == 0) throw std::invalid_argument("transaction nonce must be positive");
    if (asset_id != kNativeAsset) throw std::invalid_argument("unsupported asset identifier");

    Transaction transaction;
    transaction.sender = wallet.address();
    transaction.recipient = recipient_address;
    transaction.asset = std::move(asset_id);
    transaction.amount = transfer_amount;
    transaction.fee = transaction_fee;
    transaction.nonce = transaction_nonce;
    transaction.public_key = wallet.public_key();
    transaction.signature = crypto::sign_ed25519(wallet.secret_key(), transaction.signing_bytes());
    return transaction;
}

Transaction Transaction::deserialize(const Bytes& data) {
    ByteReader reader(data);
    if (reader.u8() != 1) throw std::runtime_error("unsupported transaction encoding version");
    Transaction result;
    result.sender = reader.string(128);
    result.recipient = reader.string(128);
    result.asset = reader.string(16);
    result.amount = reader.u64();
    result.fee = reader.u64();
    result.nonce = reader.u64();
    result.public_key = reader.fixed<32>();
    result.signature = reader.fixed<64>();
    if (!reader.empty()) throw std::runtime_error("trailing bytes in transaction encoding");
    return result;
}

} // namespace feinstat::ledger
