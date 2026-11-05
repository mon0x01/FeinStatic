#pragma once

#include "feinstat/common/types.hpp"
#include "feinstat/settlement/settlement_book.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace feinstat::exchange {

using OrderId = std::uint64_t;
using Price = std::uint64_t;      // quote-asset atoms per whole base lot
using Quantity = std::uint64_t;  // whole base lots

enum class Side { Buy, Sell };
enum class OrderType { Limit, Market };
enum class OrderStatus { Open, PartiallyFilled, Filled, Cancelled, PartiallyFilledCancelled };

struct Order {
    OrderId id{0};
    std::uint64_t sequence{0};
    std::string account_id;
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price limit_price{0};
    Quantity quantity{0};
    Quantity remaining_quantity{0};
    Quantity filled_quantity{0};
    Amount reserved_remaining{0};
    OrderStatus status{OrderStatus::Open};
};

struct Trade {
    std::uint64_t id{0};
    OrderId maker_order_id{0};
    OrderId taker_order_id{0};
    std::string buyer;
    std::string seller;
    Price price{0};
    Quantity quantity{0};
    Amount quote_amount{0};
};

struct AccountSnapshot {
    Amount base_balance{0};
    Amount quote_balance{0};
    Amount reserved_base{0};
    Amount reserved_quote{0};
    [[nodiscard]] Amount available_base() const noexcept { return base_balance - reserved_base; }
    [[nodiscard]] Amount available_quote() const noexcept { return quote_balance - reserved_quote; }
};

struct PriceLevel {
    Price price{0};
    Quantity quantity{0};
    std::size_t order_count{0};
};

struct Snapshot {
    std::vector<std::string> registered_accounts;
    std::vector<Order> orders;
    std::vector<Trade> trades;
    OrderId next_order_id{1};
    std::uint64_t next_trade_id{1};
};

class MatchingEngine {
public:
    MatchingEngine();
    explicit MatchingEngine(std::shared_ptr<settlement::SettlementBook> settlement,
                            std::string base_asset = "BASE",
                            std::string quote_asset = "QUOTE",
                            Amount base_lot_size = 1);

    // Legacy standalone account creation funds the two market assets. In a
    // shared platform use register_account() after creating the canonical
    // account once in SettlementBook.
    void create_account(const std::string& account_id, Amount base_balance, Amount quote_balance);
    void register_account(const std::string& account_id);

    [[nodiscard]] OrderId submit_limit(const std::string& account_id,
                                       Side side, Price price, Quantity quantity);
    [[nodiscard]] OrderId submit_market_buy(const std::string& account_id, Amount quote_budget);
    [[nodiscard]] OrderId submit_market_sell(const std::string& account_id, Quantity quantity);
    [[nodiscard]] bool cancel_order(const std::string& account_id, OrderId order_id);

    [[nodiscard]] AccountSnapshot account(const std::string& account_id) const;
    [[nodiscard]] std::optional<Order> order(OrderId order_id) const;
    [[nodiscard]] std::vector<Order> open_orders(const std::string& account_id = {}) const;
    [[nodiscard]] const std::vector<Trade>& trades() const noexcept { return trades_; }
    [[nodiscard]] std::vector<PriceLevel> bid_depth(std::size_t max_levels = 20) const;
    [[nodiscard]] std::vector<PriceLevel> ask_depth(std::size_t max_levels = 20) const;
    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;
    [[nodiscard]] std::optional<Price> last_price() const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] bool validate(std::string* error = nullptr) const;

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] static MatchingEngine restore(std::shared_ptr<settlement::SettlementBook> settlement,
                                                const std::string& base_asset,
                                                const std::string& quote_asset,
                                                Amount base_lot_size,
                                                const Snapshot& snapshot);
    [[nodiscard]] MatchingEngine with_settlement(std::shared_ptr<settlement::SettlementBook> settlement) const;
    [[nodiscard]] const std::string& base_asset() const noexcept { return base_asset_; }
    [[nodiscard]] const std::string& quote_asset() const noexcept { return quote_asset_; }
    [[nodiscard]] Amount base_lot_size() const noexcept { return base_lot_size_; }

private:
    using BidBook = std::map<Price, std::deque<OrderId>, std::greater<Price>>;
    using AskBook = std::map<Price, std::deque<OrderId>>;

    [[nodiscard]] OrderId submit_order(Order incoming);
    void require_account(const std::string& account_id) const;
    [[nodiscard]] bool active(const Order& order) const noexcept;
    [[nodiscard]] bool has_crossing_self_order(const std::string& account_id, Side incoming_side,
                                               Price incoming_price, bool market) const;
    void rest_order(OrderId order_id);
    [[nodiscard]] Amount checked_product(Price price, Quantity quantity) const;
    [[nodiscard]] Amount checked_base_quantity(Quantity quantity) const;

    std::shared_ptr<settlement::SettlementBook> settlement_;
    std::string base_asset_{"BASE"};
    std::string quote_asset_{"QUOTE"};
    Amount base_lot_size_{1};
    std::set<std::string> registered_accounts_;
    std::map<OrderId, Order> orders_;
    BidBook bids_;
    AskBook asks_;
    std::vector<Trade> trades_;
    OrderId next_order_id_{1};
    std::uint64_t next_trade_id_{1};
};

[[nodiscard]] const char* to_string(Side side) noexcept;
[[nodiscard]] const char* to_string(OrderStatus status) noexcept;

} // namespace feinstat::exchange
