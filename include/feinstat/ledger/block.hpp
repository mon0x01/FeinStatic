#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/ledger/transaction.hpp"

#include <vector>

namespace feinstat::ledger {

struct GenesisAllocation {
    Address address;
    Amount amount{0};
};

struct Block {
    std::uint64_t height{0};
    std::uint64_t timestamp{0};
    Hash previous_hash{};
    std::uint64_t nonce{0};
    std::uint32_t difficulty{0};
    Address miner;
    Amount reward{0};
    std::vector<GenesisAllocation> genesis_allocations;
    std::vector<Transaction> transactions;
    Hash hash{};

    [[nodiscard]] Hash calculate_hash() const;
    [[nodiscard]] bool has_valid_proof_of_work() const;
    void mine(std::uint32_t target_difficulty);
    [[nodiscard]] Bytes serialize() const;
    static Block deserialize(const Bytes& data);

    static Block make_genesis(const std::vector<GenesisAllocation>& allocations);
};

} // namespace feinstat::ledger
