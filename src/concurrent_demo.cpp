// Demonstrates the Phase 4 threading model:
//
//   [Producer thread]  --push-->  [SPSC Ring Buffer]  --pop-->  [Matching thread]
//   (simulates order intake,                                    (the ONLY thread
//    e.g. parsing network                                        that ever touches
//    input -- not shown here)                                    the order book)
//
// The matching engine itself stays completely single-threaded and lock-free
// internally (same MatchingEngine from Phase 1-3, unchanged). The ONLY
// concurrency-safe component in the whole system is the ring buffer sitting
// between the two threads. This is deliberate: the smaller the amount of
// code that has to reason about concurrency, the smaller the surface area
// for concurrency bugs.

#include "matching_engine.hpp"
#include "spsc_ring_buffer.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

using namespace exsim;
using Clock = std::chrono::steady_clock;

namespace {

Order make_random_order(OrderId id, std::mt19937& rng) {
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> offset_dist(0.0, 5.0);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);

    Price mid = 10000;
    Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
    int offset = static_cast<int>(offset_dist(rng));
    Price price = mid + (side == Side::Buy ? -std::abs(offset) : std::abs(offset));
    if (price < 1) price = 1;
    Quantity qty = qty_dist(rng);

    return Order{id, side, OrderType::Limit, price, qty, qty, static_cast<Timestamp>(id)};
}

} // namespace

int main(int argc, char** argv) {
    size_t num_orders = 500000;
    if (argc > 1) {
        num_orders = static_cast<size_t>(std::stoul(argv[1]));
    }

    SPSCRingBuffer<Order, 4096> queue;
    MatchingEngine engine;

    std::atomic<size_t> total_trades{0};
    std::atomic<bool> producer_done{false};

    auto start = Clock::now();

    // Producer thread: pure order generation + enqueue. In a real system
    // this thread would be doing network I/O / parsing -- kept simple
    // here since the POINT is the threading model, not order generation.
    std::thread producer([&]() {
        std::mt19937 rng(42);
        for (size_t i = 0; i < num_orders; ++i) {
            Order order = make_random_order(static_cast<OrderId>(i + 1), rng);
            while (!queue.try_push(order)) {
                std::this_thread::yield(); // ring buffer momentarily full
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Matching thread: the ONLY thread that ever calls submit_order(), so
    // the order book never needs any internal locking at all.
    std::thread matcher([&]() {
        Order order;
        size_t processed = 0;
        while (processed < num_orders) {
            if (queue.try_pop(order)) {
                SubmitResult result = engine.submit_order(order);
                total_trades.fetch_add(result.trades.size(), std::memory_order_relaxed);
                ++processed;
            } else {
                std::this_thread::yield(); // ring buffer momentarily empty
            }
        }
    });

    producer.join();
    matcher.join();

    auto end = Clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::printf("=== Concurrent Producer/Matcher Demo ===\n");
    std::printf("Orders processed:   %zu\n", num_orders);
    std::printf("Total trades:       %zu\n", total_trades.load());
    std::printf("Wall time:          %.4f s\n", seconds);
    std::printf("Effective throughput: %.0f orders/sec (across the full pipeline)\n",
                 num_orders / seconds);
    std::printf("Final book size:    %zu resting orders\n", engine.book().order_count());

    return 0;
}
