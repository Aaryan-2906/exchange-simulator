// Mirrors test_order_book.cpp exactly, but against FlatOrderBook. Running
// the identical test cases against both implementations is itself a form
// of correctness proof: if FlatOrderBook passes every test OrderBook
// passes, the optimization didn't change behavior, only performance.
#include <gtest/gtest.h>
#include "flat_order_book.hpp"

using namespace exsim;

namespace {

Order make_order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts) {
    return Order{id, side, OrderType::Limit, price, qty, qty, ts};
}

} // namespace

TEST(FlatOrderBookTest, InsertAndFindOrder) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    Order* found = book.find_order(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 1u);
    EXPECT_EQ(found->remaining_qty, 10u);
}

TEST(FlatOrderBookTest, BestBidIsHighestPrice) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Buy, 105, 10, 2));
    book.insert_order(make_order(3, Side::Buy, 98, 10, 3));

    EXPECT_EQ(book.best_bid_price(), 105);
}

TEST(FlatOrderBookTest, BestAskIsLowestPrice) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Sell, 110, 10, 1));
    book.insert_order(make_order(2, Side::Sell, 108, 10, 2));
    book.insert_order(make_order(3, Side::Sell, 115, 10, 3));

    EXPECT_EQ(book.best_ask_price(), 108);
}

TEST(FlatOrderBookTest, FIFOTimePriorityWithinSamePriceLevel) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Buy, 100, 5, 2));
    book.insert_order(make_order(3, Side::Buy, 100, 7, 3));

    auto& level = book.best_bid_level();
    ASSERT_EQ(level.size(), 3u);
    EXPECT_EQ(level.front().id, 1u);
    auto it = level.begin();
    ++it;
    EXPECT_EQ(it->id, 2u);
    ++it;
    EXPECT_EQ(it->id, 3u);
}

TEST(FlatOrderBookTest, RemoveOrderErasesItAndEmptyLevel) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    EXPECT_TRUE(book.remove_order(1));
    EXPECT_EQ(book.find_order(1), nullptr);
    EXPECT_FALSE(book.has_bids());
}

TEST(FlatOrderBookTest, RemoveNonexistentOrderReturnsFalse) {
    FlatOrderBook book;
    EXPECT_FALSE(book.remove_order(999));
}

TEST(FlatOrderBookTest, FillOrderReducesRemainingQty) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    book.fill_order(1, 4);
    Order* found = book.find_order(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->remaining_qty, 6u);
}

TEST(FlatOrderBookTest, FillOrderToZeroRemovesItFromBook) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));

    book.fill_order(1, 10);
    EXPECT_EQ(book.find_order(1), nullptr);
    EXPECT_FALSE(book.has_bids());
}

TEST(FlatOrderBookTest, MultiplePriceLevelsCoexistOnBothSides) {
    FlatOrderBook book;
    book.insert_order(make_order(1, Side::Buy, 100, 10, 1));
    book.insert_order(make_order(2, Side::Sell, 105, 10, 2));

    EXPECT_TRUE(book.has_bids());
    EXPECT_TRUE(book.has_asks());
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(FlatOrderBookTest, InsertOutOfOrderKeepsLevelsSorted) {
    FlatOrderBook book;
    // Insert bids out of price order -- vector must still report correct best.
    book.insert_order(make_order(1, Side::Buy, 100, 5, 1));
    book.insert_order(make_order(2, Side::Buy, 110, 5, 2));
    book.insert_order(make_order(3, Side::Buy, 90, 5, 3));
    book.insert_order(make_order(4, Side::Buy, 105, 5, 4));

    EXPECT_EQ(book.best_bid_price(), 110);
    book.remove_order(2);
    EXPECT_EQ(book.best_bid_price(), 105);
    book.remove_order(4);
    EXPECT_EQ(book.best_bid_price(), 100);
}
