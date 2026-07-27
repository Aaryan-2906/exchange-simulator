#pragma once

#include "order.hpp"

#include <list>
#include <map>
#include <unordered_map>
#include <optional>

namespace exsim {

// A single price level: all orders resting at that exact price,
// stored oldest-first so the front of the list is next in time priority.
using PriceLevel = std::list<Order>;

// OrderBook is a PURE DATA STRUCTURE. It knows how to store, find, and
// remove resting orders efficiently. It does NOT know how to match orders
// against each other -- that logic lives in MatchingEngine. This separation
// matters: OrderBook is easy to unit-test in isolation (did I insert/remove
// correctly?), independent of matching-rule correctness.
class OrderBook {
public:
    // Insert a resting order into the book. Assumes remaining_qty > 0.
    void insert_order(const Order& order);

    // Remove an order entirely (used for cancel, and for cleanup after a
    // resting order is fully filled). Returns false if the id wasn't found.
    bool remove_order(OrderId id);

    // Look up a resting order by id (used for cancel/modify validation and
    // for reading its current remaining_qty). Returns nullptr if not found.
    Order* find_order(OrderId id);

    // Reduce a resting order's remaining_qty by filled_qty (called by the
    // matching engine as trades occur). If the order becomes fully filled,
    // it is automatically removed from the book.
    void fill_order(OrderId id, Quantity filled_qty);

    bool has_bids() const { return !bids_.empty(); }
    bool has_asks() const { return !asks_.empty(); }

    // Best bid = highest price a buyer is willing to pay.
    // Best ask = lowest price a seller is willing to accept.
    Price best_bid_price() const { return bids_.begin()->first; }
    Price best_ask_price() const { return asks_.begin()->first; }

    // The list of orders resting at the best price, oldest first.
    PriceLevel& best_bid_level() { return bids_.begin()->second; }
    PriceLevel& best_ask_level() { return asks_.begin()->second; }

    // Total number of resting orders across the whole book (for tests/debug).
    size_t order_count() const { return order_index_.size(); }

private:
    struct OrderLocation {
        Side side;
        Price price;
        PriceLevel::iterator it;  // stable as long as we use std::list
    };

    // Bids sorted highest price first (best bid = begin()).
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // Asks sorted lowest price first (best ask = begin()).
    std::map<Price, PriceLevel, std::less<Price>> asks_;

    // O(1) lookup from order id -> exactly where it lives in bids_/asks_,
    // so cancel/modify don't require scanning every price level.
    std::unordered_map<OrderId, OrderLocation> order_index_;

    // Erase a now-empty price level from bids_ or asks_.
    void erase_level_if_empty(Side side, Price price);
};

} // namespace exsim
