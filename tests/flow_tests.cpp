#include "test.hpp"

#include "feinstat/flow/amm.hpp"

#include <memory>
#include <random>

using namespace feinstat::flow;

FS_TEST(constant_product_quote_swap_updates_reserves_and_fee_accounting) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "FST", 1'000);
    engine.credit("lp", "USD", 1'000);
    engine.credit("trader", "FST", 100);
    const auto pool_id = engine.create_pool("FST", "USD", 30);
    const auto liquidity = engine.add_liquidity("lp", pool_id, 1'000, 1'000);
    FS_REQUIRE(liquidity.shares == 1'000);
    FS_REQUIRE(engine.lp_shares_of("lp", pool_id) == 1'000);

    const auto quote = engine.quote(pool_id, "FST", 100);
    FS_REQUIRE(quote.amount_out == 90);
    FS_REQUIRE(quote.fee_amount == 1);
    FS_REQUIRE(quote.price_impact_bps > 0);
    const auto swap = engine.swap("trader", pool_id, "FST", 100, 90);
    FS_REQUIRE(swap.amount_out == quote.amount_out);
    FS_REQUIRE(engine.balance_of("trader", "FST") == 0);
    FS_REQUIRE(engine.balance_of("trader", "USD") == 90);
    const auto updated_pool = engine.pool(pool_id).value();
    FS_REQUIRE(updated_pool.reserve_x == 1'100);
    FS_REQUIRE(updated_pool.reserve_y == 910);
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(minimum_output_failure_is_atomic) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "AAA", 1'000);
    engine.credit("lp", "BBB", 1'000);
    engine.credit("trader", "AAA", 100);
    const auto pool_id = engine.create_pool("AAA", "BBB");
    (void)engine.add_liquidity("lp", pool_id, 1'000, 1'000);
    const auto before = engine.pool(pool_id).value();
    const auto before_input = engine.balance_of("trader", "AAA");
    const auto before_output = engine.balance_of("trader", "BBB");
    FS_REQUIRE_THROWS(engine.swap("trader", pool_id, "AAA", 100, 99));
    const auto after = engine.pool(pool_id).value();
    FS_REQUIRE(after.reserve_x == before.reserve_x);
    FS_REQUIRE(after.reserve_y == before.reserve_y);
    FS_REQUIRE(engine.balance_of("trader", "AAA") == before_input);
    FS_REQUIRE(engine.balance_of("trader", "BBB") == before_output);
    FS_REQUIRE(engine.swaps().empty());
}

FS_TEST(liquidity_add_remove_tracks_provider_shares_and_reserves) {
    AmmEngine engine;
    engine.create_account("lp-1");
    engine.create_account("lp-2");
    engine.credit("lp-1", "X", 2'000);
    engine.credit("lp-1", "Y", 2'000);
    engine.credit("lp-2", "X", 200);
    engine.credit("lp-2", "Y", 200);
    const auto pool_id = engine.create_pool("X", "Y");
    const auto initial = engine.add_liquidity("lp-1", pool_id, 1'000, 1'000);
    FS_REQUIRE(initial.shares == 1'000);
    const auto second = engine.add_liquidity("lp-2", pool_id, 100, 100);
    FS_REQUIRE(second.shares == 100);
    FS_REQUIRE(second.amount_x_used == 100);
    FS_REQUIRE(second.amount_y_used == 100);
    const auto removed = engine.remove_liquidity("lp-2", pool_id, 100);
    FS_REQUIRE(removed.amount_x_used == 100);
    FS_REQUIRE(removed.amount_y_used == 100);
    FS_REQUIRE(engine.lp_shares_of("lp-2", pool_id) == 0);
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(amm_rejects_invalid_inputs_insufficient_funds_and_bad_pool_state) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "X", 100);
    engine.credit("lp", "Y", 100);
    const auto pool_id = engine.create_pool("X", "Y");
    FS_REQUIRE_THROWS(engine.create_pool("X", "X"));
    FS_REQUIRE_THROWS(engine.add_liquidity("lp", pool_id, 0, 10));
    (void)engine.add_liquidity("lp", pool_id, 100, 100);
    FS_REQUIRE_THROWS(engine.quote(pool_id, "X", 0));
    FS_REQUIRE_THROWS(engine.swap("trader", pool_id, "X", 1, 1));
    FS_REQUIRE_THROWS(engine.remove_liquidity("trader", pool_id, 1));
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(amm_supports_reverse_direction_and_proportional_liquidity_caps) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "X", 2'000);
    engine.credit("lp", "Y", 2'000);
    engine.credit("trader", "Y", 100);
    const auto pool_id = engine.create_pool("X", "Y", 30);
    (void)engine.add_liquidity("lp", pool_id, 1'000, 1'000);

    const auto added = engine.add_liquidity("lp", pool_id, 200, 100);
    FS_REQUIRE(added.shares == 100);
    FS_REQUIRE(added.amount_x_used == 100);
    FS_REQUIRE(added.amount_y_used == 100);
    FS_REQUIRE(engine.balance_of("lp", "X") == 900);
    FS_REQUIRE(engine.balance_of("lp", "Y") == 900);

    const auto reverse_quote = engine.quote(pool_id, "Y", 100);
    FS_REQUIRE(reverse_quote.asset_out == "X");
    FS_REQUIRE(reverse_quote.amount_out > 0);
    const auto reverse_swap = engine.swap("trader", pool_id, "Y", 100, reverse_quote.amount_out);
    FS_REQUIRE(reverse_swap.amount_out == reverse_quote.amount_out);
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(amm_output_balance_overflow_rejects_swap_without_partial_state_change) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "X", 1'000);
    engine.credit("lp", "Y", 1'000);
    engine.credit("trader", "X", 100);
    engine.credit("trader", "Y", UINT64_MAX - 5);
    const auto pool_id = engine.create_pool("X", "Y");
    (void)engine.add_liquidity("lp", pool_id, 1'000, 1'000);
    const auto pool_before = engine.pool(pool_id).value();
    FS_REQUIRE_THROWS(engine.swap("trader", pool_id, "X", 100, 1));
    const auto pool_after = engine.pool(pool_id).value();
    FS_REQUIRE(pool_after.reserve_x == pool_before.reserve_x);
    FS_REQUIRE(pool_after.reserve_y == pool_before.reserve_y);
    FS_REQUIRE(engine.balance_of("trader", "X") == 100);
    FS_REQUIRE(engine.balance_of("trader", "Y") == UINT64_MAX - 5);
    FS_REQUIRE(engine.swaps().empty());
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(amm_wide_intermediates_handle_extreme_valid_reserves_without_overflow) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "X", 1);
    engine.credit("lp", "Y", UINT64_MAX);
    engine.credit("trader", "X", UINT64_MAX / 2);
    const auto pool_id = engine.create_pool("X", "Y", 30);
    (void)engine.add_liquidity("lp", pool_id, 1, UINT64_MAX);

    const auto quote = engine.quote(pool_id, "X", UINT64_MAX / 2);
    FS_REQUIRE(quote.amount_out > 0);
    FS_REQUIRE(quote.amount_out < UINT64_MAX);
    FS_REQUIRE(quote.price_impact_bps <= kBasisPoints);
    FS_REQUIRE(engine.validate_pool(pool_id));
}

FS_TEST(amm_invariants_hold_across_deterministic_bidirectional_swap_stress) {
    AmmEngine engine;
    engine.create_account("lp");
    engine.create_account("trader-x");
    engine.create_account("trader-y");
    engine.credit("lp", "X", 1'000'000'000);
    engine.credit("lp", "Y", 1'000'000'000);
    engine.credit("trader-x", "X", 1'000'000);
    engine.credit("trader-y", "Y", 1'000'000);
    const auto pool_id = engine.create_pool("X", "Y", 30);
    (void)engine.add_liquidity("lp", pool_id, 1'000'000'000, 1'000'000'000);
    std::mt19937_64 random(0xF10A55EEDULL);

    for (std::uint64_t i = 0; i < 400; ++i) {
        const bool x_to_y = (i & 1U) == 0;
        const std::string asset_in = x_to_y ? "X" : "Y";
        const std::string trader = x_to_y ? "trader-x" : "trader-y";
        const feinstat::Amount amount_in = 100 + (random() % 900);
        const auto quotation = engine.quote(pool_id, asset_in, amount_in);
        const auto executed = engine.swap(trader, pool_id, asset_in, amount_in, quotation.amount_out);
        FS_REQUIRE(executed.amount_out == quotation.amount_out);
        std::string error;
        if (!engine.validate_pool(pool_id, &error)) {
            throw std::runtime_error("AMM stress invariant failed: " + error);
        }
    }
    FS_REQUIRE(engine.swaps().size() == 400);
    FS_REQUIRE(engine.validate_pool(pool_id));
}


FS_TEST(amm_snapshot_rejects_reused_pool_and_swap_counters) {
    auto book = std::make_shared<feinstat::settlement::SettlementBook>();
    AmmEngine engine(book);
    engine.create_account("lp");
    engine.create_account("trader");
    engine.credit("lp", "X", 1'000);
    engine.credit("lp", "Y", 1'000);
    engine.credit("trader", "X", 100);
    const auto pool_id = engine.create_pool("X", "Y");
    (void)engine.add_liquidity("lp", pool_id, 1'000, 1'000);
    const auto quotation = engine.quote(pool_id, "X", 10);
    (void)engine.swap("trader", pool_id, "X", 10, quotation.amount_out);

    auto bad_pool_counter = engine.snapshot();
    bad_pool_counter.next_pool_id = 1;
    FS_REQUIRE_THROWS(AmmEngine::restore(book, bad_pool_counter));

    auto bad_swap_counter = engine.snapshot();
    bad_swap_counter.next_swap_id = 1;
    FS_REQUIRE_THROWS(AmmEngine::restore(book, bad_swap_counter));

    auto bad_swap_pair = engine.snapshot();
    bad_swap_pair.swaps.front().asset_out = "X";
    FS_REQUIRE_THROWS(AmmEngine::restore(book, bad_swap_pair));

    auto noncanonical_pool_id = engine.snapshot();
    noncanonical_pool_id.pools.front().id = "POOL-01";
    FS_REQUIRE_THROWS(AmmEngine::restore(book, noncanonical_pool_id));
}
