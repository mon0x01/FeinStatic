#pragma once

#include "feinstat/ledger/chain.hpp"

#include <cstdint>

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

} // namespace feinstat::consensus
