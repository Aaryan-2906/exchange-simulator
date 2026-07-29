#include <gtest/gtest.h>
#include "gateway_protocol.hpp"

using namespace exsim;

TEST(GatewayProtocolTest, ParsesNewLimitBuyOrder) {
    auto cmd = parse_gateway_line("NEW BUY LIMIT 10050 10 1001");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->type, CommandType::New);
    EXPECT_EQ(cmd->order.side, Side::Buy);
    EXPECT_EQ(cmd->order.type, OrderType::Limit);
    EXPECT_EQ(cmd->order.price, 10050);
    EXPECT_EQ(cmd->order.quantity, 10u);
    EXPECT_EQ(cmd->order.id, 1001u);
}

TEST(GatewayProtocolTest, ParsesNewMarketSellOrder) {
    auto cmd = parse_gateway_line("NEW SELL MARKET 0 25 2002");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->order.side, Side::Sell);
    EXPECT_EQ(cmd->order.type, OrderType::Market);
    EXPECT_EQ(cmd->order.quantity, 25u);
}

TEST(GatewayProtocolTest, ParsesIOCOrder) {
    auto cmd = parse_gateway_line("NEW BUY IOC 9950 5 3003");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->order.type, OrderType::IOC);
}

TEST(GatewayProtocolTest, ParsesCancel) {
    auto cmd = parse_gateway_line("CANCEL 1001");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->type, CommandType::Cancel);
    EXPECT_EQ(cmd->cancel_target, 1001u);
}

TEST(GatewayProtocolTest, RejectsUnknownVerb) {
    EXPECT_FALSE(parse_gateway_line("FOO BAR").has_value());
}

TEST(GatewayProtocolTest, RejectsInvalidSide) {
    EXPECT_FALSE(parse_gateway_line("NEW SIDEWAYS LIMIT 100 10 1").has_value());
}

TEST(GatewayProtocolTest, RejectsInvalidOrderType) {
    EXPECT_FALSE(parse_gateway_line("NEW BUY BANANA 100 10 1").has_value());
}

TEST(GatewayProtocolTest, RejectsZeroQuantity) {
    EXPECT_FALSE(parse_gateway_line("NEW BUY LIMIT 100 0 1").has_value());
}

TEST(GatewayProtocolTest, RejectsMissingFields) {
    EXPECT_FALSE(parse_gateway_line("NEW BUY LIMIT 100").has_value());
}

TEST(GatewayProtocolTest, RejectsEmptyLine) {
    EXPECT_FALSE(parse_gateway_line("").has_value());
}

TEST(GatewayProtocolTest, StatusToStringCoversAllStatuses) {
    EXPECT_EQ(status_to_string(OrderStatus::New), "NEW");
    EXPECT_EQ(status_to_string(OrderStatus::PartiallyFilled), "PARTIAL");
    EXPECT_EQ(status_to_string(OrderStatus::Filled), "FILLED");
    EXPECT_EQ(status_to_string(OrderStatus::Cancelled), "CANCELLED");
    EXPECT_EQ(status_to_string(OrderStatus::Rejected), "REJECTED");
}
