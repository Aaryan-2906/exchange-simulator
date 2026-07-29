// This is deliberately an INTEGRATION test, not a unit test: it opens a
// real listening socket, spawns real threads, and drives the system with
// a real TCP client connection -- proving the gateway, ring buffers, and
// matching engine actually work together, not just in isolation.
#include <gtest/gtest.h>

#include "gateway_events.hpp"
#include "matching_engine.hpp"
#include "order_gateway.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace exsim;

namespace {

// A small standalone client for the test -- connects, sends a line,
// and reads back whatever accumulates within a short window. Real
// integration tests over a live threaded pipeline are inherently a bit
// timing-sensitive; the retry/sleep loop below is a deliberate, small
// amount of slack for that, not a sign of a flaky design.
class TestClient {
public:
    explicit TestClient(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        // Retry connect briefly -- the gateway thread's listen() might
        // not have completed the instant the test thread tries to connect.
        for (int attempt = 0; attempt < 50; ++attempt) {
            if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    ~TestClient() { if (fd_ >= 0) close(fd_); }

    void send_line(const std::string& line) {
        std::string full = line + "\n";
        send(fd_, full.c_str(), full.size(), 0);
    }

    // Reads whatever's available over a short window and returns it.
    std::string read_available(int timeout_ms = 500) {
        std::string result;
        char buf[4096];
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        timeval tv{0, 20000}; // 20ms per recv attempt
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (std::chrono::steady_clock::now() < deadline) {
            ssize_t n = recv(fd_, buf, sizeof(buf), 0);
            if (n > 0) {
                result.append(buf, static_cast<size_t>(n));
                // Give a little more time in case more is still arriving.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } else if (!result.empty()) {
                break; // got something and nothing more is coming right now
            }
        }
        return result;
    }

private:
    int fd_ = -1;
};

// Bundles the gateway + matcher thread together the same way
// exchange_server.cpp does, minus persistence (not what this test covers).
class TestServer {
public:
    explicit TestServer(uint16_t port)
        : gateway_(port, to_matcher_, from_matcher_) {}

    bool start() {
        if (!gateway_.start()) return false;
        gateway_thread_ = std::thread([this]() { gateway_.run(); });
        matcher_thread_ = std::thread([this]() { matcher_loop(); });
        return true;
    }

    ~TestServer() {
        gateway_.stop();
        shutdown_.store(true, std::memory_order_release);
        if (gateway_thread_.joinable()) gateway_thread_.join();
        if (matcher_thread_.joinable()) matcher_thread_.join();
    }

private:
    void matcher_loop() {
        GatewayEvent event;
        Timestamp ts = 1;
        while (!shutdown_.load(std::memory_order_acquire)) {
            if (!to_matcher_.try_pop(event)) {
                std::this_thread::yield();
                continue;
            }
            if (event.command.type == CommandType::New) {
                Order order = event.command.order;
                order.timestamp = ts++;
                SubmitResult result = engine_.submit_order(order);

                std::ostringstream ack;
                ack << "ACK " << order.id << " " << status_to_string(result.final_status)
                    << " " << result.remaining_qty << "\n";
                ResponseEvent ack_ev{event.client_fd, ack.str()};
                while (!from_matcher_.try_push(ack_ev)) {}

                for (const auto& trade : result.trades) {
                    std::ostringstream tr;
                    tr << "TRADE " << order.id << " " << trade.price << " " << trade.quantity << "\n";
                    ResponseEvent tr_ev{event.client_fd, tr.str()};
                    while (!from_matcher_.try_push(tr_ev)) {}
                }
            } else if (event.command.type == CommandType::Cancel) {
                bool ok = engine_.cancel_order(event.command.cancel_target);
                std::ostringstream ack;
                ack << "ACK " << event.command.cancel_target << " "
                    << (ok ? "CANCELLED" : "REJECTED") << " 0\n";
                ResponseEvent ack_ev{event.client_fd, ack.str()};
                while (!from_matcher_.try_push(ack_ev)) {}
            }
        }
    }

    GatewayEventQueue to_matcher_;
    ResponseEventQueue from_matcher_;
    OrderGateway gateway_;
    MatchingEngine engine_;
    std::atomic<bool> shutdown_{false};
    std::thread gateway_thread_;
    std::thread matcher_thread_;
};

} // namespace

TEST(GatewayIntegrationTest, RestingLimitOrderGetsAckedAsNew) {
    TestServer server(19191);
    ASSERT_TRUE(server.start());

    TestClient client(19191);
    client.send_line("NEW BUY LIMIT 10000 10 1");

    std::string response = client.read_available();
    EXPECT_NE(response.find("ACK 1 NEW 10"), std::string::npos) << "Got: " << response;
}

TEST(GatewayIntegrationTest, CrossingOrdersProduceTradeNotification) {
    TestServer server(19192);
    ASSERT_TRUE(server.start());

    TestClient buyer(19192);
    buyer.send_line("NEW BUY LIMIT 10000 10 1");
    buyer.read_available(200); // drain the resting ACK, not under test here

    TestClient seller(19192);
    seller.send_line("NEW SELL LIMIT 10000 10 2");
    std::string response = seller.read_available();

    EXPECT_NE(response.find("ACK 2 FILLED 0"), std::string::npos) << "Got: " << response;
    EXPECT_NE(response.find("TRADE 2 10000 10"), std::string::npos) << "Got: " << response;
}

TEST(GatewayIntegrationTest, CancelRemovesOrderAndIsAcknowledged) {
    TestServer server(19193);
    ASSERT_TRUE(server.start());

    TestClient client(19193);
    client.send_line("NEW BUY LIMIT 10000 10 1");
    client.read_available(200);

    client.send_line("CANCEL 1");
    std::string response = client.read_available();
    EXPECT_NE(response.find("ACK 1 CANCELLED 0"), std::string::npos) << "Got: " << response;
}

TEST(GatewayIntegrationTest, MultipleSimultaneousClientsAreHandledIndependently) {
    TestServer server(19194);
    ASSERT_TRUE(server.start());

    // Proves the poll()-based single-threaded gateway genuinely handles
    // multiple concurrent connections, not just one at a time. Each
    // client waits for its own ACK before the next one sends -- this is
    // necessary because with independent TCP connections, there is no
    // guaranteed ordering of which line the single gateway thread
    // processes first (an earlier version of this test assumed
    // send-order == processing-order across different sockets, which is
    // NOT something the system guarantees, and the test failed for that
    // reason -- not because the matching logic was wrong. This version
    // fixes the test rather than papering over it.)
    TestClient a(19194);
    a.send_line("NEW BUY LIMIT 9990 5 1");
    std::string resp_a = a.read_available(300);
    EXPECT_NE(resp_a.find("ACK 1 NEW 5"), std::string::npos) << "a got: " << resp_a;

    TestClient b(19194);
    b.send_line("NEW BUY LIMIT 9980 5 2");
    std::string resp_b = b.read_available(300);
    EXPECT_NE(resp_b.find("ACK 2 NEW 5"), std::string::npos) << "b got: " << resp_b;

    // Now that a (bid 9990) and b (bid 9980) are both confirmed resting,
    // c's incoming sell at 9990 must cross a specifically (best bid,
    // price priority) -- this ordering IS deterministic, because it's
    // enforced by the test waiting for each prior ACK first.
    TestClient c(19194);
    c.send_line("NEW SELL LIMIT 9990 5 3");
    std::string resp_c = c.read_available(300);
    EXPECT_NE(resp_c.find("ACK 3 FILLED 0"), std::string::npos) << "c got: " << resp_c;
    EXPECT_NE(resp_c.find("TRADE 3 9990 5"), std::string::npos) << "c got: " << resp_c;
}
