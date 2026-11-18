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
    Height last_active_height{0}; // last height at which the validator was active
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
    // Epoch length in heights. Epoch transitions (committee rotation and
    // inactivity leak) are applied at epoch boundaries (testnet-labelled,
    // not production policy).
    Height epoch_length{1000};
    // Number of epochs a bonded validator may be inactive before the
    // inactivity leak reduces its stake (testnet-labelled).
    std::uint32_t inactivity_leak_after_epochs{4};
    // Inactivity leak: fraction of bonded stake removed per epoch of
    // inactivity beyond the threshold, in basis points (100 = 1%)
    // (testnet-labelled, not production policy).
    std::uint32_t inactivity_leak_fraction_basis_points{100};
};

// Epoch bookkeeping returned by epoch_info() / transition_epoch().
struct EpochInfo {
    Height current_epoch{0};         // current_epoch = height / epoch_length
    Height epoch_length{0};          // configured epoch length
    Height last_transition_height{0}; // height of the last applied epoch transition
};

// Slashing evidence type. Evidence-based slashing only (ADR-0003): no
// discretionary slashing, no privileged emergency slashing authority.
enum class EvidenceType : std::uint8_t {
    equivocation, // same consensus key signed two conflicting votes at the
                  // same height and round
};

// Two conflicting votes signed by one validator's consensus key at the same
// height and round. Byte-inequality of vote_a/vote_b establishes the conflict.
struct EquivocationEvidence {
    ConsensusKey consensus_key{}; // equivocating validator's consensus key
    Height height{};              // height of the conflicting votes
    std::uint64_t round{};        // round of the conflicting votes
    Bytes vote_a{};               // serialized conflicting vote A
    Bytes vote_b{};               // serialized conflicting vote B
    Signature signature_a{};      // validator's signature over vote A
    Signature signature_b{};      // validator's signature over vote B
};

// Slashing evidence submitted for verification and deterministic slashing.
struct SlashingEvidence {
    EvidenceType type{EvidenceType::equivocation};
    EquivocationEvidence equivocation{};
};

// A slashing event applied to a validator (observability only, not
// consensus-critical).
struct SlashingEvent {
    OperatorKey operator_key{};
    ConsensusKey consensus_key{};
    Amount amount{0};   // amount slashed
    Height height{0};   // height at which slashing was applied
    std::string reason; // human-readable, non-consensus-critical
    feinstat::Hash evidence_hash{}; // canonical hash of the submitted evidence
};

// Outcome of submit_slashing_evidence().
struct SlashingOutcome {
    bool accepted{false};        // evidence was valid and slashing applied
    Amount slashed_amount{0};    // amount slashed from bonded stake
    ValidatorStatus new_status{ValidatorStatus::Registered};
    std::string reason; // human-readable, non-consensus-critical
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
    evidence_duplicate,    // identical evidence already submitted
    evidence_invalid,      // malformed or non-equivocation evidence
    signature_verification_failed,
    validator_not_bonded,  // validator is not slashable (not bonded/unbonding)
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
