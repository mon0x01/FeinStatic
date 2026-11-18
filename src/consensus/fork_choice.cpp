#include "feinstat/consensus/fork_choice.hpp"

#include <algorithm>
#include <stdexcept>

namespace feinstat::consensus {

ChainComparison DeterministicForkChoice::compare(const ledger::Chain& current,
                                                   const ledger::Chain& candidate) {
    const auto& current_blocks = current.blocks();
    const auto& candidate_blocks = candidate.blocks();
    if (current_blocks.empty() || candidate_blocks.empty()) {
        throw std::invalid_argument("fork choice requires chains with genesis blocks");
    }
    if (current.difficulty() != candidate.difficulty() ||
        current.block_reward() != candidate.block_reward() ||
        current_blocks.front().serialize() != candidate_blocks.front().serialize()) {
        throw std::invalid_argument("fork choice requires matching genesis and consensus parameters");
    }

    const std::size_t shared_limit = std::min(current_blocks.size(), candidate_blocks.size());
    std::size_t common_blocks = 0;
    while (common_blocks < shared_limit &&
           current_blocks[common_blocks].hash == candidate_blocks[common_blocks].hash) {
        ++common_blocks;
    }
    if (common_blocks == current_blocks.size() && common_blocks == candidate_blocks.size()) {
        return {ChainPreference::Equivalent, static_cast<std::uint64_t>(common_blocks - 1), true};
    }

    const std::uint64_t common_height = common_blocks == 0
        ? 0 : static_cast<std::uint64_t>(common_blocks - 1);
    if (candidate.height() > current.height()) {
        return {ChainPreference::AdoptCandidate, common_height, false};
    }
    if (candidate.height() < current.height()) {
        return {ChainPreference::KeepCurrent, common_height, false};
    }

    // Equal work: a deterministic tie-break ensures honest nodes presented
    // with the same two candidates converge on the same tip. The value is not
    // security weight; a miner can grind nonce values to influence it.
    const bool candidate_wins = std::lexicographical_compare(
        candidate_blocks.back().hash.begin(), candidate_blocks.back().hash.end(),
        current_blocks.back().hash.begin(), current_blocks.back().hash.end());
    return {candidate_wins ? ChainPreference::AdoptCandidate : ChainPreference::KeepCurrent,
            common_height, false};
}

bool FinalityChecker::record_commit(std::uint64_t height, const Hash& app_hash) {
    if (!initialized_) {
        // First commit observed: initialize the finalized checkpoint.
        finalized_height_ = height;
        finalized_app_hash_ = app_hash;
        initialized_ = true;
        return true;
    }
    if (height > finalized_height_) {
        // Chain extension: advance the finalized checkpoint.
        finalized_height_ = height;
        finalized_app_hash_ = app_hash;
        return true;
    }
    if (height == finalized_height_ && finalized_app_hash_ == app_hash) {
        // Idempotent re-commit at the finalized tip: not a fork.
        return true;
    }
    // Fork detected: a conflicting or stale block at or below the finalized
    // height. Do not advance the checkpoint; the caller should treat this as
    // a fork and initiate recovery.
    return false;
}

FinalityStatus FinalityChecker::finality_status(std::uint64_t height) const noexcept {
    if (!initialized_) {
        return FinalityStatus::NotFinalized;
    }
    if (height == finalized_height_) {
        return FinalityStatus::Finalized;
    }
    if (height < finalized_height_) {
        return FinalityStatus::ForkDetected;
    }
    return FinalityStatus::NotFinalized;
}

bool FinalityChecker::would_fork(std::uint64_t height) const noexcept {
    if (!initialized_) {
        return false;
    }
    return height < finalized_height_;
}

void FinalityChecker::reset() noexcept {
    finalized_height_ = 0;
    finalized_app_hash_ = Hash{};
    initialized_ = false;
}

} // namespace feinstat::consensus
