#include "feinstat/exchange/matching_engine.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace feinstat::exchange {
namespace {

Amount checked_add(Amount left, Amount right, const char* message) {
    if (left > UINT64_MAX - right) throw std::overflow_error(message);
    return left + right;
}

bool is_active_status(OrderStatus status) {
    return status == OrderStatus::Open || status == OrderStatus::PartiallyFilled;
}

} // namespace

MatchingEngine::MatchingEngine()
    : MatchingEngine(std::make_shared<settlement::SettlementBook>(), "BASE", "QUOTE", 1) {}

MatchingEngine::MatchingEngine(std::shared_ptr<settlement::SettlementBook> settlement_book,
                               std::string base_asset,
                               std::string quote_asset,
                               Amount base_lot_size)
    : settlement_(std::move(settlement_book)), base_asset_(std::move(base_asset)),
      quote_asset_(std::move(quote_asset)), base_lot_size_(base_lot_size) {
    if (!settlement_) settlement_ = std::make_shared<settlement::SettlementBook>();
    if (!settlement::SettlementBook::valid_asset_id(base_asset_) ||
        !settlement::SettlementBook::valid_asset_id(quote_asset_) ||
        base_asset_ == quote_asset_ || base_lot_size_ == 0) {
        throw std::invalid_argument("invalid matching market asset configuration");
    }
}

void MatchingEngine::create_account(const std::string& account_id,
                                    Amount base_balance,
                                    Amount quote_balance) {
    if (registered_accounts_.contains(account_id)) throw std::invalid_argument("exchange account already exists");
    if (!settlement_->has_account(account_id)) settlement_->create_account(account_id);
    registered_accounts_.insert(account_id);
    if (base_balance != 0) settlement_->credit(account_id, base_asset_, base_balance, "match-account-funding");
    if (quote_balance != 0) settlement_->credit(account_id, quote_asset_, quote_balance, "match-account-funding");
}

void MatchingEngine::register_account(const std::string& account_id) {
    if (account_id.empty()) throw std::invalid_argument("exchange account ID cannot be empty");
    if (!settlement_->has_account(account_id)) throw std::invalid_argument("settlement account must exist before exchange registration");
    if (!registered_accounts_.insert(account_id).second) throw std::invalid_argument("exchange account already registered");
}

Amount MatchingEngine::checked_product(Price price, Quantity quantity) const {
    if (quantity != 0 && price > UINT64_MAX / quantity) {
        throw std::overflow_error("order notional overflows quote amount");
    }
    return price * quantity;
}

Amount MatchingEngine::checked_base_quantity(Quantity quantity) const {
    if (quantity != 0 && base_lot_size_ > UINT64_MAX / quantity) {
        throw std::overflow_error("base-asset quantity overflows");
    }
    return quantity * base_lot_size_;
}

void MatchingEngine::require_account(const std::string& account_id) const {
    if (!registered_accounts_.contains(account_id) || !settlement_->has_account(account_id)) {
        throw std::invalid_argument("unknown exchange account: " + account_id);
    }
}

bool MatchingEngine::active(const Order& order_value) const noexcept {
    return is_active_status(order_value.status);
}

bool MatchingEngine::has_crossing_self_order(const std::string& account_id,
                                             Side incoming_side,
                                             Price incoming_price,
                                             bool market) const {
    if (incoming_side == Side::Buy) {
        for (auto level = asks_.begin(); level != asks_.end(); ++level) {
            if (!market && level->first > incoming_price) break;
            for (const OrderId id : level->second) {
                const auto existing = orders_.find(id);
                if (existing != orders_.end() && active(existing->second) &&
                    existing->second.account_id == account_id) return true;
            }
        }
    } else {
        for (auto level = bids_.begin(); level != bids_.end(); ++level) {
            if (!market && level->first < incoming_price) break;
            for (const OrderId id : level->second) {
                const auto existing = orders_.find(id);
                if (existing != orders_.end() && active(existing->second) &&
                    existing->second.account_id == account_id) return true;
            }
        }
    }
    return false;
}

OrderId MatchingEngine::submit_limit(const std::string& account_id,
                                     Side side,
                                     Price price,
                                     Quantity quantity) {
    if (side != Side::Buy && side != Side::Sell) throw std::invalid_argument("invalid order side");
    if (price == 0 || quantity == 0) throw std::invalid_argument("limit order price and quantity must be positive");
    const AccountSnapshot funds = account(account_id);
    const Amount available = side == Side::Buy ? funds.available_quote() : funds.available_base();
    const Amount reserve = side == Side::Buy ? checked_product(price, quantity)
                                                : checked_base_quantity(quantity);
    if (available < reserve) throw std::runtime_error("insufficient available balance for limit order");
    if (has_crossing_self_order(account_id, side, price, false)) {
        throw std::runtime_error("self-trade prevention: order would cross the same account's resting order");
    }

    Order incoming;
    incoming.account_id = account_id;
    incoming.side = side;
    incoming.type = OrderType::Limit;
    incoming.limit_price = price;
    incoming.quantity = quantity;
    incoming.remaining_quantity = quantity;
    incoming.reserved_remaining = reserve;
    return submit_order(std::move(incoming));
}

OrderId MatchingEngine::submit_market_buy(const std::string& account_id, Amount quote_budget) {
    if (quote_budget == 0) throw std::invalid_argument("market buy budget must be positive");
    if (account(account_id).available_quote() < quote_budget) {
        throw std::runtime_error("insufficient quote balance for market buy");
    }
    if (has_crossing_self_order(account_id, Side::Buy, 0, true)) {
        throw std::runtime_error("self-trade prevention: market buy would cross own resting sell");
    }

    Order incoming;
    incoming.account_id = account_id;
    incoming.side = Side::Buy;
    incoming.type = OrderType::Market;
    incoming.reserved_remaining = quote_budget;
    return submit_order(std::move(incoming));
}

OrderId MatchingEngine::submit_market_sell(const std::string& account_id, Quantity quantity) {
    if (quantity == 0) throw std::invalid_argument("market sell quantity must be positive");
    if (account(account_id).available_base() < checked_base_quantity(quantity)) {
        throw std::runtime_error("insufficient base balance for market sell");
    }
    if (has_crossing_self_order(account_id, Side::Sell, 0, true)) {
        throw std::runtime_error("self-trade prevention: market sell would cross own resting buy");
    }

    Order incoming;
    incoming.account_id = account_id;
    incoming.side = Side::Sell;
    incoming.type = OrderType::Market;
    incoming.quantity = quantity;
    incoming.remaining_quantity = quantity;
    incoming.reserved_remaining = checked_base_quantity(quantity);
    return submit_order(std::move(incoming));
}

OrderId MatchingEngine::submit_order(Order incoming) {
    if (next_order_id_ == 0 || next_order_id_ == UINT64_MAX) {
        throw std::overflow_error("order ID space exhausted");
    }
    const OrderId id = next_order_id_;
    incoming.id = id;
    incoming.sequence = id;

    // Build a transaction plan using projected copies of only the accounts and
    // maker orders touched by this incoming order. No live balance/order state
    // changes until every fill, arithmetic operation, trade record and required
    // allocation has been validated/prepared.
    std::map<OrderId, Order> projected_orders;
    projected_orders.emplace(id, incoming);
    std::map<std::string, AccountSnapshot> projected_accounts;
    auto account_projection = [&](const std::string& account_id) -> AccountSnapshot& {
        auto it = projected_accounts.find(account_id);
        if (it == projected_accounts.end()) {
            it = projected_accounts.emplace(account_id, account(account_id)).first;
        }
        return it->second;
    };

    AccountSnapshot& incoming_account = account_projection(incoming.account_id);
    if (incoming.side == Side::Buy) {
        incoming_account.reserved_quote = checked_add(incoming_account.reserved_quote,
            incoming.reserved_remaining, "reserved quote overflow");
    } else {
        incoming_account.reserved_base = checked_add(incoming_account.reserved_base,
            incoming.reserved_remaining, "reserved base overflow");
    }

    std::vector<Trade> prepared_trades;
    std::vector<Price> visited_price_levels;
    std::uint64_t projected_trade_id = next_trade_id_;

    auto apply_projected_fill = [&](OrderId maker_id, Price execution_price, Quantity quantity) {
        if (quantity == 0) return;
        auto maker_it = projected_orders.find(maker_id);
        if (maker_it == projected_orders.end()) {
            maker_it = projected_orders.emplace(maker_id, orders_.at(maker_id)).first;
        }
        Order& taker = projected_orders.at(id);
        Order& maker = maker_it->second;
        if (!active(maker) || maker.account_id == taker.account_id || maker.side == taker.side) {
            throw std::logic_error("matching plan references an inactive or incompatible maker order");
        }

        Order& buy = taker.side == Side::Buy ? taker : maker;
        Order& sell = taker.side == Side::Sell ? taker : maker;
        const Amount actual_quote = checked_product(execution_price, quantity);
        const Amount buyer_reserve_consumption = buy.type == OrderType::Market
            ? actual_quote : checked_product(buy.limit_price, quantity);
        const Amount base_asset_quantity = checked_base_quantity(quantity);
        const Amount seller_reserve_consumption = base_asset_quantity;
        AccountSnapshot& buyer_funds = account_projection(buy.account_id);
        AccountSnapshot& seller_funds = account_projection(sell.account_id);

        if (buy.reserved_remaining < buyer_reserve_consumption ||
            buyer_funds.reserved_quote < buyer_reserve_consumption ||
            buyer_funds.quote_balance < actual_quote) {
            throw std::logic_error("buyer reservation invariant violated during match preflight");
        }
        if (sell.reserved_remaining < seller_reserve_consumption ||
            seller_funds.reserved_base < seller_reserve_consumption ||
            seller_funds.base_balance < quantity) {
            throw std::logic_error("seller reservation invariant violated during match preflight");
        }
        if (buy.filled_quantity > UINT64_MAX - quantity ||
            sell.filled_quantity > UINT64_MAX - quantity) {
            throw std::overflow_error("filled quantity overflow");
        }
        if (projected_trade_id == 0 || projected_trade_id == UINT64_MAX) {
            throw std::overflow_error("trade ID space exhausted");
        }
        const Amount buyer_base_after = checked_add(buyer_funds.base_balance, base_asset_quantity,
                                                    "buyer base balance overflow");
        const Amount seller_quote_after = checked_add(seller_funds.quote_balance, actual_quote,
                                                       "seller quote balance overflow");

        // Construct/copy string fields before mutating projected state. These
        // prepared records can later be moved into a pre-reserved trade vector.
        prepared_trades.push_back(Trade{projected_trade_id, maker.id, taker.id,
            buy.account_id, sell.account_id, execution_price, quantity, actual_quote});
        ++projected_trade_id;

        buyer_funds.reserved_quote -= buyer_reserve_consumption;
        buyer_funds.quote_balance -= actual_quote;
        buyer_funds.base_balance = buyer_base_after;
        seller_funds.reserved_base -= seller_reserve_consumption;
        seller_funds.base_balance -= base_asset_quantity;
        seller_funds.quote_balance = seller_quote_after;

        buy.reserved_remaining -= buyer_reserve_consumption;
        sell.reserved_remaining -= seller_reserve_consumption;
        buy.filled_quantity += quantity;
        sell.filled_quantity += quantity;
        if (buy.type == OrderType::Limit) buy.remaining_quantity -= quantity;
        sell.remaining_quantity -= quantity;

        if (buy.type == OrderType::Limit) {
            buy.status = buy.remaining_quantity == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
        }
        sell.status = sell.remaining_quantity == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    };

    bool stop_matching = false;
    if (incoming.side == Side::Buy) {
        for (const auto& [price, queue] : asks_) {
            if (incoming.type == OrderType::Limit && price > incoming.limit_price) break;
            if (incoming.type == OrderType::Market && projected_orders.at(id).reserved_remaining / price == 0) break;
            visited_price_levels.push_back(price);
            for (const OrderId maker_id : queue) {
                const auto source = orders_.find(maker_id);
                if (source == orders_.end()) continue;
                auto projected = projected_orders.find(maker_id);
                const Order& maker = projected == projected_orders.end() ? source->second : projected->second;
                if (!active(maker)) continue;
                Order& taker = projected_orders.at(id);
                const Quantity available = incoming.type == OrderType::Market
                    ? taker.reserved_remaining / price : taker.remaining_quantity;
                if (available == 0) { stop_matching = true; break; }
                const Quantity matched = std::min(maker.remaining_quantity, available);
                apply_projected_fill(maker_id, price, matched);
                const Order& updated_taker = projected_orders.at(id);
                if ((incoming.type == OrderType::Market && updated_taker.reserved_remaining / price == 0) ||
                    (incoming.type == OrderType::Limit && updated_taker.remaining_quantity == 0)) {
                    stop_matching = true;
                    break;
                }
            }
            if (stop_matching) break;
        }
    } else {
        for (const auto& [price, queue] : bids_) {
            if (incoming.type == OrderType::Limit && price < incoming.limit_price) break;
            visited_price_levels.push_back(price);
            for (const OrderId maker_id : queue) {
                const auto source = orders_.find(maker_id);
                if (source == orders_.end()) continue;
                auto projected = projected_orders.find(maker_id);
                const Order& maker = projected == projected_orders.end() ? source->second : projected->second;
                if (!active(maker)) continue;
                const Order& taker = projected_orders.at(id);
                const Quantity matched = std::min(maker.remaining_quantity, taker.remaining_quantity);
                if (matched == 0) { stop_matching = true; break; }
                apply_projected_fill(maker_id, price, matched);
                if (projected_orders.at(id).remaining_quantity == 0) { stop_matching = true; break; }
            }
            if (stop_matching) break;
        }
    }

    Order& projected_incoming = projected_orders.at(id);
    if (projected_incoming.type == OrderType::Market && projected_incoming.side == Side::Buy) {
        const bool budget_fully_spent = projected_incoming.reserved_remaining == 0;
        if (projected_incoming.reserved_remaining > incoming_account.reserved_quote) {
            throw std::logic_error("market buy remaining reservation exceeds account projection");
        }
        incoming_account.reserved_quote -= projected_incoming.reserved_remaining;
        projected_incoming.reserved_remaining = 0;
        projected_incoming.status = projected_incoming.filled_quantity == 0 ? OrderStatus::Cancelled
            : (budget_fully_spent ? OrderStatus::Filled : OrderStatus::PartiallyFilledCancelled);
    } else if (projected_incoming.type == OrderType::Market && projected_incoming.side == Side::Sell) {
        if (projected_incoming.reserved_remaining > incoming_account.reserved_base) {
            throw std::logic_error("market sell remaining reservation exceeds account projection");
        }
        incoming_account.reserved_base -= projected_incoming.reserved_remaining;
        projected_incoming.reserved_remaining = 0;
        projected_incoming.status = projected_incoming.remaining_quantity == 0 ? OrderStatus::Filled
            : (projected_incoming.filled_quantity == 0 ? OrderStatus::Cancelled
                                                       : OrderStatus::PartiallyFilledCancelled);
    } else {
        projected_incoming.status = projected_incoming.remaining_quantity == 0 ? OrderStatus::Filled
            : (projected_incoming.filled_quantity == 0 ? OrderStatus::Open : OrderStatus::PartiallyFilled);
    }

    if (prepared_trades.size() > trades_.max_size() - trades_.size()) {
        throw std::length_error("trade history capacity exhausted");
    }
    const std::size_t required_trade_capacity = trades_.size() + prepared_trades.size();
    if (required_trade_capacity > trades_.capacity()) {
        const std::size_t maximum_capacity = trades_.max_size();
        const std::size_t current_capacity = trades_.capacity();
        std::size_t next_capacity = current_capacity == 0 ? 8
            : (current_capacity > maximum_capacity / 2 ? maximum_capacity : current_capacity * 2);
        if (next_capacity < required_trade_capacity) next_capacity = required_trade_capacity;
        trades_.reserve(next_capacity);
    }

    std::vector<settlement::BalanceUpdate> settlement_updates;
    settlement_updates.reserve(projected_accounts.size() * 2);
    for (const auto& [account_id, projection] : projected_accounts) {
        const settlement::BalanceState projected_base{projection.base_balance, projection.reserved_base};
        const settlement::BalanceState projected_quote{projection.quote_balance, projection.reserved_quote};
        if (settlement_->state(account_id, base_asset_) != projected_base) {
            settlement_updates.push_back({account_id, base_asset_, projected_base});
        }
        if (settlement_->state(account_id, quote_asset_) != projected_quote) {
            settlement_updates.push_back({account_id, quote_asset_, projected_quote});
        }
    }

    const bool should_rest = projected_incoming.type == OrderType::Limit &&
                             projected_incoming.remaining_quantity > 0;
    Order final_incoming = std::move(projected_incoming);
    projected_orders.erase(id);
    const auto [inserted_at, inserted] = orders_.emplace(id, std::move(final_incoming));
    if (!inserted) throw std::logic_error("duplicate order ID during commit");
    if (should_rest) {
        try {
            rest_order(id);
        } catch (...) {
            orders_.erase(inserted_at);
            throw;
        }
    }

    // This batch is the only remaining potentially-throwing commit stage. It
    // provides a strong exception guarantee; roll back the just-inserted taker
    // if allocation/validation fails before settlement changes are published.
    try {
        settlement_->replace_states(settlement_updates, "match-order", std::to_string(id));
    } catch (...) {
        if (should_rest && incoming.side == Side::Buy) {
            const auto level = bids_.find(incoming.limit_price);
            if (level != bids_.end()) {
                if (!level->second.empty() && level->second.back() == id) level->second.pop_back();
                if (level->second.empty()) bids_.erase(level);
            }
        } else if (should_rest) {
            const auto level = asks_.find(incoming.limit_price);
            if (level != asks_.end()) {
                if (!level->second.empty() && level->second.back() == id) level->second.pop_back();
                if (level->second.empty()) asks_.erase(level);
            }
        }
        orders_.erase(id);
        throw;
    }

    // Commit contains only fixed-size account assignments, noexcept swaps,
    // and moves into pre-reserved storage.
    for (auto& [order_id, projection] : projected_orders) {
        auto actual = orders_.find(order_id);
        if (actual == orders_.end()) throw std::logic_error("maker order disappeared during match commit");
        using std::swap;
        swap(actual->second, projection);
    }
    for (auto& trade : prepared_trades) trades_.push_back(std::move(trade));
    next_order_id_ = id + 1;
    next_trade_id_ = projected_trade_id;

    // Matching only consumes from the head of each FIFO price queue. Prune
    // now-inactive heads after the successful commit; doing it before commit
    // would make a failed preflight observably mutate the book. This keeps stale
    // cancelled/filled IDs from accumulating into linear scans over history.
    auto prune_heads = [&](auto& book) noexcept {
        for (const Price price : visited_price_levels) {
            auto level = book.find(price);
            if (level == book.end()) continue;
            auto& queue = level->second;
            while (!queue.empty()) {
                const auto current = orders_.find(queue.front());
                if (current != orders_.end() && active(current->second)) break;
                queue.pop_front();
            }
            if (queue.empty()) book.erase(level);
        }
    };
    if (incoming.side == Side::Buy) prune_heads(asks_);
    else prune_heads(bids_);
    return id;
}

void MatchingEngine::rest_order(OrderId order_id) {
    const auto& resting = orders_.at(order_id);
    if (resting.side == Side::Buy) bids_[resting.limit_price].push_back(order_id);
    else asks_[resting.limit_price].push_back(order_id);
}

bool MatchingEngine::cancel_order(const std::string& account_id, OrderId order_id) {
    require_account(account_id);
    auto it = orders_.find(order_id);
    if (it == orders_.end() || it->second.account_id != account_id || !active(it->second) ||
        it->second.type != OrderType::Limit) return false;

    const AccountSnapshot before = account(account_id);
    AccountSnapshot after = before;
    if (it->second.side == Side::Buy) {
        if (after.reserved_quote < it->second.reserved_remaining) throw std::logic_error("quote reservation underflow");
        after.reserved_quote -= it->second.reserved_remaining;
    } else {
        if (after.reserved_base < it->second.reserved_remaining) throw std::logic_error("base reservation underflow");
        after.reserved_base -= it->second.reserved_remaining;
    }
    auto settlement_candidate = std::make_shared<settlement::SettlementBook>(*settlement_);
    if (after.base_balance != before.base_balance || after.reserved_base != before.reserved_base) {
        settlement_candidate->replace_state(account_id, base_asset_,
            settlement::BalanceState{after.base_balance, after.reserved_base}, "match-cancel", std::to_string(order_id));
    }
    if (after.quote_balance != before.quote_balance || after.reserved_quote != before.reserved_quote) {
        settlement_candidate->replace_state(account_id, quote_asset_,
            settlement::BalanceState{after.quote_balance, after.reserved_quote}, "match-cancel", std::to_string(order_id));
    }
    *settlement_ = std::move(*settlement_candidate);
    it->second.reserved_remaining = 0;
    it->second.status = it->second.filled_quantity == 0
        ? OrderStatus::Cancelled : OrderStatus::PartiallyFilledCancelled;
    return true;
}

AccountSnapshot MatchingEngine::account(const std::string& account_id) const {
    require_account(account_id);
    const auto base = settlement_->state(account_id, base_asset_);
    const auto quote = settlement_->state(account_id, quote_asset_);
    return AccountSnapshot{base.total, quote.total, base.reserved, quote.reserved};
}

std::optional<Order> MatchingEngine::order(OrderId order_id) const {
    const auto it = orders_.find(order_id);
    if (it == orders_.end()) return std::nullopt;
    return it->second;
}

std::vector<Order> MatchingEngine::open_orders(const std::string& account_id) const {
    std::vector<Order> result;
    for (const auto& [id, order_value] : orders_) {
        (void)id;
        if (active(order_value) && (account_id.empty() || order_value.account_id == account_id)) {
            result.push_back(order_value);
        }
    }
    return result;
}

std::vector<PriceLevel> MatchingEngine::bid_depth(std::size_t max_levels) const {
    std::vector<PriceLevel> result;
    for (const auto& [price, queue] : bids_) {
        if (result.size() >= max_levels) break;
        PriceLevel level{price, 0, 0};
        for (const OrderId id : queue) {
            const auto it = orders_.find(id);
            if (it == orders_.end() || !active(it->second)) continue;
            if (level.quantity > UINT64_MAX - it->second.remaining_quantity) {
                throw std::overflow_error("bid depth quantity overflow");
            }
            level.quantity += it->second.remaining_quantity;
            ++level.order_count;
        }
        if (level.order_count > 0) result.push_back(level);
    }
    return result;
}

std::vector<PriceLevel> MatchingEngine::ask_depth(std::size_t max_levels) const {
    std::vector<PriceLevel> result;
    for (const auto& [price, queue] : asks_) {
        if (result.size() >= max_levels) break;
        PriceLevel level{price, 0, 0};
        for (const OrderId id : queue) {
            const auto it = orders_.find(id);
            if (it == orders_.end() || !active(it->second)) continue;
            if (level.quantity > UINT64_MAX - it->second.remaining_quantity) {
                throw std::overflow_error("ask depth quantity overflow");
            }
            level.quantity += it->second.remaining_quantity;
            ++level.order_count;
        }
        if (level.order_count > 0) result.push_back(level);
    }
    return result;
}

std::optional<Price> MatchingEngine::best_bid() const {
    const auto depth = bid_depth(1);
    return depth.empty() ? std::nullopt : std::optional<Price>(depth.front().price);
}

std::optional<Price> MatchingEngine::best_ask() const {
    const auto depth = ask_depth(1);
    return depth.empty() ? std::nullopt : std::optional<Price>(depth.front().price);
}

std::optional<Price> MatchingEngine::last_price() const noexcept {
    return trades_.empty() ? std::nullopt : std::optional<Price>(trades_.back().price);
}

bool MatchingEngine::validate(std::string* error) const {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    try {
        std::map<std::string, std::pair<Amount, Amount>> expected_reservations;
        std::map<OrderId, Quantity> expected_fills;
        std::map<OrderId, std::size_t> active_book_entries;
        OrderId expected_order_id = 1;

        for (const auto& [price, queue] : bids_) {
            if (price == 0) return fail("zero-price bid level");
            for (const OrderId id : queue) {
                const auto it = orders_.find(id);
                if (it == orders_.end()) return fail("bid book references unknown order");
                if (!active(it->second)) continue;
                if (it->second.side != Side::Buy || it->second.type != OrderType::Limit ||
                    it->second.limit_price != price) return fail("bid book order is in the wrong level");
                ++active_book_entries[id];
            }
        }
        for (const auto& [price, queue] : asks_) {
            if (price == 0) return fail("zero-price ask level");
            for (const OrderId id : queue) {
                const auto it = orders_.find(id);
                if (it == orders_.end()) return fail("ask book references unknown order");
                if (!active(it->second)) continue;
                if (it->second.side != Side::Sell || it->second.type != OrderType::Limit ||
                    it->second.limit_price != price) return fail("ask book order is in the wrong level");
                ++active_book_entries[id];
            }
        }

        for (const auto& [id, order_value] : orders_) {
            if (id != expected_order_id++ || order_value.id != id || order_value.sequence != id ||
                !registered_accounts_.contains(order_value.account_id)) {
                return fail("order identity, sequence, or account reference is invalid");
            }
            if (order_value.type == OrderType::Limit) {
                if (order_value.limit_price == 0 || order_value.quantity == 0 ||
                    order_value.remaining_quantity > order_value.quantity ||
                    order_value.filled_quantity > UINT64_MAX - order_value.remaining_quantity ||
                    order_value.filled_quantity + order_value.remaining_quantity != order_value.quantity) {
                    return fail("limit order quantity accounting is inconsistent");
                }
                const bool status_consistent =
                    (order_value.status == OrderStatus::Open && order_value.filled_quantity == 0 &&
                     order_value.remaining_quantity == order_value.quantity) ||
                    (order_value.status == OrderStatus::PartiallyFilled && order_value.filled_quantity > 0 &&
                     order_value.remaining_quantity > 0) ||
                    (order_value.status == OrderStatus::Filled && order_value.filled_quantity == order_value.quantity &&
                     order_value.remaining_quantity == 0) ||
                    (order_value.status == OrderStatus::Cancelled && order_value.filled_quantity == 0 &&
                     order_value.remaining_quantity == order_value.quantity) ||
                    (order_value.status == OrderStatus::PartiallyFilledCancelled && order_value.filled_quantity > 0 &&
                     order_value.remaining_quantity > 0);
                if (!status_consistent) return fail("limit order status does not agree with filled/remaining quantity");
                if (active(order_value)) {
                    if (active_book_entries[id] != 1 || order_value.remaining_quantity == 0) {
                        return fail("active limit order is not present exactly once in the book");
                    }
                    const Amount expected = order_value.side == Side::Buy
                        ? checked_product(order_value.limit_price, order_value.remaining_quantity)
                        : checked_base_quantity(order_value.remaining_quantity);
                    if (order_value.reserved_remaining != expected) return fail("active limit order reserve mismatch");
                    auto& reservation = expected_reservations[order_value.account_id];
                    if (order_value.side == Side::Buy) reservation.second = checked_add(reservation.second, expected, "expected quote reservations overflow");
                    else reservation.first = checked_add(reservation.first, expected, "expected base reservations overflow");
                } else if (order_value.reserved_remaining != 0) {
                    return fail("inactive limit order retains a reservation");
                }
            } else {
                if (active(order_value) || order_value.reserved_remaining != 0 || active_book_entries[id] != 0 ||
                    order_value.limit_price != 0) {
                    return fail("market order remains active, reserved, priced, or in the book");
                }
                if (order_value.side == Side::Buy) {
                    const bool status_consistent =
                        (order_value.status == OrderStatus::Cancelled && order_value.filled_quantity == 0) ||
                        (order_value.status == OrderStatus::Filled && order_value.filled_quantity > 0) ||
                        (order_value.status == OrderStatus::PartiallyFilledCancelled && order_value.filled_quantity > 0);
                    if (order_value.quantity != 0 || order_value.remaining_quantity != 0 || !status_consistent) {
                        return fail("market buy execution metadata is inconsistent");
                    }
                } else {
                    if (order_value.quantity == 0 ||
                        order_value.filled_quantity > UINT64_MAX - order_value.remaining_quantity ||
                        order_value.filled_quantity + order_value.remaining_quantity != order_value.quantity) {
                        return fail("market sell quantity accounting is inconsistent");
                    }
                    const bool status_consistent =
                        (order_value.status == OrderStatus::Cancelled && order_value.filled_quantity == 0 &&
                         order_value.remaining_quantity == order_value.quantity) ||
                        (order_value.status == OrderStatus::Filled && order_value.filled_quantity == order_value.quantity &&
                         order_value.remaining_quantity == 0) ||
                        (order_value.status == OrderStatus::PartiallyFilledCancelled && order_value.filled_quantity > 0 &&
                         order_value.remaining_quantity > 0);
                    if (!status_consistent) return fail("market sell status does not agree with executed quantity");
                }
            }
            expected_fills[id] = 0;
        }

        std::uint64_t expected_trade_id = 1;
        for (const auto& trade : trades_) {
            if (trade.id != expected_trade_id++) return fail("trade IDs are not contiguous");
            const auto maker_it = orders_.find(trade.maker_order_id);
            const auto taker_it = orders_.find(trade.taker_order_id);
            if (maker_it == orders_.end() || taker_it == orders_.end()) return fail("trade references unknown order");
            if (maker_it->second.side == taker_it->second.side || trade.quantity == 0 || trade.price == 0) {
                return fail("trade side, price, or quantity is invalid");
            }
            const Order& buy = maker_it->second.side == Side::Buy ? maker_it->second : taker_it->second;
            const Order& sell = maker_it->second.side == Side::Sell ? maker_it->second : taker_it->second;
            if (buy.account_id != trade.buyer || sell.account_id != trade.seller ||
                trade.price != maker_it->second.limit_price ||
                trade.quote_amount != checked_product(trade.price, trade.quantity)) {
                return fail("trade settlement fields are inconsistent");
            }
            expected_fills[trade.maker_order_id] = checked_add(expected_fills[trade.maker_order_id], trade.quantity, "maker fill sum overflow");
            expected_fills[trade.taker_order_id] = checked_add(expected_fills[trade.taker_order_id], trade.quantity, "taker fill sum overflow");
        }

        if (next_order_id_ != expected_order_id) return fail("next order ID does not follow persisted order history");
        if (next_trade_id_ != expected_trade_id) return fail("next trade ID does not follow persisted trade history");
        for (const auto& [id, order_value] : orders_) {
            if (expected_fills[id] != order_value.filled_quantity) return fail("order filled quantity differs from trade tape");
        }
        for (const auto& account_id : registered_accounts_) {
            const auto expected = expected_reservations[account_id];
            const AccountSnapshot funds = account(account_id);
            if (funds.reserved_base != expected.first ||
                funds.reserved_quote != expected.second) return fail("account reservation totals do not match open orders");
            if (funds.reserved_base > funds.base_balance ||
                funds.reserved_quote > funds.quote_balance) return fail("account reservation exceeds total balance");
        }
        return true;
    } catch (const std::exception& exception) {
        return fail(exception.what());
    }
}

Snapshot MatchingEngine::snapshot() const {
    Snapshot result;
    result.registered_accounts.assign(registered_accounts_.begin(), registered_accounts_.end());
    result.orders.reserve(orders_.size());
    for (const auto& [id, order_value] : orders_) { (void)id; result.orders.push_back(order_value); }
    result.trades = trades_;
    result.next_order_id = next_order_id_;
    result.next_trade_id = next_trade_id_;
    return result;
}

MatchingEngine MatchingEngine::restore(std::shared_ptr<settlement::SettlementBook> settlement_book,
                                       const std::string& base_asset,
                                       const std::string& quote_asset,
                                       Amount base_lot_size,
                                       const Snapshot& snapshot_value) {
    MatchingEngine result(std::move(settlement_book), base_asset, quote_asset, base_lot_size);
    if (snapshot_value.registered_accounts.size() > 100'000 || snapshot_value.orders.size() > 1'000'000 ||
        snapshot_value.trades.size() > 5'000'000 || snapshot_value.next_order_id == 0 ||
        snapshot_value.next_trade_id == 0) throw std::runtime_error("matching snapshot exceeds limits or has invalid counters");
    for (const auto& account_id : snapshot_value.registered_accounts) {
        if (!result.settlement_->has_account(account_id) || !result.registered_accounts_.insert(account_id).second) {
            throw std::runtime_error("matching snapshot references missing or duplicate settlement account");
        }
    }
    for (const auto& order_value : snapshot_value.orders) {
        if (order_value.id == 0 || order_value.sequence == 0 || order_value.account_id.empty() ||
            !result.registered_accounts_.contains(order_value.account_id) ||
            (order_value.side != Side::Buy && order_value.side != Side::Sell) ||
            (order_value.type != OrderType::Limit && order_value.type != OrderType::Market) ||
            static_cast<unsigned>(order_value.status) > static_cast<unsigned>(OrderStatus::PartiallyFilledCancelled) ||
            !result.orders_.emplace(order_value.id, order_value).second) {
            throw std::runtime_error("invalid or duplicate order in matching snapshot");
        }
        if (order_value.type == OrderType::Limit && result.active(order_value)) result.rest_order(order_value.id);
    }
    result.trades_ = snapshot_value.trades;
    result.next_order_id_ = snapshot_value.next_order_id;
    result.next_trade_id_ = snapshot_value.next_trade_id;
    OrderId maximum_order_id = 0;
    for (const auto& [id, order_value] : result.orders_) {
        (void)order_value;
        maximum_order_id = std::max(maximum_order_id, id);
    }
    if (result.next_order_id_ <= maximum_order_id ||
        result.next_trade_id_ != static_cast<std::uint64_t>(result.trades_.size()) + 1) {
        throw std::runtime_error("matching snapshot sequence counters are inconsistent with stored records");
    }
    std::string error;
    if (!result.validate(&error)) throw std::runtime_error("matching snapshot validation failed: " + error);
    return result;
}

MatchingEngine MatchingEngine::with_settlement(std::shared_ptr<settlement::SettlementBook> settlement_book) const {
    MatchingEngine copy = *this;
    if (!settlement_book) throw std::invalid_argument("matching engine requires a settlement book");
    copy.settlement_ = std::move(settlement_book);
    return copy;
}

const char* to_string(Side side) noexcept {
    return side == Side::Buy ? "BUY" : "SELL";
}

const char* to_string(OrderStatus status) noexcept {
    switch (status) {
        case OrderStatus::Open: return "OPEN";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::PartiallyFilledCancelled: return "PARTIALLY_FILLED_CANCELLED";
    }
    return "UNKNOWN";
}

} // namespace feinstat::exchange
