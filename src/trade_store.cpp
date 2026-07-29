#include "trade_store.hpp"

#include <cstdio>
#include <stdexcept>

namespace exsim {

TradeStore::TradeStore(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("TradeStore: failed to open db: " + err);
    }

    init_schema();

    static const char* kInsertSql =
        "INSERT INTO trades (resting_order_id, aggressor_order_id, price, quantity, timestamp) "
        "VALUES (?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("TradeStore: failed to prepare insert statement: " + err);
    }
}

TradeStore::~TradeStore() {
    if (insert_stmt_) {
        sqlite3_finalize(insert_stmt_);
    }
    if (db_) {
        sqlite3_close(db_);
    }
}

void TradeStore::init_schema() {
    static const char* kSchema =
        "CREATE TABLE IF NOT EXISTS trades ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  resting_order_id INTEGER NOT NULL,"
        "  aggressor_order_id INTEGER NOT NULL,"
        "  price INTEGER NOT NULL,"
        "  quantity INTEGER NOT NULL,"
        "  timestamp INTEGER NOT NULL"
        ");";

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown error";
        sqlite3_free(err_msg);
        throw std::runtime_error("TradeStore: failed to create schema: " + err);
    }
}

bool TradeStore::insert_trade(const Trade& trade) {
    // Reset the PREPARED statement (clears bindings and rewinds it) rather
    // than re-preparing from SQL text -- this is the whole point of
    // keeping insert_stmt_ around as a member.
    sqlite3_reset(insert_stmt_);
    sqlite3_clear_bindings(insert_stmt_);

    sqlite3_bind_int64(insert_stmt_, 1, static_cast<sqlite3_int64>(trade.resting_order_id));
    sqlite3_bind_int64(insert_stmt_, 2, static_cast<sqlite3_int64>(trade.aggressor_order_id));
    sqlite3_bind_int64(insert_stmt_, 3, static_cast<sqlite3_int64>(trade.price));
    sqlite3_bind_int64(insert_stmt_, 4, static_cast<sqlite3_int64>(trade.quantity));
    sqlite3_bind_int64(insert_stmt_, 5, static_cast<sqlite3_int64>(trade.timestamp));

    int rc = sqlite3_step(insert_stmt_);
    if (rc != SQLITE_DONE) {
        std::fprintf(stderr, "TradeStore: insert failed: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

std::vector<Trade> TradeStore::fetch_all_trades() {
    std::vector<Trade> trades;

    static const char* kSelectSql =
        "SELECT resting_order_id, aggressor_order_id, price, quantity, timestamp "
        "FROM trades ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, kSelectSql, -1, &stmt, nullptr) != SQLITE_OK) {
        return trades;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Trade t;
        t.resting_order_id = static_cast<OrderId>(sqlite3_column_int64(stmt, 0));
        t.aggressor_order_id = static_cast<OrderId>(sqlite3_column_int64(stmt, 1));
        t.price = static_cast<Price>(sqlite3_column_int64(stmt, 2));
        t.quantity = static_cast<Quantity>(sqlite3_column_int64(stmt, 3));
        t.timestamp = static_cast<Timestamp>(sqlite3_column_int64(stmt, 4));
        trades.push_back(t);
    }

    sqlite3_finalize(stmt);
    return trades;
}

size_t TradeStore::trade_count() {
    static const char* kCountSql = "SELECT COUNT(*) FROM trades;";
    sqlite3_stmt* stmt = nullptr;
    size_t count = 0;
    if (sqlite3_prepare_v2(db_, kCountSql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

} // namespace exsim
