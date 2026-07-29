// exchange_server: the Phase 5 integration point.
//
//   [Gateway thread]  --GatewayEvent-->  [Matching thread]  --Trade-->  [Persistence thread]
//   (poll() over N          <--ResponseEvent--       |
//    TCP clients)                                (writes to trades.db via TradeStore)
//
// Three threads, three single responsibilities, matching the original
// architecture spec's separation-of-concerns philosophy:
//   - Gateway thread:      ONLY does socket I/O and line parsing
//   - Matching thread:     ONLY calls into MatchingEngine -- exactly the
//                          same single-threaded, lock-free-internally
//                          engine from Phase 1-4, completely unmodified
//   - Persistence thread:  ONLY writes to SQLite
//
// Every arrow above is one of the SPSC ring buffers from Phase 4 -- each
// one has exactly one producer thread and one consumer thread, which is
// what makes them safe to implement lock-free.

#include "gateway_events.hpp"
#include "matching_engine.hpp"
#include "order_gateway.hpp"
#include "trade_store.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <sstream>
#include <thread>

using namespace exsim;

namespace {
OrderGateway* g_gateway = nullptr;

void handle_sigint(int) {
    if (g_gateway) g_gateway->stop();
}
} // namespace

int main(int argc, char** argv) {
    uint16_t port = 9090;
    std::string db_path = "trades.db";
    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc > 2) db_path = argv[2];

    GatewayEventQueue to_matcher;
    ResponseEventQueue from_matcher;
    TradeEventQueue to_persistence;

    OrderGateway gateway(port, to_matcher, from_matcher);
    if (!gateway.start()) {
        std::fprintf(stderr, "Failed to start gateway on port %d\n", port);
        return 1;
    }
    g_gateway = &gateway;
    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    std::atomic<bool> shutdown{false};
    MatchingEngine engine;

    std::thread matcher_thread([&]() {
        GatewayEvent event;
        Timestamp ts_counter = 1;

        auto process = [&](const GatewayEvent& ev) {
            if (ev.command.type == CommandType::New) {
                Order order = ev.command.order;
                order.timestamp = ts_counter++;
                SubmitResult result = engine.submit_order(order);

                std::ostringstream ack;
                ack << "ACK " << order.id << " " << status_to_string(result.final_status)
                    << " " << result.remaining_qty << "\n";
                ResponseEvent ack_ev{ev.client_fd, ack.str()};
                while (!from_matcher.try_push(ack_ev)) { /* backpressure */ }

                for (const auto& trade : result.trades) {
                    std::ostringstream tr;
                    tr << "TRADE " << order.id << " " << trade.price << " " << trade.quantity << "\n";
                    ResponseEvent tr_ev{ev.client_fd, tr.str()};
                    while (!from_matcher.try_push(tr_ev)) { /* backpressure */ }
                    while (!to_persistence.try_push(trade)) { /* backpressure */ }
                }
            } else if (ev.command.type == CommandType::Cancel) {
                bool ok = engine.cancel_order(ev.command.cancel_target);
                std::ostringstream ack;
                ack << "ACK " << ev.command.cancel_target << " "
                    << (ok ? "CANCELLED" : "REJECTED") << " 0\n";
                ResponseEvent ack_ev{ev.client_fd, ack.str()};
                while (!from_matcher.try_push(ack_ev)) { /* backpressure */ }
            }
        };

        while (!shutdown.load(std::memory_order_acquire)) {
            if (to_matcher.try_pop(event)) {
                process(event);
            } else {
                std::this_thread::yield();
            }
        }
        // Drain whatever's left so a clean shutdown doesn't silently
        // discard in-flight orders.
        while (to_matcher.try_pop(event)) {
            process(event);
        }
    });

    std::thread persistence_thread([&]() {
        TradeStore store(db_path);
        Trade trade;
        while (!shutdown.load(std::memory_order_acquire)) {
            if (to_persistence.try_pop(trade)) {
                store.insert_trade(trade);
            } else {
                std::this_thread::yield();
            }
        }
        while (to_persistence.try_pop(trade)) {
            store.insert_trade(trade);
        }
    });

    std::printf("Exchange server listening on port %d, persisting trades to %s\n",
                port, db_path.c_str());
    std::printf("Protocol: NEW <BUY|SELL> <LIMIT|MARKET|IOC> <price> <qty> <client_order_id>\n");
    std::printf("          CANCEL <client_order_id>\n");
    std::printf("Try: printf 'NEW BUY LIMIT 10000 10 1\\n' | nc localhost %d\n", port);

    gateway.run(); // blocks until SIGINT/SIGTERM

    shutdown.store(true, std::memory_order_release);
    matcher_thread.join();
    persistence_thread.join();

    std::printf("Shutdown complete.\n");
    return 0;
}
