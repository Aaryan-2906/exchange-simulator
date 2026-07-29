#pragma once

#include "order.hpp"

#include <sqlite3.h>
#include <string>
#include <vector>

namespace exsim {

// TradeStore: persists executed trades to a SQLite database.
//
// DESIGN NOTE: the insert statement is PREPARED ONCE in the constructor
// and reused for every trade, rather than building/parsing a new SQL
// string per insert. SQLite has to parse and plan a SQL statement before
// executing it -- for a single insert that's usually fine, but on a
// persistence thread that could be writing thousands of trades per
// second, re-parsing identical SQL every time is wasted work. This is
// the same "why cache expensive setup outside the hot path" principle
// as the memory-pool discussion in earlier phases, applied to SQL.
//
// THREADING NOTE: this class is used from exactly ONE thread (the
// persistence thread) in this project's architecture -- see
// exchange_server.cpp. A single sqlite3 connection is not safe to use
// from multiple threads concurrently without SQLite's serialized mode
// (which has its own locking overhead); keeping persistence
// single-threaded sidesteps that entirely, the same way keeping matching
// single-threaded sidesteps order-book locking.
class TradeStore {
public:
    explicit TradeStore(const std::string& db_path);
    ~TradeStore();

    TradeStore(const TradeStore&) = delete;
    TradeStore& operator=(const TradeStore&) = delete;

    // Persist a single trade. Returns false on failure (logged to stderr).
    bool insert_trade(const Trade& trade);

    // For tests / inspection: read back everything persisted so far,
    // ordered by insertion (rowid).
    std::vector<Trade> fetch_all_trades();

    size_t trade_count();

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_stmt_ = nullptr;

    void init_schema();
};

} // namespace exsim
