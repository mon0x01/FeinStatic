#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/crypto/crypto.hpp"

namespace feinstat::ledger {

class Wallet {
public:
    static Wallet generate();
    static Wallet from_secret(const SecretKey& secret_key);

    Wallet(const Wallet&) = delete;
    Wallet& operator=(const Wallet&) = delete;
    Wallet(Wallet&& other) noexcept;
    Wallet& operator=(Wallet&& other) noexcept;
    ~Wallet() noexcept;

    [[nodiscard]] const Address& address() const noexcept { return address_; }
    [[nodiscard]] const PublicKey& public_key() const noexcept { return public_key_; }
    [[nodiscard]] const SecretKey& secret_key() const noexcept { return secret_key_; }

private:
    Wallet(SecretKey secret_key, PublicKey public_key);

    SecretKey secret_key_{};
    PublicKey public_key_{};
    Address address_;
};

} // namespace feinstat::ledger
