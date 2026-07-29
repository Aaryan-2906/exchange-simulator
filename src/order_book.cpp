#include "order_book.hpp"

#include <stdexcept>

namespace exsim {

void OrderBook::insert_order(const Order& order) {
    if (order.side == Side::Buy) {
        auto& level = bids_[order.price];       // creates level if new
        level.push_back(order);                 // append = joins back of time-priority queue
        auto it = std::prev(level.end());
        order_index_[order.id] = {order.side, order.price, it};
    } else {
        auto& level = asks_[order.price];
        level.push_back(order);
        auto it = std::prev(level.end());
        order_index_[order.id] = {order.side, order.price, it};
    }
}

bool OrderBook::remove_order(OrderId id) {
    auto idx_it = order_index_.find(id);
    if (idx_it == order_index_.end()) {
        return false;
    }
    const OrderLocation& loc = idx_it->second;

    // Copy the fields we need BEFORE erasing from order_index_ below --
    // `loc` is a reference into that hashtable's node, so erasing the
    // node would leave `loc` dangling.
    Side side = loc.side;
    Price price = loc.price;

    if (side == Side::Buy) {
        auto level_it = bids_.find(price);
        level_it->second.erase(loc.it);
    } else {
        auto level_it = asks_.find(price);
        level_it->second.erase(loc.it);
    }

    order_index_.erase(idx_it);
    erase_level_if_empty(side, price);
    return true;
}

Order* OrderBook::find_order(OrderId id) {
    auto idx_it = order_index_.find(id);
    if (idx_it == order_index_.end()) {
        return nullptr;
    }
    return &(*idx_it->second.it);
}

void OrderBook::fill_order(OrderId id, Quantity filled_qty) {
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

void OrderBook::erase_level_if_empty(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.empty()) {
            bids_.erase(it);
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.empty()) {
            asks_.erase(it);
        }
    }
}

} // namespace exsim
