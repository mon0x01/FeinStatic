#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/settlement/settlement_book.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace feinstat::flow {

inline constexpr std::uint32_t kBasisPoints = 10'000;
inline constexpr std::uint32_t kDefaultFeeBps = 30;

struct Pool {
    std::string id;
    std::string asset_x;
    std::string asset_y;
    Amount reserve_x{0};
    Amount reserve_y{0};
    Amount total_lp_shares{0};
    std::uint32_t fee_bps{kDefaultFeeBps};
};

struct SwapQuote {
    std::string pool_id;
    std::string asset_in;
    std::string asset_out;
    Amount amount_in{0};
    Amount amount_out{0};
    Amount fee_amount{0};
    std::uint32_t price_impact_bps{0};
};

struct LiquidityResult {
    Amount shares{0};
    Amount amount_x_used{0};
    Amount amount_y_used{0};
};

struct SwapRecord {
    std::uint64_t id{0};
    std::string account_id;
    std::string pool_id;
    std::string asset_in;
    std::string asset_out;
    Amount amount_in{0};
    Amount amount_out{0};
    Amount fee_amount{0};
};

struct LpPosition {
    std::string pool_id;
    std::string account_id;
    Amount shares{0};
};

struct Snapshot {
    std::vector<std::string> registered_accounts;
    std::vector<Pool> pools;
    std::vector<LpPosition> lp_positions;
    std::vector<SwapRecord> swaps;
    std::uint64_t next_pool_id{1};
    std::uint64_t next_swap_id{1};
};

class AmmEngine {
public:
    AmmEngine();
    explicit AmmEngine(std::shared_ptr<settlement::SettlementBook> settlement);

    void create_account(const std::string& account_id);
    void register_account(const std::string& account_id);
    void credit(const std::string& account_id, const std::string& asset, Amount amount);
    [[nodiscard]] Amount balance_of(const std::string& account_id, const std::string& asset) const;

    [[nodiscard]] std::string create_pool(const std::string& asset_x,
                                          const std::string& asset_y,
                                          std::uint32_t fee_bps = kDefaultFeeBps);
    [[nodiscard]] LiquidityResult add_liquidity(const std::string& account_id,
                                                const std::string& pool_id,
                                                Amount max_amount_x,
                                                Amount max_amount_y,
                                                Amount minimum_shares = 1);
    [[nodiscard]] LiquidityResult remove_liquidity(const std::string& account_id,
                                                   const std::string& pool_id,
                                                   Amount shares);

    [[nodiscard]] SwapQuote quote(const std::string& pool_id,
                                  const std::string& asset_in,
                                  Amount amount_in) const;
    [[nodiscard]] SwapRecord swap(const std::string& account_id,
                                  const std::string& pool_id,
                                  const std::string& asset_in,
                                  Amount amount_in,
                                  Amount minimum_amount_out);

    [[nodiscard]] std::optional<Pool> pool(const std::string& pool_id) const;
    [[nodiscard]] Amount lp_shares_of(const std::string& account_id, const std::string& pool_id) const;
    [[nodiscard]] const std::vector<SwapRecord>& swaps() const noexcept { return swaps_; }
    [[nodiscard]] bool validate_pool(const std::string& pool_id, std::string* error = nullptr) const;

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] static AmmEngine restore(std::shared_ptr<settlement::SettlementBook> settlement,
                                           const Snapshot& snapshot);
    [[nodiscard]] AmmEngine with_settlement(std::shared_ptr<settlement::SettlementBook> settlement) const;

private:
    void require_account(const std::string& account_id) const;
    [[nodiscard]] Pool& require_pool(const std::string& pool_id);
    [[nodiscard]] const Pool& require_pool(const std::string& pool_id) const;
    [[nodiscard]] static bool valid_asset_id(const std::string& asset) noexcept;
    [[nodiscard]] static Amount checked_add(Amount left, Amount right, const char* message);

    std::shared_ptr<settlement::SettlementBook> settlement_;
    std::set<std::string> registered_accounts_;
    std::map<std::string, Pool> pools_;
    std::map<std::pair<std::string, std::string>, Amount> lp_positions_;
    std::vector<SwapRecord> swaps_;
    std::uint64_t next_pool_id_{1};
    std::uint64_t next_swap_id_{1};
};

} // namespace feinstat::flow
