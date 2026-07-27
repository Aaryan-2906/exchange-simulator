#include "matching_engine.hpp"

#include <algorithm>

namespace exsim {

template <typename BookT>
bool MatchingEngineImpl<BookT>::crosses(Side incoming_side, OrderType incoming_type,
                              Price incoming_limit_price, Price resting_price) {
    // A Market order crosses at any price -- it takes whatever liquidity
    // is available, regardless of how bad the price is.
    if (incoming_type == OrderType::Market) {
        return true;
    }
    // Limit and IOC orders both respect a limit price; the only difference
    // between them is what happens to any UNFILLED remainder afterwards
    // (Limit rests it on the book, IOC cancels it) -- handled in submit_order.
    if (incoming_side == Side::Buy) {
        // Buyer will pay up to their limit price; any ask AT or BELOW that
        // price is acceptable.
        return resting_price <= incoming_limit_price;
    } else {
        // Seller will accept down to their limit price; any bid AT or ABOVE
        // that price is acceptable.
        return resting_price >= incoming_limit_price;
    }
}

template <typename BookT>
SubmitResult MatchingEngineImpl<BookT>::submit_order(Order order) {
    order.remaining_qty = order.quantity;
    order.status = OrderStatus::New;

    std::vector<Trade> trades;

    while (order.remaining_qty > 0) {
        if (order.side == Side::Buy) {
            if (!book_.has_asks()) break;
            Price best_ask = book_.best_ask_price();
            if (!crosses(order.side, order.type, order.price, best_ask)) break;

            auto& level = book_.best_ask_level();
            Order& resting = level.front();      // oldest order at this price = time priority
            Quantity trade_qty = std::min(order.remaining_qty, resting.remaining_qty);
            OrderId resting_id = resting.id;      // capture before fill_order may erase it

            trades.push_back(Trade{resting_id, order.id, best_ask, trade_qty, order.timestamp});
            order.remaining_qty -= trade_qty;
            book_.fill_order(resting_id, trade_qty);  // updates/removes the resting order
        } else {
            if (!book_.has_bids()) break;
            Price best_bid = book_.best_bid_price();
            if (!crosses(order.side, order.type, order.price, best_bid)) break;

            auto& level = book_.best_bid_level();
            Order& resting = level.front();
            Quantity trade_qty = std::min(order.remaining_qty, resting.remaining_qty);
            OrderId resting_id = resting.id;

            trades.push_back(Trade{resting_id, order.id, best_bid, trade_qty, order.timestamp});
            order.remaining_qty -= trade_qty;
            book_.fill_order(resting_id, trade_qty);
        }
    }

    if (order.remaining_qty == 0) {
        order.status = OrderStatus::Filled;
    } else if (order.type == OrderType::Limit) {
        // Limit orders rest whatever quantity remains unfilled.
        order.status = trades.empty() ? OrderStatus::New : OrderStatus::PartiallyFilled;
        book_.insert_order(order);
    } else {
        // Market and IOC orders NEVER rest -- any unfilled remainder is
        // simply cancelled, not added to the book.
        order.status = trades.empty() ? OrderStatus::Cancelled : OrderStatus::PartiallyFilled;
    }

    return SubmitResult{trades, order.status, order.remaining_qty};
}

template <typename BookT>
bool MatchingEngineImpl<BookT>::cancel_order(OrderId id) {
    return book_.remove_order(id);
}

template <typename BookT>
bool MatchingEngineImpl<BookT>::modify_order(OrderId id, Price new_price, Quantity new_qty, Timestamp now) {
    Order* existing = book_.find_order(id);
    if (!existing) {
        return false;
    }

    // Quantity DECREASE at the SAME price keeps time priority (standard
    // exchange rule -- you're taking liquidity away, not adding new risk
    // ahead of others, so there's no fairness reason to send you to the
    // back of the queue).
    if (new_price == existing->price && new_qty <= existing->remaining_qty) {
        existing->remaining_qty = new_qty;
        return true;
    }

    // Anything else (price change, or a quantity INCREASE) loses time
    // priority: standard exchange behavior treats this as cancel + new order.
    //
    // NOTE / KNOWN SIMPLIFICATION: this re-inserts the modified order
    // directly onto the book without re-running it through the matching
    // loop. In a real exchange, a modify that would now cross the book
    // (e.g. raising a buy limit above the current best ask) should
    // immediately match, exactly like a fresh submit_order would. Wiring
    // modify_order to go through submit_order's matching path is a
    // natural next improvement -- flagged here rather than silently
    // getting the edge case wrong.
    Order updated = *existing;
    book_.remove_order(id);
    updated.price = new_price;
    updated.quantity = new_qty;
    updated.remaining_qty = new_qty;
    updated.timestamp = now;
    updated.status = OrderStatus::New;
    book_.insert_order(updated);
    return true;
}

// Explicit instantiation: this is what actually generates the machine code
// for MatchingEngineImpl<OrderBook> and MatchingEngineImpl<FlatOrderBook>.
// Without these two lines, the template definitions above would compile
// to nothing -- a template only becomes real code when instantiated.
template class MatchingEngineImpl<OrderBook>;
template class MatchingEngineImpl<FlatOrderBook>;

} // namespace exsim
