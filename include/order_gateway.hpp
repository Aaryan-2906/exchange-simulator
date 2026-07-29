#pragma once

#include "gateway_events.hpp"

#include <atomic>
#include <cstdint>
#include <poll.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace exsim {

// OrderGateway: accepts multiple simultaneous TCP client connections and
// is the ONLY thread that ever touches sockets. It multiplexes all client
// connections with poll() rather than spawning one thread per connection
// -- this is a deliberate choice: one thread per connection would mean
// MULTIPLE threads all trying to push into to_matcher_, breaking the
// single-producer contract the SPSC ring buffer depends on. Staying
// single-threaded here, multiplexed with poll(), means the ring buffer
// design from Phase 4 doesn't need to change at all to support multiple
// clients -- it's still genuinely single-producer.
//
// (POSIX-only: uses <poll.h> / BSD sockets directly, no portability
// abstraction layer -- a deliberate scope decision for a Linux-targeted
// portfolio project, flagged here rather than silently assumed.)
class OrderGateway {
public:
    OrderGateway(uint16_t port, GatewayEventQueue& to_matcher, ResponseEventQueue& from_matcher);
    ~OrderGateway();

    // Binds and listens. Returns false on failure.
    bool start();

    // Runs the poll() loop until stop() is called (from another thread)
    // or a fatal socket error occurs. Also drains from_matcher_ and
    // writes responses back to the right client socket each iteration.
    void run();

    void stop();

    uint16_t port() const { return port_; }

private:
    uint16_t port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    GatewayEventQueue& to_matcher_;
    ResponseEventQueue& from_matcher_;

    // index 0 is always the listening socket; poll() wants a flat array
    // of {fd, events, revents} it can inspect directly.
    std::vector<pollfd> fds_;

    // Per-connection partial-line buffer -- TCP is a byte stream, not a
    // message stream, so a single recv() may deliver half a line, or
    // several lines at once. Each client's leftover partial data is kept
    // here between poll() iterations until a full newline-terminated
    // line is assembled.
    std::unordered_map<int, std::string> read_buffers_;

    void accept_new_connection();
    void handle_client_readable(size_t fds_index);
    void remove_connection(size_t fds_index);
    void drain_responses();
};

} // namespace exsim
