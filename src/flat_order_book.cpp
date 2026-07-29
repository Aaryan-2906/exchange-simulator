#include "flat_order_book.hpp"

#include <stdexcept>

namespace exsim {

FlatOrderBook::PriceLevelEntry* FlatOrderBook::find_level(
    std::vector<PriceLevelEntry>& levels, Price price, bool descending) {
    auto cmp = descending
        ? [](const PriceLevelEntry& e, Price p) { return e.price > p; }
        : [](const PriceLevelEntry& e, Price p) { return e.price < p; };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, cmp);
    if (it != levels.end() && it->price == price) {
        return &(*it);
    }
    return nullptr;
}

FlatOrderBook::PriceLevelEntry& FlatOrderBook::get_or_create_level(
    std::vector<PriceLevelEntry>& levels, Price price, bool descending) {
    auto cmp = descending
        ? [](const PriceLevelEntry& e, Price p) { return e.price > p; }
        : [](const PriceLevelEntry& e, Price p) { return e.price < p; };

    auto it = std::lower_bound(levels.begin(), levels.end(), price, cmp);
    if (it != levels.end() && it->price == price) {
        return *it;
    }
    // Not found -- insert a new level at the correct sorted position.
    // NOTE: this shifts every element after `it`. Deliberate trade-off,
    // see header comment: cheap because distinct price levels are few.
    auto inserted = levels.insert(it, PriceLevelEntry{price, {}});
    return *inserted;
}

void FlatOrderBook::erase_level_if_empty(
    std::vector<PriceLevelEntry>& levels, Price price, bool descending) {
    PriceLevelEntry* level = find_level(levels, price, descending);
    if (level && level->orders.empty()) {
        // Recompute position for erase (pointer arithmetic into the vector).
        auto cmp = descending
            ? [](const PriceLevelEntry& e, Price p) { return e.price > p; }
            : [](const PriceLevelEntry& e, Price p) { return e.price < p; };
        auto it = std::lower_bound(levels.begin(), levels.end(), price, cmp);
        levels.erase(it);
    }
}

void FlatOrderBook::insert_order(const Order& order) {
    bool is_bid = (order.side == Side::Buy);
    auto& levels = is_bid ? bids_ : asks_;
    PriceLevelEntry& level = get_or_create_level(levels, order.price, is_bid);

    level.orders.push_back(order);
    auto it = std::prev(level.orders.end());
    order_index_[order.id] = {order.side, order.price, it};
}

bool FlatOrderBook::remove_order(OrderId id) {
    auto idx_it = order_index_.find(id);
    if (idx_it == order_index_.end()) {
        return false;
    }
    const OrderLocation& loc = idx_it->second;
    bool is_bid = (loc.side == Side::Buy);
    auto& levels = is_bid ? bids_ : asks_;

    // Copy price BEFORE erasing from order_index_ below -- `loc` is a
    // reference into that hashtable's node, so it would dangle after the
    // erase.
    Price price = loc.price;

    PriceLevelEntry* level = find_level(levels, price, is_bid);
    level->orders.erase(loc.it);

    order_index_.erase(idx_it);
    erase_level_if_empty(levels, price, is_bid);
    return true;
}

Order* FlatOrderBook::find_order(OrderId id) {
    auto idx_it = order_index_.find(id);
    if (idx_it == order_index_.end()) {
        return nullptr;
    }
    return &(*idx_it->second.it);
}

void FlatOrderBook::fill_order(OrderId id, Quantity filled_qty) {
    Order* order = find_order(id);
    if (!order) {
        throw std::runtime_error("fill_order: order id not found in book");
    }
    if (filled_qty > order->remaining_qty) {
        throw std::runtime_error("fill_order: filled_qty exceeds remaining_qty");
    }
    order->remaining_qty -= filled_qty;
    if (order->remaining_qty == 0) {
        order->status = OrderStatus::Filled;
        remove_order(id);
    } else {
        order->status = OrderStatus::PartiallyFilled;
    }
}

} // namespace exsim
