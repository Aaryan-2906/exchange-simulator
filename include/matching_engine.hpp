#pragma once

#include "order.hpp"
#include "order_book.hpp"
#include "flat_order_book.hpp"

#include <vector>

namespace exsim {

// Result of submitting a new order: any trades it generated, plus its
// final status (did it rest on the book, get fully filled, get rejected?).
struct SubmitResult {
    std::vector<Trade> trades;
    OrderStatus final_status;
    Quantity remaining_qty;   // whatever, if anything, is left unfilled
};

// Templated on the underlying book implementation (BookT) so the EXACT
// SAME matching logic can run against different OrderBook designs. This is
// what makes the Phase 3 benchmark a genuine apples-to-apples comparison:
// only the storage layer changes (std::map vs sorted vector), nothing
// about the matching algorithm itself does.
//
// BookT must provide: insert_order, remove_order, find_order, fill_order,
// has_bids/has_asks, best_bid_price/best_ask_price, best_bid_level/
// best_ask_level, order_count. OrderBook and FlatOrderBook both satisfy
// this (informally -- no C++20 concept is declared for it yet, a natural
// follow-up).
template <typename BookT>
class MatchingEngineImpl {
public:
    // Submit a new order. Handles Limit, Market, and IOC uniformly:
    // 1. Try to match against the opposite side of the book (aggressively,
    //    at increasingly worse prices, until the order is filled, the book
    //    runs out of liquidity, or -- for Limit orders -- the next price
    //    would violate the order's limit price).
    // 2. If anything remains AND the order type allows resting (Limit only),
    //    add the remainder to the book. Market and IOC orders never rest --
    //    unfilled quantity is simply cancelled.
    SubmitResult submit_order(Order order);

    // Cancel a resting order. Returns false if it doesn't exist (already
    // filled, already cancelled, or never existed).
    bool cancel_order(OrderId id);

    // Modify a resting order's price and/or quantity. Per standard exchange
    // behavior, a price OR quantity-increase change loses time priority --
    // it's implemented as cancel + re-submit with a fresh timestamp.
    // A quantity DECREASE is allowed in-place without losing priority.
    // Returns false if the order doesn't exist.
    bool modify_order(OrderId id, Price new_price, Quantity new_qty, Timestamp now);

    const BookT& book() const { return book_; }

private:
    BookT book_;

    // Returns true if `incoming` (at the given side/price/type) is still
    // allowed to match against `resting_price`. For a Buy Limit order, it
    // can match any ask <= its limit price. For Market orders, any price.
    static bool crosses(Side incoming_side, OrderType incoming_type,
                        Price incoming_limit_price, Price resting_price);
};

// Convenience aliases -- these are what the rest of the codebase (tests,
// demo, existing benchmark) uses, so nothing outside this file needed to
// change when the class became a template.
using MatchingEngine = MatchingEngineImpl<OrderBook>;
using FlatMatchingEngine = MatchingEngineImpl<FlatOrderBook>;

} // namespace exsim

// Template definitions live in the .cpp and are explicitly instantiated
// for OrderBook and FlatOrderBook at the bottom of that file. This keeps
// the header readable (declarations only) while still being a template --
// the trade-off is that MatchingEngineImpl can ONLY ever be instantiated
// for those two types, since nothing else gets an explicit instantiation.
// That's an intentional, documented limitation, not an oversight.
