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

} // namespace feinstat::consensus
