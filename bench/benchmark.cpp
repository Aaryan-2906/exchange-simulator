// Benchmark harness for MatchingEngine::submit_order.
//
// Measures two things that matter for a matching engine:
//   1. THROUGHPUT: total orders processed per second
//   2. LATENCY DISTRIBUTION: how long a single submit_order() call takes,
//      as a distribution (mean is misleading for latency -- a few slow
//      calls can hide behind a low average; p99/max are what actually
//      matter for a system where consistency matters as much as speed)
//
// Deliberately hand-rolled with std::chrono rather than a benchmarking
// library for Phase 2 -- understanding exactly what's being measured and
// why matters more here than a polished library. Google Benchmark is a
// reasonable stretch addition later.

#include "matching_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace exsim;
using Clock = std::chrono::steady_clock;

namespace {

struct LatencyStats {
    double mean_ns;
    double p50_ns;
    double p95_ns;
    double p99_ns;
    double max_ns;
};

LatencyStats compute_stats(std::vector<double>& latencies_ns) {
    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t n = latencies_ns.size();

    double sum = 0;
    for (double v : latencies_ns) sum += v;

    auto percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * (n - 1));
        return latencies_ns[idx];
    };

    return LatencyStats{
        sum / n,
        percentile(0.50),
        percentile(0.95),
        percentile(0.99),
        latencies_ns.back()
    };
}

// Generates a mix of orders around a moving mid-price so the book has
// realistic depth and matching actually happens (not just orders that
// always rest, and not just orders that always immediately fill).
std::vector<Order> generate_orders(size_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::normal_distribution<double> price_offset_dist(0.0, 5.0);
    std::uniform_int_distribution<Quantity> qty_dist(1, 100);
    std::uniform_int_distribution<int> type_dist(0, 9); // 10% market orders

    std::vector<Order> orders;
    orders.reserve(count);

    Price mid_price = 10000; // in ticks, e.g. $100.00 if tick = $0.01

    for (size_t i = 0; i < count; ++i) {
        Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
        OrderType type = (type_dist(rng) == 0) ? OrderType::Market : OrderType::Limit;

        Price price = 0;
        if (type == OrderType::Limit) {
            int offset = static_cast<int>(price_offset_dist(rng));
            price = mid_price + (side == Side::Buy ? -std::abs(offset) : std::abs(offset));
            if (price < 1) price = 1;
        }

        Quantity qty = qty_dist(rng);
        orders.push_back(Order{
            static_cast<OrderId>(i + 1),
            side,
            type,
            price,
            qty,
            qty,
            static_cast<Timestamp>(i)
        });
    }
    return orders;
}

} // namespace

template <typename EngineT>
void run_benchmark(const char* label, const std::vector<Order>& orders) {
    EngineT engine;
    std::vector<double> latencies_ns;
    latencies_ns.reserve(orders.size());

    auto wall_start = Clock::now();

    for (const auto& order : orders) {
        auto t0 = Clock::now();
        engine.submit_order(order);
        auto t1 = Clock::now();
        latencies_ns.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    auto wall_end = Clock::now();
    double wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();

    LatencyStats stats = compute_stats(latencies_ns);
    double orders_per_sec = orders.size() / wall_seconds;

    std::printf("=== %s ===\n", label);
    std::printf("Orders submitted:   %zu\n", orders.size());
    std::printf("Wall time:          %.4f s\n", wall_seconds);
    std::printf("Throughput:         %.0f orders/sec\n", orders_per_sec);
    std::printf("Final book size:    %zu resting orders\n", engine.book().order_count());
    std::printf("--- Per-order latency (submit_order) ---\n");
    std::printf("Mean:   %8.1f ns\n", stats.mean_ns);
    std::printf("p50:    %8.1f ns\n", stats.p50_ns);
    std::printf("p95:    %8.1f ns\n", stats.p95_ns);
    std::printf("p99:    %8.1f ns\n", stats.p99_ns);
    std::printf("Max:    %8.1f ns  (note: on shared/virtualized infra, max is\n", stats.max_ns);
    std::printf("                    often OS scheduling noise, not algorithmic --\n");
    std::printf("                    see README for how this was verified)\n\n");
}

int main(int argc, char** argv) {
    size_t num_orders = 200000;
    if (argc > 1) {
        num_orders = static_cast<size_t>(std::stoul(argv[1]));
    }

    // SAME order sequence fed to both engines -- this is what makes the
    // comparison fair. Any difference in the numbers below is attributable
    // ONLY to the book implementation (std::map vs sorted vector), not to
    // different input data.
    auto orders = generate_orders(num_orders, /*seed=*/42);

    run_benchmark<MatchingEngine>("OrderBook (std::map) -- BASELINE", orders);
    run_benchmark<FlatMatchingEngine>("FlatOrderBook (sorted vector) -- OPTIMIZED", orders);

    return 0;
}
