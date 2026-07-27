# Exchange Simulator

A high-performance limit order book and matching engine built in **C++20**, designed to simulate the core order-matching infrastructure of a real exchange.

Supports Limit, Market, and IOC orders with strict **price-time priority** matching, partial fills, and O(1) cancel/modify — achieving **3.5M+ orders/sec** throughput and **~1 μs p99 latency** on the optimized engine.

---

## Features

- **Order Types** — Limit, Market, Immediate-Or-Cancel (IOC)
- **Operations** — Submit, Cancel, Modify
- **Price-Time Priority** — best price wins; ties broken by earliest arrival (FIFO)
- **Partial Fills** — both aggressor and resting orders can be partially filled
- **Integer Tick Pricing** — prices stored as `int64_t` ticks to avoid floating-point errors
- **O(1) Order Cancel** — hash-map backed direct lookup to any resting order
- **44 Unit Tests** — comprehensive GoogleTest suite covering both engine variants

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│                MatchingEngine                    │
│  ┌───────────────────────────────────────────┐  │
│  │  Matching Logic (price-time priority)     │  │
│  │  • crosses() — can this order trade?      │  │
│  │  • match loop — sweep opposing side       │  │
│  │  • post-match — rest or discard remainder │  │
│  └──────────────────┬────────────────────────┘  │
│                     │ uses                       │
│  ┌──────────────────▼────────────────────────┐  │
│  │  OrderBook (swappable via template)       │  │
│  │  • OrderBook      — std::map based        │  │
│  │  • FlatOrderBook  — sorted std::vector    │  │
│  └───────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

The engine is split into two layers:

| Component | Responsibility |
|---|---|
| **`MatchingEngine`** | All business logic — matching rules, trade generation, order lifecycle |
| **`OrderBook`** | Pure data structure — stores, finds, and removes resting orders |

`MatchingEngine` is implemented as a class template (`MatchingEngineImpl<BookT>`), allowing the order book implementation to be swapped without changing any matching logic. This enabled a clean A/B performance comparison between two storage strategies.

---

## Data Structures

| Structure | Implementation | Rationale |
|---|---|---|
| Price levels | `std::map<Price, std::list<Order>>` | Sorted iteration for O(1) best-price access |
| Order queue (per level) | `std::list<Order>` | Stable iterators — enables O(1) cancel without invalidation |
| Order lookup | `std::unordered_map<OrderId, iterator>` | O(1) average lookup for cancel/modify |
| Price representation | `int64_t` (tick) | Avoids floating-point precision errors |

**Optimized variant (`FlatOrderBook`):** Replaces `std::map` with a sorted `std::vector` for contiguous memory layout and improved cache performance.

---

## Performance Benchmarks

Benchmarked with 200,000 orders per run across 10 independent runs:

| Metric | `OrderBook` (std::map) | `FlatOrderBook` (std::vector) | Improvement |
|---|---|---|---|
| **Throughput** | ~2.7–3.1M orders/sec | ~3.5–3.7M orders/sec | **+20%** |
| **p99 Latency** | ~2.7–2.9 μs | ~1.1–1.4 μs | **−50%** |

The flat vector variant benefits from contiguous memory access patterns during price-level traversal, trading O(n) insertion cost for significantly better cache utilization — a worthwhile tradeoff for the typical number of active price levels.

---

## Build & Run

**Prerequisites:** C++20 compiler (GCC 10+, Clang 12+), CMake 3.16+

```bash
# Clone
git clone https://github.com/Aaryan-2906/exchange-simulator.git
cd exchange-simulator

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Run
./demo          # interactive demo scenario
./unit_tests    # run all 44 tests
./bench         # run performance benchmark
```

---

## Project Structure

```
exchange-simulator/
├── include/
│   ├── order.hpp              # Core types: Order, Trade, Side, OrderType
│   ├── order_book.hpp         # std::map-based order book
│   ├── flat_order_book.hpp    # Sorted vector-based order book
│   └── matching_engine.hpp    # Templated matching engine
├── src/
│   ├── order_book.cpp
│   ├── flat_order_book.cpp
│   ├── matching_engine.cpp
│   └── main.cpp               # Demo driver
├── tests/
│   ├── test_order_book.cpp
│   ├── test_flat_order_book.cpp
│   ├── test_matching_engine.cpp
│   └── test_matching_engine_typed.cpp  # Typed tests for both engines
├── bench/
│   └── benchmark.cpp          # Throughput & latency benchmarks
└── CMakeLists.txt
```

---

## Roadmap

- [x] **Phase 1** — Core matching engine with correctness tests
- [x] **Phase 2** — Benchmarking infrastructure
- [x] **Phase 3** — Cache-optimized `FlatOrderBook` with measured performance gains
- [ ] **Phase 4** — Lock-free SPSC ring buffer for multithreaded order intake
- [ ] **Phase 5** — TCP order gateway + SQLite trade persistence
- [ ] Self-trade prevention (STP)
- [ ] `modify_order` re-matching on price change

---

## License

MIT
