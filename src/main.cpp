#include "matching_engine.hpp"

#include <iostream>

using namespace exsim;

void print_result(const std::string& label, const SubmitResult& result) {
    std::cout << label << " -> status=" << static_cast<int>(result.final_status)
              << " remaining=" << result.remaining_qty
              << " trades=" << result.trades.size() << "\n";
    for (const auto& t : result.trades) {
        std::cout << "    TRADE: resting=" << t.resting_order_id
                  << " aggressor=" << t.aggressor_order_id
                  << " price=" << t.price
                  << " qty=" << t.quantity << "\n";
    }
}

int main() {
    MatchingEngine engine;

    // Resting sell limit orders on the book first.
    print_result("Sell 10 @ 101", engine.submit_order(
        Order{1, Side::Sell, OrderType::Limit, 101, 10, 10, 1}));
    print_result("Sell 5 @ 100", engine.submit_order(
        Order{2, Side::Sell, OrderType::Limit, 100, 5, 5, 2}));

    // Incoming buy limit order that crosses -- should match the cheaper
    // resting order first (price priority), even though it was placed second.
    print_result("Buy 8 @ 101", engine.submit_order(
        Order{3, Side::Buy, OrderType::Limit, 101, 8, 8, 3}));

    std::cout << "Book has " << engine.book().order_count() << " resting orders left\n";

    // A market order that sweeps whatever's left.
    print_result("Buy 20 @ MARKET", engine.submit_order(
        Order{4, Side::Buy, OrderType::Market, 0, 20, 20, 4}));

    std::cout << "Book has " << engine.book().order_count() << " resting orders left\n";

    return 0;
}
