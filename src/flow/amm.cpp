#include "feinstat/flow/amm.hpp"

#include <algorithm>
#include <charconv>
#include <boost/multiprecision/cpp_int.hpp>
#include <cctype>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace feinstat::flow {
namespace {

using Wide = boost::multiprecision::uint256_t;

Amount narrow_amount(const Wide& value, const char* message) {
    if (value > Wide(UINT64_MAX)) throw std::overflow_error(message);
    return value.convert_to<Amount>();
}

Amount mul_div_floor(Amount a, Amount b, Amount denominator, const char* message) {
    if (denominator == 0) throw std::invalid_argument("division by zero in AMM math");
    return narrow_amount((Wide(a) * Wide(b)) / Wide(denominator), message);
}

Amount mul_div_ceil(Amount a, Amount b, Amount denominator, const char* message) {
    if (denominator == 0) throw std::invalid_argument("division by zero in AMM math");
    const Wide product = Wide(a) * Wide(b);
    const Wide quotient = product / Wide(denominator);
    const Wide rounded = quotient + ((product % denominator) == 0 ? 0 : 1);
    return narrow_amount(rounded, message);
}

Amount integer_sqrt(Wide value) {
    if (value == 0) return 0;
    Wide low = 0;
    Wide high = Wide(UINT64_MAX) + 1;
    while (low + 1 < high) {
        const Wide middle = low + (high - low) / 2;
        if (middle <= value / middle) low = middle;
        else high = middle;
    }
    return narrow_amount(low, "initial LP share count overflow");
}

void set_error(std::string* output, const std::string& message) {
    if (output != nullptr) *output = message;
}

} // namespace

Amount AmmEngine::checked_add(Amount left, Amount right, const char* message) {
    if (left > UINT64_MAX - right) throw std::overflow_error(message);
    return left + right;
}

bool AmmEngine::valid_asset_id(const std::string& asset) noexcept {
    if (asset.empty() || asset.size() > 16) return false;
    for (const char raw_character : asset) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (!(std::isalnum(character) || character == '_' || character == '-')) return false;
    }
    return true;
}

AmmEngine::AmmEngine()
    : AmmEngine(std::make_shared<settlement::SettlementBook>()) {}

AmmEngine::AmmEngine(std::shared_ptr<settlement::SettlementBook> settlement_book)
    : settlement_(std::move(settlement_book)) {
    if (!settlement_) settlement_ = std::make_shared<settlement::SettlementBook>();
}

void AmmEngine::require_account(const std::string& account_id) const {
    if (!registered_accounts_.contains(account_id) || !settlement_->has_account(account_id)) {
        throw std::invalid_argument("unknown Flow account: " + account_id);
    }
}

Pool& AmmEngine::require_pool(const std::string& pool_id) {
    const auto it = pools_.find(pool_id);
    if (it == pools_.end()) throw std::invalid_argument("unknown liquidity pool: " + pool_id);
    return it->second;
}

const Pool& AmmEngine::require_pool(const std::string& pool_id) const {
    const auto it = pools_.find(pool_id);
    if (it == pools_.end()) throw std::invalid_argument("unknown liquidity pool: " + pool_id);
    return it->second;
}

void AmmEngine::create_account(const std::string& account_id) {
    if (account_id.empty()) throw std::invalid_argument("Flow account cannot be empty");
    if (registered_accounts_.contains(account_id)) throw std::invalid_argument("Flow account already exists");
    if (!settlement_->has_account(account_id)) settlement_->create_account(account_id);
    registered_accounts_.insert(account_id);
}

void AmmEngine::register_account(const std::string& account_id) {
    if (account_id.empty() || !settlement_->has_account(account_id)) {
        throw std::invalid_argument("settlement account must exist before Flow registration");
    }
    if (!registered_accounts_.insert(account_id).second) throw std::invalid_argument("Flow account already registered");
}

void AmmEngine::credit(const std::string& account_id, const std::string& asset, Amount amount) {
    require_account(account_id);
    if (!valid_asset_id(asset)) throw std::invalid_argument("invalid asset identifier");
    if (amount == 0) throw std::invalid_argument("credit amount must be positive");
    settlement_->credit(account_id, asset, amount, "flow-credit", "explicit test/demo funding");
}

Amount AmmEngine::balance_of(const std::string& account_id, const std::string& asset) const {
    require_account(account_id);
    if (!valid_asset_id(asset)) throw std::invalid_argument("invalid asset identifier");
    return settlement_->available(account_id, asset);
}

std::string AmmEngine::create_pool(const std::string& asset_x,
                                   const std::string& asset_y,
                                   std::uint32_t fee_bps) {
    if (!valid_asset_id(asset_x) || !valid_asset_id(asset_y) || asset_x == asset_y) {
        throw std::invalid_argument("pool requires two distinct valid asset IDs");
    }
    if (fee_bps > 1'000) throw std::invalid_argument("AMM fee cannot exceed 10 percent");
    for (const auto& [id, existing] : pools_) {
        (void)id;
        if ((existing.asset_x == asset_x && existing.asset_y == asset_y) ||
            (existing.asset_x == asset_y && existing.asset_y == asset_x)) {
            throw std::invalid_argument("pool for this asset pair already exists");
        }
    }
    if (next_pool_id_ == UINT64_MAX) throw std::overflow_error("pool ID space exhausted");
    const std::string id = "POOL-" + std::to_string(next_pool_id_);
    const auto [it, inserted] = pools_.emplace(id, Pool{id, asset_x, asset_y, 0, 0, 0, fee_bps});
    (void)it;
    if (!inserted) throw std::logic_error("pool ID already exists");
    ++next_pool_id_;
    return id;
}

LiquidityResult AmmEngine::add_liquidity(const std::string& account_id,
                                          const std::string& pool_id,
                                          Amount max_amount_x,
                                          Amount max_amount_y,
                                          Amount minimum_shares) {
    if (max_amount_x == 0 || max_amount_y == 0) throw std::invalid_argument("liquidity amounts must be positive");
    require_account(account_id);
    Pool& target_pool = require_pool(pool_id);
    const auto balance_state_x = settlement_->state(account_id, target_pool.asset_x);
    const auto balance_state_y = settlement_->state(account_id, target_pool.asset_y);
    const Amount balance_x = balance_state_x.available();
    const Amount balance_y = balance_state_y.available();

    Amount shares = 0;
    Amount used_x = max_amount_x;
    Amount used_y = max_amount_y;
    if (target_pool.total_lp_shares == 0) {
        if (target_pool.reserve_x != 0 || target_pool.reserve_y != 0) {
            throw std::logic_error("empty LP share supply has non-empty reserves");
        }
        shares = integer_sqrt(static_cast<Wide>(max_amount_x) * static_cast<Wide>(max_amount_y));
    } else {
        if (target_pool.reserve_x == 0 || target_pool.reserve_y == 0) {
            throw std::logic_error("active pool has a zero reserve");
        }
        const Amount shares_x = mul_div_floor(max_amount_x, target_pool.total_lp_shares,
                                              target_pool.reserve_x, "LP share calculation overflow");
        const Amount shares_y = mul_div_floor(max_amount_y, target_pool.total_lp_shares,
                                              target_pool.reserve_y, "LP share calculation overflow");
        shares = std::min(shares_x, shares_y);
        if (shares > 0) {
            used_x = mul_div_ceil(shares, target_pool.reserve_x,
                                  target_pool.total_lp_shares, "liquidity X calculation overflow");
            used_y = mul_div_ceil(shares, target_pool.reserve_y,
                                  target_pool.total_lp_shares, "liquidity Y calculation overflow");
        }
    }
    if (shares == 0 || shares < minimum_shares) throw std::runtime_error("liquidity contribution mints fewer than minimum LP shares");
    if (used_x > max_amount_x || used_y > max_amount_y) throw std::logic_error("LP rounding exceeded supplied amounts");
    if (balance_x < used_x || balance_y < used_y) throw std::runtime_error("insufficient asset balance for liquidity provision");

    const auto key = std::make_pair(pool_id, account_id);
    const auto position_it = lp_positions_.find(key);
    const Amount current_position = position_it == lp_positions_.end() ? 0 : position_it->second;
    const Amount updated_position = checked_add(current_position, shares, "LP position overflow");
    const Amount updated_total_shares = checked_add(target_pool.total_lp_shares, shares, "total LP shares overflow");
    const Amount updated_reserve_x = checked_add(target_pool.reserve_x, used_x, "pool X reserve overflow");
    const Amount updated_reserve_y = checked_add(target_pool.reserve_y, used_y, "pool Y reserve overflow");

    auto updated_positions = lp_positions_;
    updated_positions[key] = updated_position;
    const std::vector<settlement::BalanceUpdate> settlement_updates{
        {account_id, target_pool.asset_x, {balance_state_x.total - used_x, balance_state_x.reserved}},
        {account_id, target_pool.asset_y, {balance_state_y.total - used_y, balance_state_y.reserved}}
    };
    settlement_->replace_states(settlement_updates, "flow-add-liquidity", pool_id);
    lp_positions_.swap(updated_positions);
    target_pool.reserve_x = updated_reserve_x;
    target_pool.reserve_y = updated_reserve_y;
    target_pool.total_lp_shares = updated_total_shares;
    return {shares, used_x, used_y};
}

LiquidityResult AmmEngine::remove_liquidity(const std::string& account_id,
                                             const std::string& pool_id,
                                             Amount shares) {
    if (shares == 0) throw std::invalid_argument("LP shares to remove must be positive");
    require_account(account_id);
    Pool& target_pool = require_pool(pool_id);
    const auto key = std::make_pair(pool_id, account_id);
    const auto position_it = lp_positions_.find(key);
    if (position_it == lp_positions_.end() || position_it->second < shares) {
        throw std::runtime_error("insufficient LP shares");
    }
    if (shares > target_pool.total_lp_shares || target_pool.total_lp_shares == 0) {
        throw std::logic_error("LP share supply invariant violated");
    }

    const bool remove_all = shares == target_pool.total_lp_shares;
    const Amount amount_x = remove_all ? target_pool.reserve_x
        : mul_div_floor(shares, target_pool.reserve_x, target_pool.total_lp_shares, "LP redemption X overflow");
    const Amount amount_y = remove_all ? target_pool.reserve_y
        : mul_div_floor(shares, target_pool.reserve_y, target_pool.total_lp_shares, "LP redemption Y overflow");
    if (amount_x == 0 || amount_y == 0) throw std::runtime_error("LP position is too small to redeem both assets");

    const auto balance_state_x = settlement_->state(account_id, target_pool.asset_x);
    const auto balance_state_y = settlement_->state(account_id, target_pool.asset_y);
    const auto updated_positions_amount_x = checked_add(balance_state_x.total, amount_x, "LP redemption X balance overflow");
    const auto updated_positions_amount_y = checked_add(balance_state_y.total, amount_y, "LP redemption Y balance overflow");
    auto updated_positions = lp_positions_;
    auto updated_position_it = updated_positions.find(key);
    updated_position_it->second -= shares;
    if (updated_position_it->second == 0) updated_positions.erase(updated_position_it);
    const std::vector<settlement::BalanceUpdate> settlement_updates{
        {account_id, target_pool.asset_x, {updated_positions_amount_x, balance_state_x.reserved}},
        {account_id, target_pool.asset_y, {updated_positions_amount_y, balance_state_y.reserved}}
    };
    settlement_->replace_states(settlement_updates, "flow-remove-liquidity", pool_id);
    lp_positions_.swap(updated_positions);
    target_pool.reserve_x -= amount_x;
    target_pool.reserve_y -= amount_y;
    target_pool.total_lp_shares -= shares;
    return {shares, amount_x, amount_y};
}

SwapQuote AmmEngine::quote(const std::string& pool_id,
                           const std::string& asset_in,
                           Amount amount_in) const {
    if (amount_in == 0) throw std::invalid_argument("swap input must be positive");
    const Pool& target_pool = require_pool(pool_id);
    Amount reserve_in = 0;
    Amount reserve_out = 0;
    std::string asset_out;
    if (asset_in == target_pool.asset_x) {
        reserve_in = target_pool.reserve_x;
        reserve_out = target_pool.reserve_y;
        asset_out = target_pool.asset_y;
    } else if (asset_in == target_pool.asset_y) {
        reserve_in = target_pool.reserve_y;
        reserve_out = target_pool.reserve_x;
        asset_out = target_pool.asset_x;
    } else {
        throw std::invalid_argument("input asset does not belong to this pool");
    }
    if (reserve_in == 0 || reserve_out == 0 || target_pool.total_lp_shares == 0) {
        throw std::runtime_error("cannot swap against an empty pool");
    }

    // A 64-bit amount multiplied by a basis-point-scaled amount can require
    // up to 142 bits. Keep every intermediate in fixed-width 256-bit integer
    // arithmetic; narrowing happens only after the final financial quantity is
    // known to fit the public uint64 Amount type.
    const Wide weighted_input = Wide(amount_in) * (kBasisPoints - target_pool.fee_bps);
    const Wide denominator = Wide(reserve_in) * kBasisPoints + weighted_input;
    const Wide numerator = Wide(reserve_out) * weighted_input;
    if (denominator == 0) throw std::runtime_error("invalid AMM quote denominator");
    const Amount output = narrow_amount(numerator / denominator, "swap output overflow");
    if (output == 0 || output >= reserve_out) throw std::runtime_error("swap output rounds to zero or exhausts pool reserve");
    const Amount effective_input = narrow_amount(weighted_input / kBasisPoints, "effective swap input overflow");
    const Amount fee = amount_in - effective_input;
    const Wide spot_output = (Wide(amount_in) * reserve_out) / reserve_in;
    const Wide impact = spot_output <= Wide(output) ? Wide(0)
        : ((spot_output - output) * kBasisPoints) / spot_output;
    const auto impact_bps = narrow_amount(impact, "price impact overflow");
    if (impact_bps > kBasisPoints) throw std::logic_error("price impact exceeds 100 percent");

    const Amount updated_reserve_in = checked_add(reserve_in, amount_in, "pool input reserve overflow");
    const Amount updated_reserve_out = reserve_out - output;
    const Wide invariant_before = Wide(target_pool.reserve_x) * target_pool.reserve_y;
    const Wide invariant_after = asset_in == target_pool.asset_x
        ? Wide(updated_reserve_in) * updated_reserve_out
        : Wide(updated_reserve_out) * updated_reserve_in;
    if (invariant_after < invariant_before) throw std::logic_error("constant-product invariant would decrease");

    return {pool_id, asset_in, asset_out, amount_in, output, fee,
            static_cast<std::uint32_t>(impact_bps)};
}

SwapRecord AmmEngine::swap(const std::string& account_id,
                           const std::string& pool_id,
                           const std::string& asset_in,
                           Amount amount_in,
                           Amount minimum_amount_out) {
    if (minimum_amount_out == 0) throw std::invalid_argument("minimum output must be positive");
    const SwapQuote quotation = quote(pool_id, asset_in, amount_in);
    if (quotation.amount_out < minimum_amount_out) throw std::runtime_error("swap rejected: minimum output / slippage protection not met");

    require_account(account_id);
    const auto input_state = settlement_->state(account_id, quotation.asset_in);
    const auto output_state = settlement_->state(account_id, quotation.asset_out);
    if (input_state.available() < amount_in) throw std::runtime_error("insufficient input asset balance");
    const Amount updated_input_total = input_state.total - amount_in;
    const Amount updated_output_total = checked_add(output_state.total, quotation.amount_out,
                                                    "trader output balance overflow");

    Pool& target_pool = require_pool(pool_id);
    Amount updated_x = target_pool.reserve_x;
    Amount updated_y = target_pool.reserve_y;
    if (asset_in == target_pool.asset_x) {
        updated_x = checked_add(updated_x, amount_in, "pool X reserve overflow");
        updated_y -= quotation.amount_out;
    } else {
        updated_y = checked_add(updated_y, amount_in, "pool Y reserve overflow");
        updated_x -= quotation.amount_out;
    }
    const Wide invariant_before = Wide(target_pool.reserve_x) * target_pool.reserve_y;
    const Wide invariant_after = Wide(updated_x) * updated_y;
    if (invariant_after < invariant_before) throw std::logic_error("swap would violate constant-product invariant");
    if (next_swap_id_ == UINT64_MAX) throw std::overflow_error("swap ID space exhausted");

    SwapRecord record{next_swap_id_, account_id, pool_id, quotation.asset_in, quotation.asset_out,
                      amount_in, quotation.amount_out, quotation.fee_amount};
    if (swaps_.size() == swaps_.capacity()) {
        const std::size_t current_capacity = swaps_.capacity();
        const std::size_t maximum_capacity = swaps_.max_size();
        if (current_capacity == maximum_capacity) throw std::length_error("swap history capacity exhausted");
        const std::size_t next_capacity = current_capacity == 0 ? 8
            : (current_capacity > maximum_capacity / 2 ? maximum_capacity : current_capacity * 2);
        swaps_.reserve(next_capacity);
    }
    const std::vector<settlement::BalanceUpdate> settlement_updates{
        {account_id, quotation.asset_in, {updated_input_total, input_state.reserved}},
        {account_id, quotation.asset_out, {updated_output_total, output_state.reserved}}
    };
    settlement_->replace_states(settlement_updates, "flow-swap", pool_id);
    target_pool.reserve_x = updated_x;
    target_pool.reserve_y = updated_y;
    swaps_.push_back(std::move(record));
    ++next_swap_id_;
    return swaps_.back();
}

std::optional<Pool> AmmEngine::pool(const std::string& pool_id) const {
    const auto it = pools_.find(pool_id);
    if (it == pools_.end()) return std::nullopt;
    return it->second;
}

Amount AmmEngine::lp_shares_of(const std::string& account_id, const std::string& pool_id) const {
    (void)require_account(account_id);
    (void)require_pool(pool_id);
    const auto it = lp_positions_.find({pool_id, account_id});
    return it == lp_positions_.end() ? 0 : it->second;
}

bool AmmEngine::validate_pool(const std::string& pool_id, std::string* error) const {
    try {
        const Pool& target_pool = require_pool(pool_id);
        if (target_pool.asset_x == target_pool.asset_y || !valid_asset_id(target_pool.asset_x) ||
            !valid_asset_id(target_pool.asset_y) || target_pool.fee_bps > 1'000) {
            set_error(error, "invalid pool metadata");
            return false;
        }
        const bool empty_pool = target_pool.reserve_x == 0 && target_pool.reserve_y == 0;
        if ((target_pool.total_lp_shares == 0 && !empty_pool) ||
            (target_pool.total_lp_shares > 0 &&
             (target_pool.reserve_x == 0 || target_pool.reserve_y == 0))) {
            set_error(error, "LP share supply and reserve state disagree");
            return false;
        }
        Amount share_sum = 0;
        for (const auto& [key, positions] : lp_positions_) {
            if (key.first != pool_id) continue;
            if (positions == 0) {
                set_error(error, "zero LP position should not be stored");
                return false;
            }
            share_sum = checked_add(share_sum, positions, "LP position sum overflow");
        }
        if (share_sum != target_pool.total_lp_shares) {
            set_error(error, "LP position sum does not equal total shares");
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    }
}

Snapshot AmmEngine::snapshot() const {
    Snapshot result;
    result.registered_accounts.assign(registered_accounts_.begin(), registered_accounts_.end());
    result.pools.reserve(pools_.size());
    for (const auto& [id, pool_value] : pools_) { (void)id; result.pools.push_back(pool_value); }
    result.lp_positions.reserve(lp_positions_.size());
    for (const auto& [key, shares] : lp_positions_) {
        result.lp_positions.push_back(LpPosition{key.first, key.second, shares});
    }
    result.swaps = swaps_;
    result.next_pool_id = next_pool_id_;
    result.next_swap_id = next_swap_id_;
    return result;
}

AmmEngine AmmEngine::restore(std::shared_ptr<settlement::SettlementBook> settlement_book,
                             const Snapshot& snapshot_value) {
    AmmEngine result(std::move(settlement_book));
    if (snapshot_value.registered_accounts.size() > 100'000 || snapshot_value.pools.size() > 100'000 ||
        snapshot_value.lp_positions.size() > 1'000'000 || snapshot_value.swaps.size() > 5'000'000 ||
        snapshot_value.next_pool_id == 0 || snapshot_value.next_swap_id == 0) {
        throw std::runtime_error("AMM snapshot exceeds limits or has invalid counters");
    }
    for (const auto& account_id : snapshot_value.registered_accounts) {
        if (!result.settlement_->has_account(account_id) || !result.registered_accounts_.insert(account_id).second) {
            throw std::runtime_error("AMM snapshot references missing or duplicate settlement account");
        }
    }
    std::uint64_t maximum_pool_id = 0;
    std::set<std::uint64_t> numeric_pool_ids;
    std::set<std::pair<std::string, std::string>> asset_pairs;
    for (const auto& pool_value : snapshot_value.pools) {
        constexpr std::string_view prefix = "POOL-";
        if (!pool_value.id.starts_with(prefix)) throw std::runtime_error("invalid pool ID in AMM snapshot");
        std::uint64_t numeric_id = 0;
        const std::string_view suffix(pool_value.id.data() + prefix.size(), pool_value.id.size() - prefix.size());
        const auto [end, parse_error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), numeric_id);
        if (parse_error != std::errc{} || end != suffix.data() + suffix.size() || numeric_id == 0 ||
            numeric_id == UINT64_MAX || suffix != std::to_string(numeric_id) ||
            !numeric_pool_ids.insert(numeric_id).second || !valid_asset_id(pool_value.asset_x) ||
            !valid_asset_id(pool_value.asset_y) || pool_value.asset_x == pool_value.asset_y ||
            pool_value.fee_bps > 1'000 || !asset_pairs.emplace(
                std::min(pool_value.asset_x, pool_value.asset_y),
                std::max(pool_value.asset_x, pool_value.asset_y)).second ||
            !result.pools_.emplace(pool_value.id, pool_value).second) {
            throw std::runtime_error("invalid or duplicate pool in AMM snapshot");
        }
        maximum_pool_id = std::max(maximum_pool_id, numeric_id);
    }
    if (maximum_pool_id != result.pools_.size() || snapshot_value.next_pool_id != maximum_pool_id + 1) {
        throw std::runtime_error("AMM pool IDs are not contiguous or next pool ID is inconsistent");
    }
    for (const auto& position : snapshot_value.lp_positions) {
        if (position.shares == 0 || !result.registered_accounts_.contains(position.account_id) ||
            !result.pools_.contains(position.pool_id) ||
            !result.lp_positions_.emplace(std::make_pair(position.pool_id, position.account_id), position.shares).second) {
            throw std::runtime_error("invalid or duplicate LP position in AMM snapshot");
        }
    }
    result.swaps_ = snapshot_value.swaps;
    result.next_pool_id_ = snapshot_value.next_pool_id;
    result.next_swap_id_ = snapshot_value.next_swap_id;
    std::uint64_t expected_swap_id = 1;
    for (const auto& swap_value : result.swaps_) {
        const auto pool_it = result.pools_.find(swap_value.pool_id);
        if (swap_value.id != expected_swap_id++ ||
            !result.registered_accounts_.contains(swap_value.account_id) ||
            pool_it == result.pools_.end() || swap_value.amount_in == 0 || swap_value.amount_out == 0 ||
            swap_value.fee_amount > swap_value.amount_in ||
            !((swap_value.asset_in == pool_it->second.asset_x && swap_value.asset_out == pool_it->second.asset_y) ||
              (swap_value.asset_in == pool_it->second.asset_y && swap_value.asset_out == pool_it->second.asset_x))) {
            throw std::runtime_error("invalid swap record in AMM snapshot");
        }
    }
    if (result.next_swap_id_ != expected_swap_id) {
        throw std::runtime_error("AMM next swap ID does not follow persisted swap history");
    }
    for (const auto& [pool_id, pool_value] : result.pools_) {
        (void)pool_value;
        std::string error;
        if (!result.validate_pool(pool_id, &error)) throw std::runtime_error("AMM snapshot validation failed: " + error);
    }
    return result;
}

AmmEngine AmmEngine::with_settlement(std::shared_ptr<settlement::SettlementBook> settlement_book) const {
    if (!settlement_book) throw std::invalid_argument("AMM engine requires a settlement book");
    AmmEngine copy = *this;
    copy.settlement_ = std::move(settlement_book);
    return copy;
}

} // namespace feinstat::flow
