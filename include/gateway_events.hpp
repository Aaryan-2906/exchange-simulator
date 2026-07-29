#pragma once

#include "gateway_protocol.hpp"
#include "spsc_ring_buffer.hpp"

#include <string>

namespace exsim {

// Pushed by: Gateway thread (the ONE thread doing socket I/O, even though
//            it multiplexes many client connections via poll()).
// Popped by: Matching thread.
// This stays a valid SPSC queue because only ONE thread ever calls
// try_push on it, regardless of how many TCP clients are connected --
// multiplexing many sockets inside one thread is exactly what preserves
// that single-producer guarantee.
struct GatewayEvent {
    int client_fd = -1;
    GatewayCommand command;
};

// Pushed by: Matching thread.
// Popped by: Gateway thread (writes the text back to the right socket).
struct ResponseEvent {
    int client_fd = -1;
    std::string text; // one or more newline-terminated lines
};

// Fixed capacities chosen generously relative to expected burst sizes;
// see README for the same "why a fixed capacity, and what happens if
// it's exceeded" discussion as Phase 4.
using GatewayEventQueue = SPSCRingBuffer<GatewayEvent, 4096>;
using ResponseEventQueue = SPSCRingBuffer<ResponseEvent, 4096>;
using TradeEventQueue = SPSCRingBuffer<Trade, 4096>;

} // namespace exsim
