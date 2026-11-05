#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/ledger/wallet.hpp"

namespace feinstat::ledger {

struct Transaction {
    Address sender;
    Address recipient;
    std::string asset{kNativeAsset};
    Amount amount{0};
    Amount fee{0};
    std::uint64_t nonce{0};
    PublicKey public_key{};
    Signature signature{};

    [[nodiscard]] Bytes signing_bytes() const;
    [[nodiscard]] Bytes serialize() const;
    [[nodiscard]] Hash id() const;
    [[nodiscard]] bool verify_signature() const;

    static Transaction create_signed(const Wallet& wallet,
                                     const Address& recipient,
                                     Amount amount,
                                     Amount fee,
                                     std::uint64_t nonce,
                                     std::string asset = kNativeAsset);
    static Transaction deserialize(const Bytes& data);
};

} // namespace feinstat::ledger
