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

Order ioc_order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    return Order{id, side, OrderType::IOC, price, qty, qty, ts};
}

} // namespace

// --- Basic resting behavior ---

TEST(MatchingEngineTest, LimitOrderRestsWhenNoCross) {
    MatchingEngine engine;
    auto result = engine.submit_order(limit_order(1, Side::Buy, 100, 10, 1));

    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.final_status, OrderStatus::New);
    EXPECT_EQ(engine.book().order_count(), 1u);
}

// --- Price priority ---

TEST(MatchingEngineTest, PricePriorityMatchesCheapestAskFirst) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 101, 10, 1));  // worse price, placed first
    engine.submit_order(limit_order(2, Side::Sell, 100, 10, 2));  // better price, placed second

    auto result = engine.submit_order(limit_order(3, Side::Buy, 101, 5, 3));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 2u);  // cheaper ask wins despite later timestamp
    EXPECT_EQ(result.trades[0].price, 100);
}

// --- Time priority within the same price level ---

TEST(MatchingEngineTest, TimePriorityMatchesEarliestOrderFirstAtSamePrice) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 5, 1));   // placed first
    engine.submit_order(limit_order(2, Side::Sell, 100, 5, 2));   // placed second, same price

    auto result = engine.submit_order(limit_order(3, Side::Buy, 100, 5, 3));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 1u);  // FIFO: earliest at this price wins
}

// --- Partial fills ---

TEST(MatchingEngineTest, IncomingOrderPartiallyFilledThenRests) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 4, 1));

    auto result = engine.submit_order(limit_order(2, Side::Buy, 100, 10, 2));

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].quantity, 4u);
    EXPECT_EQ(result.final_status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(result.remaining_qty, 6u);
    // The unfilled remainder (qty 6) should now be resting on the book.
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_bid_price(), 100);
}

TEST(MatchingEngineTest, RestingOrderPartiallyFilledStaysOnBookWithReducedQty) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 10, 1));

    engine.submit_order(limit_order(2, Side::Buy, 100, 4, 2));

    Order* remaining = const_cast<OrderBook&>(engine.book()).find_order(1);
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(remaining->remaining_qty, 6u);
}

// --- Market orders never rest ---

TEST(MatchingEngineTest, MarketOrderSweepsAvailableLiquidity) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 5, 1));
    engine.submit_order(limit_order(2, Side::Sell, 101, 5, 2));

    auto result = engine.submit_order(market_order(3, Side::Buy, 10, 3));

    ASSERT_EQ(result.trades.size(), 2u);
    EXPECT_EQ(result.final_status, OrderStatus::Filled);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(MatchingEngineTest, MarketOrderUnfilledRemainderIsCancelledNotRested) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 3, 1));

    auto result = engine.submit_order(market_order(2, Side::Buy, 10, 2));

    EXPECT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.remaining_qty, 7u);
    EXPECT_EQ(result.final_status, OrderStatus::PartiallyFilled);
    // The unfilled 7 units must NOT be resting on the book.
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(MatchingEngineTest, MarketOrderAgainstEmptyBookIsFullyCancelled) {
    MatchingEngine engine;
    auto result = engine.submit_order(market_order(1, Side::Buy, 10, 1));

    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.final_status, OrderStatus::Cancelled);
    EXPECT_EQ(result.remaining_qty, 10u);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// --- IOC behaves like Limit for price, like Market for not resting ---

TEST(MatchingEngineTest, IOCRespectsLimitPriceAndDoesNotRestRemainder) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 105, 5, 1));  // worse than IOC's limit

    auto result = engine.submit_order(ioc_order(2, Side::Buy, 100, 5, 2));

    EXPECT_TRUE(result.trades.empty());  // 105 > IOC's limit of 100, can't cross
    EXPECT_EQ(result.final_status, OrderStatus::Cancelled);
    EXPECT_EQ(engine.book().order_count(), 1u);  // the resting sell order is untouched
}

TEST(MatchingEngineTest, IOCPartialFillCancelsRemainder) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 3, 1));

    auto result = engine.submit_order(ioc_order(2, Side::Buy, 100, 10, 2));

    EXPECT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.remaining_qty, 7u);
    EXPECT_EQ(result.final_status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(engine.book().order_count(), 0u);  // remainder cancelled, not resting
}

// --- Cancel ---

TEST(MatchingEngineTest, CancelRemovesRestingOrder) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Buy, 100, 10, 1));

    EXPECT_TRUE(engine.cancel_order(1));
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(MatchingEngineTest, CancelNonexistentOrderReturnsFalse) {
    MatchingEngine engine;
    EXPECT_FALSE(engine.cancel_order(999));
}

// --- Modify ---

TEST(MatchingEngineTest, ModifyQuantityDecreaseKeepsTimePriority) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 10, 1));  // placed first
    engine.submit_order(limit_order(2, Side::Sell, 100, 10, 2));  // placed second

    ASSERT_TRUE(engine.modify_order(1, 100, 3, 99));  // decrease qty, same price

    // Order 1 should still be first in the FIFO queue at this price.
    auto result = engine.submit_order(limit_order(3, Side::Buy, 100, 3, 3));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 1u);
    EXPECT_EQ(result.trades[0].quantity, 3u);
}

TEST(MatchingEngineTest, ModifyPriceChangeLosesTimePriority) {
    MatchingEngine engine;
    engine.submit_order(limit_order(1, Side::Sell, 100, 5, 1));  // placed first at 100
    engine.submit_order(limit_order(2, Side::Sell, 100, 5, 2));  // placed second at 100

    // Order 1 moves its price away and back to 100 -- should now be
    // behind order 2 in time priority since it re-entered the queue later.
    ASSERT_TRUE(engine.modify_order(1, 101, 5, 50));
    ASSERT_TRUE(engine.modify_order(1, 100, 5, 60));

    auto result = engine.submit_order(limit_order(3, Side::Buy, 100, 5, 3));
    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].resting_order_id, 2u);  // order 2 now has priority
}

TEST(MatchingEngineTest, ModifyNonexistentOrderReturnsFalse) {
    MatchingEngine engine;
    EXPECT_FALSE(engine.modify_order(999, 100, 5, 1));
}
