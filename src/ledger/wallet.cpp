#include "feinstat/ledger/wallet.hpp"

#include <openssl/crypto.h>
#include <utility>

namespace feinstat::ledger {

Wallet::Wallet(SecretKey secret_key, PublicKey public_key)
    : secret_key_(secret_key), public_key_(public_key),
      address_(crypto::address_from_public_key(public_key_)) {
    // The by-value constructor parameter is an additional stack copy of the
    // private key. Clear it after transferring the bytes into this Wallet.
    OPENSSL_cleanse(secret_key.data(), secret_key.size());
}

Wallet Wallet::generate() {
    auto pair = crypto::generate_ed25519_keypair();
    Wallet wallet(pair.secret_key, pair.public_key);
    OPENSSL_cleanse(pair.secret_key.data(), pair.secret_key.size());
    return wallet;
}

Wallet::Wallet(Wallet&& other) noexcept
    : secret_key_(other.secret_key_), public_key_(other.public_key_),
      address_(std::move(other.address_)) {
    OPENSSL_cleanse(other.secret_key_.data(), other.secret_key_.size());
    other.public_key_.fill(0);
}

Wallet& Wallet::operator=(Wallet&& other) noexcept {
    if (this != &other) {
        OPENSSL_cleanse(secret_key_.data(), secret_key_.size());
        secret_key_ = other.secret_key_;
        public_key_ = other.public_key_;
        address_ = std::move(other.address_);
        OPENSSL_cleanse(other.secret_key_.data(), other.secret_key_.size());
        other.public_key_.fill(0);
    }
    return *this;
}

Wallet::~Wallet() noexcept {
    OPENSSL_cleanse(secret_key_.data(), secret_key_.size());
    public_key_.fill(0);
}

Wallet Wallet::from_secret(const SecretKey& secret_key) {
    return Wallet(secret_key, crypto::derive_ed25519_public_key(secret_key));
}

} // namespace feinstat::ledger
