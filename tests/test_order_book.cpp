#include <gtest/gtest.h>
#include "order_book.hpp"

using namespace exsim;

namespace {

Order make_order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    return Order{id, side, OrderType::Limit, price, qty, qty, ts};
}

} // namespace

TEST(OrderBookTest, InsertAndFindOrder) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    Order* found = book.find_order(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 1u);
    EXPECT_EQ(found->remaining_qty, 10u);
}

TEST(OrderBookTest, BestBidIsHighestPrice) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Buy, 105, 10, 2));
    book.insert_order(make_order(3, Side::Buy, 98, 10, 3));

    EXPECT_EQ(book.best_bid_price(), 105);
}

TEST(OrderBookTest, BestAskIsLowestPrice) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Sell, 110, 10, 1));
    book.insert_order(make_order(2, Side::Sell, 108, 10, 2));
    book.insert_order(make_order(3, Side::Sell, 115, 10, 3));

    EXPECT_EQ(book.best_ask_price(), 108);
}

TEST(OrderBookTest, FIFOTimePriorityWithinSamePriceLevel) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Buy, 100, 5, 2));
    book.insert_order(make_order(3, Side::Buy, 100, 7, 3));

    auto& level = book.best_bid_level();
    ASSERT_EQ(level.size(), 3u);
    // Front of the queue must be the FIRST order placed at this price.
    EXPECT_EQ(level.front().id, 1u);
    auto it = level.begin();
    ++it;
    EXPECT_EQ(it->id, 2u);
    ++it;
    EXPECT_EQ(it->id, 3u);
}

TEST(OrderBookTest, RemoveOrderErasesItAndEmptyLevel) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    EXPECT_TRUE(book.remove_order(1));
    EXPECT_EQ(book.find_order(1), nullptr);
    EXPECT_FALSE(book.has_bids());  // level should be cleaned up, not left empty
}

TEST(OrderBookTest, RemoveNonexistentOrderReturnsFalse) {
    OrderBook book;
    EXPECT_FALSE(book.remove_order(999));
}

TEST(OrderBookTest, FillOrderReducesRemainingQty) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    book.fill_order(1, 4);
    Order* found = book.find_order(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->remaining_qty, 6u);
}

TEST(OrderBookTest, FillOrderToZeroRemovesItFromBook) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    book.fill_order(1, 10);
    EXPECT_EQ(book.find_order(1), nullptr);
    EXPECT_FALSE(book.has_bids());
}

TEST(OrderBookTest, MultiplePriceLevelsCoexistOnBothSides) {
    OrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Sell, 105, 10, 2));

    EXPECT_TRUE(book.has_bids());
    EXPECT_TRUE(book.has_asks());
    EXPECT_EQ(book.order_count(), 2u);
}
