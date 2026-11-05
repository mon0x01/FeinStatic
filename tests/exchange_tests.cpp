#include "test.hpp"

#include "feinstat/exchange/matching_engine.hpp"

#include <memory>
#include <random>

using namespace feinstat::exchange;

FS_TEST(limit_orders_follow_price_time_priority_and_support_partial_fills) {
    MatchingEngine engine;
    engine.create_account("buyer-1", 0, 1'000);
    engine.create_account("buyer-2", 0, 1'000);
    engine.create_account("seller", 10, 0);

    const auto first_buy = engine.submit_limit("buyer-1", Side::Buy, 100, 5);
    const auto second_buy = engine.submit_limit("buyer-2", Side::Buy, 100, 2);
    const auto sell = engine.submit_limit("seller", Side::Sell, 100, 6);

    const auto history = engine.trades();
    FS_REQUIRE(history.size() == 2);
    FS_REQUIRE(history[0].maker_order_id == first_buy);
    FS_REQUIRE(history[0].quantity == 5);
    FS_REQUIRE(history[1].maker_order_id == second_buy);
    FS_REQUIRE(history[1].quantity == 1);
    FS_REQUIRE(engine.order(sell)->status == OrderStatus::Filled);
    FS_REQUIRE(engine.order(second_buy)->remaining_quantity == 1);
    FS_REQUIRE(engine.account("buyer-1").base_balance == 5);
    FS_REQUIRE(engine.account("buyer-2").base_balance == 1);
    FS_REQUIRE(engine.account("seller").quote_balance == 600);
    FS_REQUIRE(engine.account("buyer-2").reserved_quote == 100);
    FS_REQUIRE(engine.best_bid() == 100);
    FS_REQUIRE(engine.bid_depth().front().quantity == 1);
    std::string error;
    FS_REQUIRE(engine.validate(&error));
}

FS_TEST(matching_uses_maker_price_across_price_levels) {
    MatchingEngine engine;
    engine.create_account("seller-1", 3, 0);
    engine.create_account("seller-2", 3, 0);
    engine.create_account("buyer", 0, 1'000);
    const auto expensive = engine.submit_limit("seller-1", Side::Sell, 103, 2);
    const auto cheap = engine.submit_limit("seller-2", Side::Sell, 101, 2);
    const auto incoming = engine.submit_limit("buyer", Side::Buy, 103, 3);
    (void)expensive;
    FS_REQUIRE(engine.order(incoming)->status == OrderStatus::Filled);
    const auto history = engine.trades();
    FS_REQUIRE(history.size() == 2);
    FS_REQUIRE(history[0].maker_order_id == cheap);
    FS_REQUIRE(history[0].price == 101);
    FS_REQUIRE(history[1].price == 103);
    FS_REQUIRE(history[1].quantity == 1);
    FS_REQUIRE(engine.best_ask() == 103);
}

FS_TEST(market_buy_partially_fills_and_releases_unused_quote_budget) {
    MatchingEngine engine;
    engine.create_account("seller-1", 2, 0);
    engine.create_account("seller-2", 3, 0);
    engine.create_account("buyer", 0, 250);
    (void)engine.submit_limit("seller-1", Side::Sell, 100, 2);
    const auto remaining_ask = engine.submit_limit("seller-2", Side::Sell, 110, 3);

    const auto market_order = engine.submit_market_buy("buyer", 250);
    FS_REQUIRE(engine.order(market_order)->status == OrderStatus::PartiallyFilledCancelled);
    FS_REQUIRE(engine.order(market_order)->filled_quantity == 2);
    FS_REQUIRE(engine.account("buyer").base_balance == 2);
    FS_REQUIRE(engine.account("buyer").quote_balance == 50);
    FS_REQUIRE(engine.account("buyer").reserved_quote == 0);
    FS_REQUIRE(engine.order(remaining_ask)->remaining_quantity == 3);
    std::string error;
    if (!engine.validate(&error)) throw std::runtime_error(error);
}

FS_TEST(market_sell_matches_multiple_levels_and_releases_unfilled_base) {
    MatchingEngine engine;
    engine.create_account("buyer-1", 0, 1'000);
    engine.create_account("buyer-2", 0, 1'000);
    engine.create_account("seller", 5, 0);
    (void)engine.submit_limit("buyer-1", Side::Buy, 100, 2);
    const auto second_bid = engine.submit_limit("buyer-2", Side::Buy, 90, 3);

    const auto market_order = engine.submit_market_sell("seller", 4);
    FS_REQUIRE(engine.order(market_order)->status == OrderStatus::Filled);
    FS_REQUIRE(engine.order(market_order)->filled_quantity == 4);
    FS_REQUIRE(engine.account("seller").base_balance == 1);
    FS_REQUIRE(engine.account("seller").reserved_base == 0);
    FS_REQUIRE(engine.account("seller").quote_balance == 380);
    FS_REQUIRE(engine.order(second_bid)->remaining_quantity == 1);
    FS_REQUIRE(engine.validate());
}

FS_TEST(cancel_releases_reserved_funds_and_self_trade_is_rejected) {
    MatchingEngine engine;
    engine.create_account("trader", 10, 1'000);
    const auto ask = engine.submit_limit("trader", Side::Sell, 100, 5);
    FS_REQUIRE(engine.account("trader").reserved_base == 5);
    FS_REQUIRE_THROWS(engine.submit_limit("trader", Side::Buy, 100, 1));
    FS_REQUIRE(engine.cancel_order("trader", ask));
    FS_REQUIRE(engine.account("trader").reserved_base == 0);
    FS_REQUIRE(engine.open_orders().empty());
    FS_REQUIRE(!engine.cancel_order("trader", ask));
    FS_REQUIRE(engine.validate());
}

FS_TEST(order_validation_rejects_insufficient_funds_and_overflow) {
    MatchingEngine engine;
    engine.create_account("poor", 0, 50);
    FS_REQUIRE_THROWS(engine.submit_limit("poor", Side::Buy, 100, 1));
    FS_REQUIRE_THROWS(engine.submit_limit("poor", Side::Buy, UINT64_MAX, 2));
    FS_REQUIRE_THROWS(engine.submit_market_buy("poor", 0));
    FS_REQUIRE_THROWS(engine.submit_limit("poor", static_cast<Side>(99), 1, 1));
    FS_REQUIRE(engine.validate());
}

FS_TEST(market_buy_reports_fully_filled_when_budget_is_consumed) {
    MatchingEngine engine;
    engine.create_account("seller", 1, 0);
    engine.create_account("buyer", 0, 100);
    (void)engine.submit_limit("seller", Side::Sell, 100, 1);
    const auto market_order = engine.submit_market_buy("buyer", 100);
    FS_REQUIRE(engine.order(market_order)->status == OrderStatus::Filled);
    FS_REQUIRE(engine.order(market_order)->filled_quantity == 1);
    FS_REQUIRE(engine.account("buyer").reserved_quote == 0);
    FS_REQUIRE(engine.validate());
}

FS_TEST(market_sell_cancels_unfilled_remainder_without_leaving_reservations) {
    MatchingEngine engine;
    engine.create_account("buyer", 0, 500);
    engine.create_account("seller", 5, 0);
    (void)engine.submit_limit("buyer", Side::Buy, 100, 2);
    const auto market_order = engine.submit_market_sell("seller", 5);
    FS_REQUIRE(engine.order(market_order)->status == OrderStatus::PartiallyFilledCancelled);
    FS_REQUIRE(engine.order(market_order)->filled_quantity == 2);
    FS_REQUIRE(engine.account("seller").base_balance == 3);
    FS_REQUIRE(engine.account("seller").reserved_base == 0);
    FS_REQUIRE(engine.validate());
}

FS_TEST(multi_level_match_failure_is_atomic) {
    MatchingEngine engine;
    engine.create_account("seller-1", 1, 0);
    engine.create_account("seller-2", 1, UINT64_MAX - 5);
    engine.create_account("buyer", 0, 30);
    const auto first_ask = engine.submit_limit("seller-1", Side::Sell, 10, 1);
    const auto second_ask = engine.submit_limit("seller-2", Side::Sell, 20, 1);

    // The first fill is individually valid, but crediting seller-2 at the
    // second level would overflow its quote balance. The entire incoming
    // market order must fail without committing the earlier fill.
    FS_REQUIRE_THROWS(engine.submit_market_buy("buyer", 30));
    FS_REQUIRE(engine.trades().empty());
    FS_REQUIRE(engine.account("buyer").base_balance == 0);
    FS_REQUIRE(engine.account("buyer").quote_balance == 30);
    FS_REQUIRE(engine.account("buyer").reserved_quote == 0);
    FS_REQUIRE(engine.account("seller-1").base_balance == 1);
    FS_REQUIRE(engine.account("seller-1").quote_balance == 0);
    FS_REQUIRE(engine.account("seller-1").reserved_base == 1);
    FS_REQUIRE(engine.account("seller-2").base_balance == 1);
    FS_REQUIRE(engine.account("seller-2").quote_balance == UINT64_MAX - 5);
    FS_REQUIRE(engine.order(first_ask)->status == OrderStatus::Open);
    FS_REQUIRE(engine.order(second_ask)->status == OrderStatus::Open);
    FS_REQUIRE(engine.validate());
}

FS_TEST(matching_engine_remains_consistent_under_deterministic_order_stress) {
    MatchingEngine engine;
    engine.create_account("buyer-a", 0, 1'000'000'000);
    engine.create_account("buyer-b", 0, 1'000'000'000);
    engine.create_account("seller-a", 1'000'000, 0);
    engine.create_account("seller-b", 1'000'000, 0);
    std::mt19937_64 random(0xF31A57A71CULL);

    for (std::uint64_t i = 0; i < 600; ++i) {
        const Price price = 90 + (random() % 41);
        const Quantity quantity = 1 + (random() % 5);
        if ((i & 1U) == 0) {
            const std::string account = (i & 2U) == 0 ? "buyer-a" : "buyer-b";
            (void)engine.submit_limit(account, Side::Buy, price, quantity);
        } else {
            const std::string account = (i & 2U) == 0 ? "seller-a" : "seller-b";
            (void)engine.submit_limit(account, Side::Sell, price, quantity);
        }
        if (i % 19 == 0) {
            const auto buyer_orders = engine.open_orders("buyer-a");
            if (!buyer_orders.empty()) (void)engine.cancel_order("buyer-a", buyer_orders.front().id);
            const auto seller_orders = engine.open_orders("seller-a");
            if (!seller_orders.empty()) (void)engine.cancel_order("seller-a", seller_orders.front().id);
        }
        std::string error;
        if (!engine.validate(&error)) throw std::runtime_error("matching stress invariant failed: " + error);
    }
    FS_REQUIRE(engine.validate());
}


FS_TEST(matching_snapshot_rejects_reused_order_sequence_counter) {
    auto book = std::make_shared<feinstat::settlement::SettlementBook>();
    MatchingEngine engine(book, "BASE", "QUOTE", 1);
    engine.create_account("seller", 2, 0);
    (void)engine.submit_limit("seller", Side::Sell, 10, 1);
    auto snapshot = engine.snapshot();
    snapshot.next_order_id = 1; // The next ID must be greater than persisted ID 1.
    FS_REQUIRE_THROWS(MatchingEngine::restore(book, "BASE", "QUOTE", 1, snapshot));

    auto gap_snapshot = engine.snapshot();
    gap_snapshot.orders.front().id = 2;
    gap_snapshot.orders.front().sequence = 2;
    gap_snapshot.next_order_id = 3;
    FS_REQUIRE_THROWS(MatchingEngine::restore(book, "BASE", "QUOTE", 1, gap_snapshot));
}
