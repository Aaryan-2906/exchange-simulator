#pragma once

#include "order.hpp"

#include <optional>
#include <sstream>
#include <string>

namespace exsim {

// A deliberately simple, human-typeable, line-based text protocol --
// NOT a real exchange protocol (real ones are binary, e.g. FIX/FAST or
// a custom binary format, for size and parsing-speed reasons). Text is
// chosen here so the gateway is trivially testable with `telnet` or
// `nc` by hand, which matters more for a portfolio project than raw
// wire efficiency does. This trade-off is worth stating directly if
// asked: "why not binary?" -- and "what would you use instead in
// production?" (a fixed-width binary struct, avoiding string parsing
// entirely) is a good follow-up to already have an answer for.
//
// Commands (one per line, newline-terminated):
//   NEW <BUY|SELL> <LIMIT|MARKET> <price> <qty> <client_order_id>
//     (price is ignored for MARKET orders, but a placeholder must still
//      be sent to keep the parser simple -- send 0)
//   CANCEL <client_order_id>
//
// Responses (one or more lines sent back to the SAME connection that
// submitted the order):
//   ACK <client_order_id> <status> <remaining_qty>
//   TRADE <client_order_id> <price> <qty>
//   ERROR <reason>

enum class CommandType {
    New,
    Cancel,
    Unknown
};

struct GatewayCommand {
    CommandType type = CommandType::Unknown;
    Order order;                 // populated for New
    OrderId cancel_target = 0;   // populated for Cancel
};

// Returns std::nullopt if the line doesn't parse -- caller is expected
// to send back an ERROR response and NOT crash or ignore silently.
inline std::optional<GatewayCommand> parse_gateway_line(const std::string& line) {
    std::istringstream iss(line);
    std::string verb;
    iss >> verb;

    if (verb == "NEW") {
        std::string side_str, type_str;
        int64_t price = 0;
        uint64_t qty = 0;
        uint64_t client_order_id = 0;

        if (!(iss >> side_str >> type_str >> price >> qty >> client_order_id)) {
            return std::nullopt;
        }

        Side side;
        if (side_str == "BUY") side = Side::Buy;
        else if (side_str == "SELL") side = Side::Sell;
        else return std::nullopt;

        OrderType type;
        if (type_str == "LIMIT") type = OrderType::Limit;
        else if (type_str == "MARKET") type = OrderType::Market;
        else if (type_str == "IOC") type = OrderType::IOC;
        else return std::nullopt;

        if (qty == 0) return std::nullopt;

        GatewayCommand cmd;
        cmd.type = CommandType::New;
        cmd.order = Order{
            static_cast<OrderId>(client_order_id),
            side,
            type,
            static_cast<Price>(price),
            static_cast<Quantity>(qty),
            static_cast<Quantity>(qty),
            0 // timestamp filled in by the gateway/matching thread, not the client
        };
        return cmd;
    }

    if (verb == "CANCEL") {
        uint64_t target = 0;
        if (!(iss >> target)) {
            return std::nullopt;
        }
        GatewayCommand cmd;
        cmd.type = CommandType::Cancel;
        cmd.cancel_target = static_cast<OrderId>(target);
        return cmd;
    }

    return std::nullopt;
}

inline std::string status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::New: return "NEW";
        case OrderStatus::PartiallyFilled: return "PARTIAL";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "UNKNOWN";
}

} // namespace exsim
