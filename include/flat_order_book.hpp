#pragma once

#include "order.hpp"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <vector>

namespace exsim {

// FlatOrderBook: same public interface and same correctness guarantees as
// OrderBook, but backed by a SORTED VECTOR of price levels instead of
// std::map (a red-black tree).
//
// WHY: std::map's tree nodes are individually heap-allocated and scattered
// in memory -- walking the tree (or even just landing on a given node)
// means chasing pointers, which is unfriendly to CPU cache. A sorted
// vector keeps all price levels CONTIGUOUS in memory, so scanning across
// several levels (e.g. a market order sweeping through 5 price levels)
// touches memory that's already likely in cache, instead of jumping
// around the heap.
//
// TRADE-OFF, stated honestly: inserting a brand-new price level into a
// sorted vector is O(n) in the number of DISTINCT PRICE LEVELS (shifting
// everything after the insertion point), versus O(log n) for std::map.
// This is a deliberate bet: in a real book, the number of distinct price
// levels is typically small (tens, not thousands) even when the number
// of individual ORDERS is huge, so the O(n) shift is cheap in practice --
// and it's the price-level lookup (best price, and sweeps across levels)
// that happens far more often and benefits from cache locality. This is
// exactly the kind of trade-off that needs a BENCHMARK to justify, not
// just an argument -- see bench/benchmark.cpp before/after results.
class FlatOrderBook {
public:
    void insert_order(const Order& order);
    bool remove_order(OrderId id);
    Order* find_order(OrderId id);
    void fill_order(OrderId id, Quantity filled_qty);

    bool has_bids() const { return !bids_.empty(); }
    bool has_asks() const { return !asks_.empty(); }

    // Bids are stored descending by price -> best bid is index 0.
    // Asks are stored ascending by price -> best ask is index 0.
    Price best_bid_price() const { return bids_.front().price; }
    Price best_ask_price() const { return asks_.front().price; }

    std::list<Order>& best_bid_level() { return bids_.front().orders; }
    std::list<Order>& best_ask_level() { return asks_.front().orders; }

    size_t order_count() const { return order_index_.size(); }

private:
    struct PriceLevelEntry {
        Price price;
        std::list<Order> orders;
    };

    struct OrderLocation {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };

    std::vector<PriceLevelEntry> bids_;  // sorted descending
    std::vector<PriceLevelEntry> asks_;  // sorted ascending
    std::unordered_map<OrderId, OrderLocation> order_index_;

    // Binary search for the level at `price`. Returns end-iterator-like
    // "not found" via the returned pointer being nullptr.
    PriceLevelEntry* find_level(std::vector<PriceLevelEntry>& levels, Price price, bool descending);
    PriceLevelEntry& get_or_create_level(std::vector<PriceLevelEntry>& levels, Price price, bool descending);
    void erase_level_if_empty(std::vector<PriceLevelEntry>& levels, Price price, bool descending);
};

} // namespace exsim
