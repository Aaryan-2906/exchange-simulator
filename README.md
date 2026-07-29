# ⚡ Exchange Simulator

A high-performance limit order book and matching engine built in **C++20**, designed to simulate the core order-matching infrastructure of a real exchange.

Supports Limit, Market, and IOC orders with strict **price-time priority** matching, partial fills, and O(1) cancel/modify — achieving **3.5M+ orders/sec** throughput and **~1 μs p99 latency** on the optimized engine, with a **lock-free concurrent pipeline** for multithreaded order intake.

---

## Features

- **Order Types** — Limit, Market, Immediate-Or-Cancel (IOC)
- **Operations** — Submit, Cancel, Modify
- **Price-Time Priority** — best price wins; ties broken by earliest arrival (FIFO)
- **Partial Fills** — both aggressor and resting orders can be partially filled
- **Integer Tick Pricing** — prices stored as `int64_t` ticks to avoid floating-point errors
- **O(1) Order Cancel** — hash-map backed direct lookup to any resting order
- **Lock-Free SPSC Ring Buffer** — wait-free producer/consumer pipeline with `std::atomic` acquire/release ordering
- **Cache-Line Padded Atomics** — `alignas(64)` on head/tail indices to eliminate false sharing
- **50 Unit Tests** — comprehensive GoogleTest suite covering both engine variants and concurrent correctness

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         System Pipeline                          │
│                                                                  │
│  [Producer Thread]          [SPSC Ring Buffer]    [Matcher Thread]│
│  ┌────────────────┐        ┌──────────────┐     ┌──────────────┐│
│  │ Order Intake   │─push──▶│  Lock-Free   │──pop▶│ Matching     ││
│  │ (network/gen)  │        │  Queue (4096)│     │ Engine       ││
│  └────────────────┘        └──────────────┘     └──────┬───────┘│
│                                                        │        │
│                                              ┌─────────▼───────┐│
│                                              │   OrderBook     ││
│                                              │ (swappable via  ││
│                                              │   template)     ││
│                                              └─────────────────┘│
└──────────────────────────────────────────────────────────────────┘
```

The matching engine is **single-threaded by design** — the order book is shared state, and concurrent matching would require locking on every operation, destroying latency and making results non-deterministic. Instead, concurrency is isolated to the hand-off boundary via a lock-free SPSC ring buffer.

| Component | Responsibility |
|---|---|
| **`MatchingEngine`** | All business logic — matching rules, trade generation, order lifecycle |
| **`OrderBook` / `FlatOrderBook`** | Pure data structure — stores, finds, and removes resting orders |
| **`SPSCRingBuffer`** | Lock-free, fixed-capacity circular queue for cross-thread order passing |

`MatchingEngine` is a class template (`MatchingEngineImpl<BookT>`), allowing the order book implementation to be swapped without changing any matching logic.

---

## Data Structures

| Structure | Implementation | Rationale |
|---|---|---|
| Price levels | `std::map<Price, std::list<Order>>` | Sorted iteration for O(1) best-price access |
| Order queue (per level) | `std::list<Order>` | Stable iterators — enables O(1) cancel without invalidation |
| Order lookup | `std::unordered_map<OrderId, iterator>` | O(1) average lookup for cancel/modify |
| Price representation | `int64_t` (tick) | Avoids floating-point precision errors |
| Inter-thread queue | `SPSCRingBuffer<T, N>` | Lock-free, cache-line padded, power-of-2 capacity for fast modular indexing |

**Optimized variant (`FlatOrderBook`):** Replaces `std::map` with a sorted `std::vector` for contiguous memory layout and improved cache performance.

---

## Performance Benchmarks

### Matching Engine (200K orders, 10 independent runs)

| Metric | `OrderBook` (std::map) | `FlatOrderBook` (std::vector) | Improvement |
|---|---|---|---|
| **Throughput** | ~2.7–3.1M orders/sec | ~3.5–3.7M orders/sec | **+20%** |
| **p99 Latency** | ~2.7–2.9 μs | ~1.1–1.4 μs | **−50%** |

### Concurrent Pipeline (500K orders)

The full producer → ring buffer → matcher pipeline produces **deterministic results** across repeated runs (identical trade count of 77,957 with same seed), confirming zero orders lost, duplicated, or reordered across thread boundaries.

---

## Concurrency Model

The `SPSCRingBuffer` uses `std::atomic` with explicit **acquire/release** memory ordering:

- **Producer:** writes order data into the slot, then publishes the new `head_` with `memory_order_release`
- **Consumer:** reads `head_` with `memory_order_acquire`, guaranteeing visibility of the producer's data writes
- **No mutex, no syscall** — both threads make forward progress without ever blocking

Key design decisions:
- **Power-of-2 capacity** — `index % capacity` replaced with `index & (capacity - 1)` for fast bitwise modular arithmetic
- **`alignas(64)` padding** — `head_` and `tail_` placed on separate cache lines to prevent false sharing between cores
- **N-1 usable slots** — one slot always kept empty to distinguish full from empty using only two indices

Correctness verified via:
1. Unit tests (push/pop, FIFO, full/empty, wraparound)
2. 200,000-item 2-thread stress test (`ConcurrentProducerConsumerPreservesAllValuesInOrder`)
3. ThreadSanitizer (`-fsanitize=thread`) — zero data race warnings

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
./demo              # single-threaded matching demo
./concurrent_demo   # Phase 4: threaded producer/matcher pipeline
./unit_tests        # run all 50 tests
./bench             # run performance benchmark
```

---

## Project Structure

```
exchange-simulator/
├── include/
│   ├── order.hpp                # Core types: Order, Trade, Side, OrderType
│   ├── order_book.hpp           # std::map-based order book
│   ├── flat_order_book.hpp      # Sorted vector-based order book
│   ├── matching_engine.hpp      # Templated matching engine
│   └── spsc_ring_buffer.hpp     # Lock-free SPSC ring buffer
├── src/
│   ├── order_book.cpp
│   ├── flat_order_book.cpp
│   ├── matching_engine.cpp
│   ├── main.cpp                 # Demo driver
│   └── concurrent_demo.cpp      # Threaded pipeline demo
├── tests/
│   ├── test_order_book.cpp
│   ├── test_flat_order_book.cpp
│   ├── test_matching_engine.cpp
│   ├── test_matching_engine_typed.cpp
│   └── test_spsc_ring_buffer.cpp  # Ring buffer unit + stress tests
├── bench/
│   └── benchmark.cpp            # Throughput & latency benchmarks
├── .github/workflows/ci.yml    # GitHub Actions CI
├── CMakeLists.txt
└── LICENSE
```

---

## Roadmap

- [x] **Phase 1** — Core matching engine with correctness tests
- [x] **Phase 2** — Benchmarking infrastructure
- [x] **Phase 3** — Cache-optimized `FlatOrderBook` with measured performance gains
- [x] **Phase 4** — Lock-free SPSC ring buffer + concurrent producer/matcher pipeline
- [ ] **Phase 5** — TCP order gateway + SQLite trade persistence
- [ ] Async logging thread (lock-free queue → dedicated logger)
- [ ] Self-trade prevention (STP)
- [ ] `modify_order` re-matching on price change

---

## License

MIT
