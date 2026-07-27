#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace exsim {

// Side of the market this order is on.
enum class Side : uint8_t {
    Buy,
    Sell
};

// Order type. Phase 1 supports Limit, Market, and IOC (Immediate-Or-Cancel).
// FOK (Fill-Or-Kill) and GTC (Good-Til-Cancelled semantics beyond "resting
// until cancelled", which is already the default) are stretch additions.
enum class OrderType : uint8_t {
    Limit,
    Market,
    IOC   // Immediate-Or-Cancel: fill what you can right now, cancel the rest
};

enum class OrderStatus : uint8_t {
    New,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

using OrderId    = uint64_t;
using Quantity   = uint64_t;
using Price      = int64_t;   // price in integer ticks, NOT double -- see README
using Timestamp  = uint64_t;  // nanoseconds since engine start, monotonic

struct Order {
    OrderId     id;
    Side        side;
    OrderType   type;
    Price       price;            // ignored for Market orders
    Quantity    quantity;         // original quantity requested
    Quantity    remaining_qty;    // quantity still unfilled
    Timestamp   timestamp;        // used for time-priority within a price level
    OrderStatus status = OrderStatus::New;

    bool is_fully_filled() const {
        return remaining_qty == 0;
    }
};

struct Trade {
    OrderId   resting_order_id;   // the order that was already on the book
    OrderId   aggressor_order_id; // the incoming order that caused the match
    Price     price;              // trade executes at the RESTING order's price
    Quantity  quantity;
    Timestamp timestamp;
};

} // namespace exsim
