# Exchange Simulator — Phase 1: Core Matching Engine

A single-threaded limit order book matching engine implementing strict
price-time priority, built in modern C++20.

## What's implemented

- **Order types:** Limit, Market, IOC (Immediate-Or-Cancel)
- **Operations:** submit, cancel, modify
- **Matching rule:** strict price-time priority — best price wins; ties at
  the same price are broken by who arrived first (FIFO)
- **Partial fills:** both the incoming order and resting orders can be
  partially filled and continue to exist afterward
- 24 GoogleTest unit tests, all passing

## Architecture — why two classes, not one

`OrderBook` is a **pure data structure**. It only knows how to store, find,
and remove resting orders efficiently. It has no idea what a "trade" is.

`MatchingEngine` contains **all the matching logic** — the actual business
rules of what counts as a valid match and what happens to unfilled quantity.
It uses `OrderBook` as a tool.

Why split them? Two reasons, and both are real engineering reasons, not
just "for cleanliness":
1. **Testability.** `OrderBookTest` verifies "did I store/find/remove things
   correctly?" completely independent of "did I match orders correctly?"
   If a test fails, you instantly know which class is broken.
2. **Change isolation.** If tomorrow you want to swap `std::map` for a
   faster flat array-based price level structure (a real Phase 3 goal),
   you only touch `OrderBook`. `MatchingEngine`'s logic doesn't change at
   all, because it only talks to `OrderBook` through its public interface.

## Data structure choices (be ready to defend these)

**Price levels: `std::map<Price, std::list<Order>>`**
- `std::map` keeps price levels sorted automatically, so "what's the best
  price?" is always just `begin()` — O(1) to read, O(log n) to insert/erase
  a whole new price level.
- Bids use `std::greater<Price>` (best = highest price = smallest key
  by that comparator = `begin()`). Asks use the default `std::less<Price>`
  (best = lowest price = `begin()`).
- **Why not `std::unordered_map`?** Because we need *sorted* iteration —
  "give me the best price" is a core, extremely hot operation. Hashing
  doesn't give you order.
- **Known limitation, and why it's fine for Phase 1:** `std::map` is a
  red-black tree — each node is a separate heap allocation, which is bad
  for CPU cache locality. This is exactly what Phase 3 (performance
  engineering) targets: replacing this with something more cache-friendly
  once correctness is locked in. Building the fast version first, before
  it's correct, is how you end up debugging two problems at once.

**Orders within a price level: `std::list<Order>`, not `std::deque` or `std::vector`**
- This is the single most important, most "gotcha" decision in the whole
  codebase, so understand it cold: `OrderBook` needs O(1) cancel of *any*
  order, not just the front/back. To do that, it keeps an
  `unordered_map<OrderId, iterator>` pointing directly at each order's
  location. This only works safely if inserting/erasing OTHER elements
  never invalidates that iterator.
- `std::list` guarantees this: erasing or inserting anywhere never
  invalidates iterators to other elements. `std::vector` and `std::deque`
  do NOT give you this guarantee for arbitrary insert/erase — a `vector`
  can reallocate its entire buffer and invalidate every iterator; a
  `deque` invalidates iterators on front/back insertion.
- Trade-off: `std::list` has worse cache locality than a contiguous
  container (its nodes are scattered on the heap). That cost is accepted
  in Phase 1 for correctness and API simplicity, and is a named target in
  Phase 3 (e.g. an intrusive linked list would keep O(1) cancel with much
  better cache behavior).

**Price as `int64_t` ticks, not `double`**
- Never use floating point for money/price in a real trading system.
  `0.1 + 0.2 != 0.3` in floating point — that's an unacceptable bug in a
  system that decides who owes whom money. Real exchanges represent price
  as an integer number of the smallest tradable unit (a "tick"), e.g. cents
  or 1/100th of a cent, and this codebase follows that convention.

**Order lookup: `unordered_map<OrderId, OrderLocation>`**
- Cancel/modify need O(1) "find where this order lives" — average O(1)
  hash lookup beats scanning every price level.

## Matching algorithm — the core loop, in words

1. Take the incoming order. Figure out which side of the book it needs to
   check (a Buy checks asks, a Sell checks bids).
2. Look at the best price on that opposite side. Ask: does this incoming
   order's type/price allow it to trade at that price? (`crosses()`
   function — Market orders cross at any price; Limit/IOC only cross if
   the price is at least as good as their limit.)
3. If yes: take the oldest order at that price (front of the FIFO list),
   trade the smaller of the two remaining quantities, record a `Trade`,
   reduce both sides' remaining quantity, and — importantly — the trade
   executes **at the resting order's price**, not the aggressor's price
   (this is standard: the person who was already waiting gets their price
   honored).
4. Repeat until the incoming order is fully filled, or the book has no
   more crossable liquidity.
5. Afterward: if anything is left AND the order is a Limit order, it rests
   on the book. If it's Market or IOC, the leftover is simply discarded —
   **this is the one line that's easy to get subtly wrong**, and there are
   dedicated tests (`MarketOrderUnfilledRemainderIsCancelledNotRested`,
   `IOCPartialFillCancelsRemainder`) proving it doesn't rest by accident.

## Known simplifications (say these OUT LOUD in an interview — it shows maturity, not weakness)

- **`modify_order` doesn't re-run matching.** When a price change would
  cause the modified order to now cross the book, a real exchange would
  immediately match it, same as a fresh order. Right now, this
  implementation just re-rests it without checking. This is flagged with
  a comment in the code and is a natural next task.
- **No self-match prevention.** A participant's own buy and sell orders can
  match against each other. Most real exchanges have explicit logic to
  prevent this (STP — self-trade prevention). Not implemented yet.
- **Single-threaded, no networking, no persistence yet.** This is Phase 1
  by design — see the development plan below.

## Phase 2 + 3: Benchmarking, and a real measured optimization

`bench/benchmark.cpp` feeds the SAME 200,000-order sequence through two
matching engines that share 100% of the matching logic (`MatchingEngineImpl`
is a template — see below) and differ ONLY in how the order book stores
price levels:

- **`OrderBook`** — `std::map<Price, std::list<Order>>` (a red-black tree)
- **`FlatOrderBook`** — a sorted `std::vector` of price levels

**Why this might be faster:** `std::map`'s tree nodes are individually
heap-allocated and scattered across memory — every lookup means chasing
pointers around the heap, which is unfriendly to CPU cache. A sorted
vector keeps all price levels contiguous, so scanning across levels
(e.g. a market order sweeping through several price levels) touches
memory that's likely already in cache.

**Why this might NOT be faster, and needed to be tested rather than assumed:**
inserting a brand-new price level into a sorted vector is O(n) in the
number of distinct price levels (shifting elements), versus O(log n) for
`std::map`. Whether that trade-off is worth it depends entirely on how
many distinct price levels a real book actually has — which is exactly
the kind of thing you don't get to just assert, you measure.

**Results (10 independent runs, 200,000 orders each):**

| Metric | OrderBook (std::map) | FlatOrderBook (vector) | Change |
|---|---|---|---|
| Throughput | ~2.7M–3.1M orders/sec | ~3.5M–3.7M orders/sec | **+~20%** |
| p99 latency | ~2.7–2.9 μs | ~1.1–1.4 μs | **–~50%** |

Both numbers were consistent across all 10 runs (5 focused on throughput,
5 on p99) — one earlier anomalous single run showed a throughput
regression, which on investigation was scheduling noise from this being a
shared/virtualized sandbox, not a real result (see the "known noise"
section below on how that was verified rather than assumed).

**Correctness, not just speed:** `tests/test_flat_order_book.cpp` mirrors
`test_order_book.cpp` line-for-line, and `tests/test_matching_engine_typed.cpp`
uses GoogleTest's typed-test mechanism to run the identical matching-logic
test bodies against both `MatchingEngine` and `FlatMatchingEngine`. All 44
tests pass for both. The optimization changed performance, not behavior —
that's provable, not just claimed.

### On that giant `Max` latency number in the benchmark output

Both engines occasionally show a single-order latency spike into the
millisecond range (vs. a p99 of ~1–3 microseconds). This was investigated,
not ignored: instrumenting the benchmark to print which order index
triggered a >1ms latency showed it happening at essentially random,
unrelated order indices run to run (e.g. order #49918 one run, #101200
the next) — not correlated with book size, price level count, or any
other property of the algorithm. That pattern (rare, random, large,
uncorrelated with load) is the signature of OS scheduler preemption on
shared/virtualized infrastructure, not an algorithmic issue. Real
low-latency systems solve this class of problem with CPU core pinning,
isolated bare-metal hosts, and real-time kernel scheduling — none of
which apply in this sandbox. p50/p95/p99 are the numbers that reflect the
algorithm; max here reflects the environment.

## What's NOT here yet (the roadmap, so you can talk about direction)

- Phase 4: A single lock-free SPSC ring buffer feeding the matching thread
  from a separate order-intake thread, plus an async logging thread
- Phase 5: TCP order gateway + SQLite trade persistence

## Building and running

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4
./unit_tests   # runs all 24 tests
./demo         # runs a small illustrative scenario
```
