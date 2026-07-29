#include <gtest/gtest.h>
#include "trade_store.hpp"

#include <cstdio>
#include <filesystem>

using namespace exsim;

namespace {

// Use a fresh temp db file per test so tests don't interfere with each
// other or leave junk behind.
class TradeStoreTest : public ::testing::Test {
protected:
    std::string db_path;

    void SetUp() override {
        db_path = "test_trade_store_" + std::to_string(::testing::UnitTest::GetInstance()
            ->current_test_info()->line()) + ".db";
        std::filesystem::remove(db_path); // in case a previous run left it
    }

    void TearDown() override {
        std::filesystem::remove(db_path);
    }
};

} // namespace

TEST_F(TradeStoreTest, InsertAndFetchSingleTrade) {
    TradeStore store(db_path);
    Trade t{1, 2, 10050, 10, 12345};

    EXPECT_TRUE(store.insert_trade(t));

    auto trades = store.fetch_all_trades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].resting_order_id, 1u);
    EXPECT_EQ(trades[0].aggressor_order_id, 2u);
    EXPECT_EQ(trades[0].price, 10050);
    EXPECT_EQ(trades[0].quantity, 10u);
    EXPECT_EQ(trades[0].timestamp, 12345u);
}

TEST_F(TradeStoreTest, PreservesInsertionOrder) {
    TradeStore store(db_path);
    store.insert_trade(Trade{1, 2, 100, 5, 1});
    store.insert_trade(Trade{3, 4, 101, 6, 2});
    store.insert_trade(Trade{5, 6, 102, 7, 3});

    auto trades = store.fetch_all_trades();
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].timestamp, 1u);
    EXPECT_EQ(trades[1].timestamp, 2u);
    EXPECT_EQ(trades[2].timestamp, 3u);
}

TEST_F(TradeStoreTest, TradeCountMatchesInserted) {
    TradeStore store(db_path);
    for (int i = 0; i < 10; ++i) {
        store.insert_trade(Trade{static_cast<OrderId>(i), static_cast<OrderId>(i + 100),
                                   100, 1, static_cast<Timestamp>(i)});
    }
    EXPECT_EQ(store.trade_count(), 10u);
}

TEST_F(TradeStoreTest, DataSurvivesReopeningTheDatabase) {
    {
        TradeStore store(db_path);
        store.insert_trade(Trade{1, 2, 100, 5, 1});
    } // store destructed, connection closed

    TradeStore reopened(db_path);
    auto trades = reopened.fetch_all_trades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].price, 100);
}
