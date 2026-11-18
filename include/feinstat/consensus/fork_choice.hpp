#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/ledger/chain.hpp"

#include <cstdint>
#include <vector>

namespace feinstat::consensus {

// FeinStatic currently uses one fixed PoW difficulty for every block in a
// chain. Consequently, valid-chain work is proportional to height. If the
// protocol later introduces variable difficulty, this score MUST be replaced
// with true cumulative work before that protocol can be enabled.
enum class ChainPreference : std::uint8_t {
    Equivalent = 0,
    KeepCurrent = 1,
    AdoptCandidate = 2
};

struct ChainComparison {
    ChainPreference preference{ChainPreference::Equivalent};
    std::uint64_t common_ancestor_height{0};
    bool same_chain{true};
};

class DeterministicForkChoice {
public:
    // Compare two independently validated chains sharing identical genesis
    // and consensus parameters. Higher height wins. At equal height, the
    // lexicographically smaller tip hash is the deterministic tie-break.
    // This is a local fork-choice rule, NOT Byzantine/distributed consensus.
    [[nodiscard]] static ChainComparison compare(const ledger::Chain& current,
                                                 const ledger::Chain& candidate);
};

//------------------------------------------------------------------------------
// CometBFT-aligned finality tracking.
//
// Per ADR-0004 / D-017, CometBFT (BFT) provides deterministic finality; the
// FST ABCI application does not implement a BFT consensus algorithm. Instead,
// the FST side tracks which heights CometBFT has finalized, detects forks
// (conflicting or stale blocks at or below the finalized height), and supports
// fork recovery at the application level. This is additive to, and aligned
// with, the CometBFT architecture: it defers BFT finality to CometBFT and only
// tracks/finality-monitors on the application side.
//------------------------------------------------------------------------------

enum class FinalityStatus : std::uint8_t {
    NotFinalized = 0, // height has not been finalized by a CometBFT commit
    Finalized   = 1,  // height finalized by a CometBFT commit
    ForkDetected = 2, // conflicting block observed at or below the finalized height
};

struct FinalityCheckpoint {
    std::uint64_t height{0};
    Hash app_hash{}; // zero hash = not yet set
};

// Tracks finality checkpoints reported by CometBFT via the ABCI Commit call.
// CometBFT is the authoritative BFT consensus engine; this class only records
// which heights CometBFT has finalized and detects application-level forks.
class FinalityChecker {
public:
    // Record that CometBFT has finalized the block at `height` with `app_hash`.
    // Returns true if the checkpoint extends the finalized chain
    // (height > finalized_height), false if it conflicts with an existing
    // finalized checkpoint (fork detected: height <= finalized_height with a
    // different app_hash, or an idempotent re-commit at the finalized tip).
    [[nodiscard]] bool record_commit(std::uint64_t height, const Hash& app_hash);

    // Finality status of `height` relative to the finalized checkpoint.
    [[nodiscard]] FinalityStatus finality_status(std::uint64_t height) const noexcept;

    // Current finalized height (0 if no commit has been recorded).
    [[nodiscard]] std::uint64_t finalized_height() const noexcept { return finalized_height_; }

    // App hash of the finalized tip (zero hash if no commit recorded).
    [[nodiscard]] const Hash& finalized_app_hash() const noexcept { return finalized_app_hash_; }

    // Whether finalizing at `height` would create a fork relative to the
    // finalized checkpoint: true when height < finalized_height_ (stale or
    // conflicting), false when height >= finalized_height_ (extension or
    // idempotent re-commit at the finalized tip).
    [[nodiscard]] bool would_fork(std::uint64_t height) const noexcept;

    // Reset all finality state (used on restart / recovery).
    void reset() noexcept;

private:
    std::uint64_t finalized_height_{0};
    Hash finalized_app_hash_{};
    bool initialized_{false};
};

} // namespace feinstat::consensus
