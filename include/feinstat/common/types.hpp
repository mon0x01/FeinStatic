#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace feinstat {

using Byte = std::uint8_t;
using Bytes = std::vector<Byte>;
using Hash = std::array<Byte, 32>;
using PublicKey = std::array<Byte, 32>;
using SecretKey = std::array<Byte, 32>;
using Signature = std::array<Byte, 64>;
using Address = std::string;
using Amount = std::uint64_t;

inline constexpr Amount kAtomsPerCoin = 100'000'000ULL;
inline constexpr const char* kNativeAsset = "FST";
inline constexpr Amount kDefaultBlockReward = 2ULL * kAtomsPerCoin;

} // namespace feinstat
