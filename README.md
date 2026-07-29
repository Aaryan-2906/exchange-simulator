# ⚡ Exchange Simulator

A high-performance limit order book and matching engine built in **C++20**, designed to simulate the core infrastructure of a real exchange — from order matching to network I/O to trade persistence.

Supports Limit, Market, and IOC orders with strict **price-time priority** matching, a **lock-free concurrent pipeline**, a **TCP order gateway** with multiplexed client connections, and **SQLite trade persistence** — achieving **3.5M+ orders/sec** throughput and **~1 μs p99 latency** on the optimized engine.

---

## Features

- **Order Types** — Limit, Market, Immediate-Or-Cancel (IOC)
- **Operations** — Submit, Cancel, Modify
- **Price-Time Priority** — best price wins; ties broken by earliest arrival (FIFO)
- **Partial Fills** — both aggressor and resting orders can be partially filled
- **Integer Tick Pricing** — prices stored as `int64_t` ticks to avoid floating-point errors
- **O(1) Order Cancel** — hash-map backed direct lookup to any resting order
- **Lock-Free SPSC Ring Buffers** — wait-free inter-thread communication with acquire/release atomics
- **TCP Order Gateway** — `poll()`-based multiplexed I/O handling multiple simultaneous clients
- **Text Protocol** — human-readable, `telnet`/`nc` testable wire format
- **SQLite Trade Persistence** — prepared-statement based trade storage on a dedicated thread
- **60+ Unit & Integration Tests** — GoogleTest suite covering matching, concurrency, protocol parsing, persistence, and end-to-end TCP flows

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                            Exchange Server                                   │
│                                                                              │
│  [Gateway Thread]        [Matching Thread]         [Persistence Thread]       │
│  ┌──────────────┐       ┌──────────────────┐      ┌───────────────────┐     │
│  │ TCP Server   │─push─▶│ MatchingEngine   │─push▶│ TradeStore        │     │
│  │ poll() over  │       │ (single-threaded, │      │ (SQLite, prepared │     │
│  │ N clients    │◀─push─│  unchanged from   │      │  statements)      │     │
│  └──────────────┘       │  Phase 1-4)       │      └───────────────────┘     │
│        ▲                └──────────────────┘                                 │
│        │                                                                     │
│   TCP clients                                                                │
│   (telnet/nc)            All arrows = SPSC Ring Buffers (lock-free)          │
└──────────────────────────────────────────────────────────────────────────────┘
```

**Three threads, three responsibilities:**

| Thread | Responsibility | Touches |
|---|---|---|
| **Gateway** | Socket I/O, line parsing, multiplexing via `poll()` | TCP sockets only |
| **Matching** | Order matching — same single-threaded engine from Phase 1 | OrderBook only |
| **Persistence** | Trade storage to SQLite | Database only |

Every inter-thread boundary is a **SPSC ring buffer** from Phase 4 — each has exactly one producer and one consumer, enabling lock-free operation. The matching engine itself is **completely unchanged** from Phase 1-4.

---

## Protocol

A simple text-based, newline-delimited protocol testable with `telnet` or `nc`:

```
# Commands (client → server)
NEW <BUY|SELL> <LIMIT|MARKET|IOC> <price> <qty> <client_order_id>
CANCEL <client_order_id>

# Responses (server → client)
ACK <client_order_id> <status> <remaining_qty>
TRADE <client_order_id> <price> <qty>
ERROR <reason>
```

**Example session:**
```bash
$ printf 'NEW BUY LIMIT 10000 10 1\n' | nc localhost 9090
ACK 1 NEW 10

$ printf 'NEW SELL LIMIT 10000 10 2\n' | nc localhost 9090
ACK 2 FILLED 0
TRADE 2 10000 10
```

---

## Data Structures

| Structure | Implementation | Rationale |
|---|---|---|
| Price levels | `std::map<Price, std::list<Order>>` | Sorted iteration for O(1) best-price access |
| Order queue (per level) | `std::list<Order>` | Stable iterators — enables O(1) cancel without invalidation |
| Order lookup | `std::unordered_map<OrderId, iterator>` | O(1) average lookup for cancel/modify |
| Price representation | `int64_t` (tick) | Avoids floating-point precision errors |
| Inter-thread queues | `SPSCRingBuffer<T, N>` | Lock-free, cache-line padded, power-of-2 capacity |
| Trade persistence | SQLite with prepared statements | Amortized SQL parsing cost, single-writer thread |

**Optimized variant (`FlatOrderBook`):** Replaces `std::map` with a sorted `std::vector` for contiguous memory layout and improved cache performance.

---

## Performance Benchmarks

### Matching Engine (200K orders, 10 independent runs)

| Metric | `OrderBook` (std::map) | `FlatOrderBook` (std::vector) | Improvement |
|---|---|---|---|
| **Throughput** | ~2.7–3.1M orders/sec | ~3.5–3.7M orders/sec | **+20%** |
| **p99 Latency** | ~2.7–2.9 μs | ~1.1–1.4 μs | **−50%** |

### Concurrent Pipeline (500K orders)

Deterministic results across repeated runs (identical trade count of 77,957 with same seed), confirming zero orders lost, duplicated, or reordered across thread boundaries.

---

## Build & Run

**Prerequisites:** C++20 compiler (GCC 10+, Clang 12+), CMake 3.16+, SQLite3

```bash
# Clone
git clone https://github.com/Aaryan-2906/exchange-simulator.git
cd exchange-simulator

# Install SQLite (Ubuntu/Debian)
sudo apt-get install -y libsqlite3-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Run
./demo                          # single-threaded matching demo
./concurrent_demo               # Phase 4: threaded producer/matcher pipeline
./exchange_server [port] [db]   # Phase 5: full TCP server (default: port 9090, trades.db)
./unit_tests                    # run all tests
./bench                         # run performance benchmark
```

**Connecting to the server:**
```bash
# Terminal 1: start the server
./exchange_server 9090 trades.db

# Terminal 2: send orders via netcat
printf 'NEW BUY LIMIT 10000 10 1\n' | nc localhost 9090
printf 'NEW SELL LIMIT 10000 10 2\n' | nc localhost 9090

# Inspect persisted trades
sqlite3 trades.db "SELECT * FROM trades;"
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
│   ├── spsc_ring_buffer.hpp     # Lock-free SPSC ring buffer
│   ├── gateway_protocol.hpp     # Text protocol parser + serializer
│   ├── gateway_events.hpp       # Inter-thread event types + queue typedefs
│   ├── order_gateway.hpp        # TCP gateway (poll-based multiplexing)
│   └── trade_store.hpp          # SQLite trade persistence
├── src/
│   ├── order_book.cpp
│   ├── flat_order_book.cpp
│   ├── matching_engine.cpp
│   ├── order_gateway.cpp        # TCP socket handling implementation
│   ├── trade_store.cpp          # SQLite CRUD implementation
│   ├── main.cpp                 # Demo driver
│   ├── concurrent_demo.cpp      # Threaded pipeline demo
│   └── exchange_server.cpp      # Full 3-thread exchange server
├── tests/
│   ├── test_order_book.cpp
│   ├── test_flat_order_book.cpp
│   ├── test_matching_engine.cpp
│   ├── test_matching_engine_typed.cpp
│   ├── test_spsc_ring_buffer.cpp
│   ├── test_gateway_protocol.cpp   # Protocol parsing tests
│   ├── test_trade_store.cpp        # SQLite persistence tests
│   └── test_gateway_integration.cpp # End-to-end TCP integration tests
├── bench/
│   └── benchmark.cpp
├── .github/workflows/ci.yml
├── CMakeLists.txt
└── LICENSE
```

---

## Roadmap

- [x] **Phase 1** — Core matching engine with correctness tests
- [x] **Phase 2** — Benchmarking infrastructure
- [x] **Phase 3** — Cache-optimized `FlatOrderBook` with measured performance gains
- [x] **Phase 4** — Lock-free SPSC ring buffer + concurrent producer/matcher pipeline
- [x] **Phase 5** — TCP order gateway + SQLite trade persistence
- [ ] Async logging thread (lock-free queue → dedicated logger)
- [ ] Self-trade prevention (STP)
- [ ] `modify_order` re-matching on price change
- [ ] Binary wire protocol (FIX/FAST style)

---

## License

MIT
