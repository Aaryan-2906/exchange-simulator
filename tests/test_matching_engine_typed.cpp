// TYPED TESTS: runs the SAME test bodies against MatchingEngine (OrderBook)
// and FlatMatchingEngine (FlatOrderBook). This is the correctness half of
// the Phase 3 story -- the benchmark proves FlatOrderBook is faster, this
// proves it isn't faster BECAUSE it silently broke some behavior.
//
// GoogleTest's TYPED_TEST_SUITE mechanism: write the test body once against
// a template parameter, list the concrete types to run it against, and
// GoogleTest generates and runs one test per type automatically.
#include <gtest/gtest.h>
#include "matching_engine.hpp"

using namespace exsim;

namespace {

Order limit_order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    return Order{id, side, OrderType::Limit, price, qty, qty, ts};
}

Order market_order(OrderId id, Side side, Quantity qty, Timestamp ts) {
    return Order{id, side, OrderType::Market, 0, qty, qty, ts};
}

} // namespace

template <typename EngineT>
class MatchingEngineTypedTest : public ::testing::Test {
protected:
    EngineT engine;
};

using EngineTypes = ::testing::Types<MatchingEngine, FlatMatchingEngine>;
TYPED_TEST_SUITE(MatchingEngineTypedTest, EngineTypes);

TYPED_TEST(MatchingEngineTypedTest, PricePriorityMatchesCheapestAskFirst) {
    this->engine.submit_order(limit_order(1, Side::Sell, 101, 10, 1));
    this->engine.submit_order(limit_order(2, Side::Sell, 100, 10, 2));

    auto result = this->engine.submit_order(limit_order(3, Side::Buy, 101, 5, 3));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 2u);
    EXPECT_EQ(result.trades[0].price, 100);
}

TYPED_TEST(MatchingEngineTypedTest, TimePriorityMatchesEarliestOrderFirstAtSamePrice) {
    this->engine.submit_order(limit_order(1, Side::Sell, 100, 5, 1));
    this->engine.submit_order(limit_order(2, Side::Sell, 100, 5, 2));

    auto result = this->engine.submit_order(limit_order(3, Side::Buy, 100, 5, 3));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 1u);
}

TYPED_TEST(MatchingEngineTypedTest, MarketOrderSweepsMultipleLevels) {
    this->engine.submit_order(limit_order(1, Side::Sell, 100, 5, 1));
    this->engine.submit_order(limit_order(2, Side::Sell, 101, 5, 2));

    auto result = this->engine.submit_order(market_order(3, Side::Buy, 10, 3));

    ASSERT_EQ(result.trades.size(), 2u);
    EXPECT_EQ(result.final_status, OrderStatus::Filled);
    EXPECT_EQ(this->engine.book().order_count(), 0u);
}

TYPED_TEST(MatchingEngineTypedTest, PartialFillLeavesRemainderResting) {
    this->engine.submit_order(limit_order(1, Side::Sell, 100, 4, 1));

    auto result = this->engine.submit_order(limit_order(2, Side::Buy, 100, 10, 2));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.remaining_qty, 6u);
    EXPECT_EQ(this->engine.book().order_count(), 1u);
}

TYPED_TEST(MatchingEngineTypedTest, CancelRemovesRestingOrder) {
    this->engine.submit_order(limit_order(1, Side::Buy, 100, 10, 1));

    EXPECT_TRUE(this->engine.cancel_order(1));
    EXPECT_EQ(this->engine.book().order_count(), 0u);
}
