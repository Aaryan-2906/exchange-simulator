#include "order_gateway.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace exsim {

namespace {
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
} // namespace

OrderGateway::OrderGateway(uint16_t port, GatewayEventQueue& to_matcher, ResponseEventQueue& from_matcher)
    : port_(port), to_matcher_(to_matcher), from_matcher_(from_matcher) {}

OrderGateway::~OrderGateway() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
    for (auto& pfd : fds_) {
        if (pfd.fd >= 0) close(pfd.fd);
    }
}

bool OrderGateway::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::perror("socket");
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return false;
    }

    if (listen(listen_fd_, /*backlog=*/64) < 0) {
        std::perror("listen");
        return false;
    }

    set_nonblocking(listen_fd_);

    fds_.clear();
    fds_.push_back(pollfd{listen_fd_, POLLIN, 0});

    running_.store(true, std::memory_order_release);
    return true;
}

void OrderGateway::stop() {
    running_.store(false, std::memory_order_release);
}

void OrderGateway::run() {
    while (running_.load(std::memory_order_acquire)) {
        // Short timeout (not infinite) so we still periodically drain
        // from_matcher_ and check running_ even with no socket activity.
        int ready = poll(fds_.data(), fds_.size(), /*timeout_ms=*/50);

        if (ready < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            break;
        }

        if (ready > 0) {
            // Listening socket: new connection(s) pending.
            if (fds_[0].revents & POLLIN) {
                accept_new_connection();
            }

            // Iterate backwards so remove_connection()'s swap-and-pop
            // doesn't skip an element or invalidate the index we're
            // currently examining.
            for (size_t i = fds_.size(); i-- > 1;) {
                if (fds_[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                    handle_client_readable(i);
                }
            }
        }

        drain_responses();
    }
}

void OrderGateway::accept_new_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_fd < 0) {
            break; // no more pending connections (EAGAIN/EWOULDBLOCK)
        }
        set_nonblocking(client_fd);
        fds_.push_back(pollfd{client_fd, POLLIN, 0});
        read_buffers_[client_fd] = "";
    }
}

void OrderGateway::handle_client_readable(size_t fds_index) {
    int fd = fds_[fds_index].fd;
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);

    if (n <= 0) {
        // 0 = orderly shutdown by peer, <0 with real error = connection gone.
        remove_connection(fds_index);
        return;
    }

    read_buffers_[fd].append(buf, static_cast<size_t>(n));

    // Extract every complete newline-terminated line currently buffered;
    // leave any trailing partial line for the next read.
    std::string& pending = read_buffers_[fd];
    size_t newline_pos;
    while ((newline_pos = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newline_pos);
        pending.erase(0, newline_pos + 1);

        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate CRLF line endings
        }
        if (line.empty()) continue;

        auto parsed = parse_gateway_line(line);
        if (!parsed) {
            ResponseEvent err{fd, "ERROR could not parse: " + line + "\n"};
            // Best-effort direct write for parse errors -- if the queue
            // to the matcher isn't even involved, there's no reason to
            // route this through the matching thread at all.
            send(fd, err.text.c_str(), err.text.size(), MSG_NOSIGNAL);
            continue;
        }

        GatewayEvent event{fd, *parsed};
        while (!to_matcher_.try_push(event)) {
            // Backpressure: matcher is falling behind. Spinning here
            // would block this gateway thread from servicing OTHER
            // clients, which is a real trade-off worth naming rather
            // than hiding -- see README's "known limitations" note for
            // Phase 5 on why this is acceptable for a portfolio project
            // but would need a smarter policy (drop, disconnect, or a
            // larger buffer) in production.
        }
    }
}

void OrderGateway::remove_connection(size_t fds_index) {
    int fd = fds_[fds_index].fd;
    close(fd);
    read_buffers_.erase(fd);
    // Swap-and-pop: O(1) removal, doesn't preserve order (order doesn't
    // matter for a flat set of poll'd sockets).
    fds_[fds_index] = fds_.back();
    fds_.pop_back();
}

void OrderGateway::drain_responses() {
    ResponseEvent resp;
    while (from_matcher_.try_pop(resp)) {
        // Best-effort: if the client already disconnected, send() will
        // fail harmlessly (MSG_NOSIGNAL avoids a SIGPIPE crash) and the
        // response is simply dropped -- there's no one left to read it.
        send(resp.client_fd, resp.text.c_str(), resp.text.size(), MSG_NOSIGNAL);
    }
}

} // namespace exsim
