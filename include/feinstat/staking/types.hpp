#pragma once

#include "feinstat/common/types.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace feinstat::staking {

// Operator key: authorizes staking actions (bond/unbond/withdraw) for a
// validator. Separate from the consensus key and from financial ownership
// (ADR-0003: validator identity separate from node identity and ownership).
using OperatorKey = PublicKey;

// Consensus key: used by CometBFT for voting. May differ from the operator
// key and supports independent rotation.
using ConsensusKey = PublicKey;

// Delegator: a participant who bonds stake to a validator (delegation).
using Delegator = PublicKey;

// Height in the FST chain.
using Height = std::uint64_t;

// Voting power unit: one unit per bonded atom by default (1:1 conversion,
// testnet-labelled until justified by simulation).
using VotingPower = std::uint64_t;

enum class ValidatorStatus : std::uint8_t {
    Registered,   // registered, not yet bonding
    Bonded,       // actively validating / voting
    Unbonding,    // unstaking, stake locked until release height
    Unbonded,     // no active stake
};

struct Validator {
    OperatorKey operator_key{};
    ConsensusKey consensus_key{};
    Amount bonded_stake{0};
    VotingPower voting_power{0};
    ValidatorStatus status{ValidatorStatus::Registered};
    Height last_updated{0};
    std::uint64_t nonce{0}; // prevents replay of staking transactions
};

struct UnbondingEntry {
    OperatorKey operator_key{};
    Amount amount{0};
    Height release_height{0};
};

struct Delegation {
    Delegator delegator{};
    OperatorKey operator_key{};
    Amount amount{0};
};

struct ValidatorUpdate {
    ConsensusKey consensus_key{};
    VotingPower voting_power{0};
};

// Snapshot of the active validator set fed to CometBFT (activation-height
// semantics applied by the ABCI application).
struct ValidatorSet {
    Height height{0};
    std::vector<ValidatorUpdate> updates{}; // sorted by consensus key
    Amount total_bonded_stake{0};
    VotingPower total_voting_power{0};
};

struct StakeParams {
    // Minimum bond to become a validator (testnet-labelled, not production policy).
    Amount min_bond{1ULL * kAtomsPerCoin};
    // Maximum number of active validators (testnet-labelled).
    std::uint32_t max_validators{100};
    // Unbonding period in heights (testnet-labelled).
    Height unbonding_period{1000};
    // Slash fraction for equivocation (basis points of bonded stake).
    std::uint32_t equivocation_slash_basis_points{500}; // 5%
};

enum class StakeErrorCode {
    ok,
    validator_not_found,
    validator_already_registered,
    insufficient_bonded_stake,
    amount_zero,
    amount_exceeds_max,
    unbonding_not_available,
    invalid_consensus_key,
    duplicate_delegation,
};

class StakeError : public std::runtime_error {
public:
    explicit StakeError(StakeErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] StakeErrorCode code() const noexcept { return code_; }
private:
    StakeErrorCode code_;
};

} // namespace feinstat::staking
