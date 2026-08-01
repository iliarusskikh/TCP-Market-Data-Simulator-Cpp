#pragma once

#include <cerrno>
#include <cstring>
#include <vector>

#include <sys/socket.h>

#include "protocol.hpp"
#include "socket_guard.hpp"

// One connected subscriber under the server poll loop.
//
// Decision: outbound queue + flush() that respects EAGAIN so a slow client does
// not stall the whole broadcast. Inbound bytes are drained/discarded — this is a
// one-way market-data feed (liveness is client-side via last_rx).

struct ClientSession {
    SocketGuard sock;
    std::vector<char> out;   // pending wire bytes
    size_t out_off = 0;      // how many of out[] already sent
    bool alive = true;

    explicit ClientSession(SocketGuard s) : sock(std::move(s)) {}

    int fd() const { return sock.get(); }
    bool has_pending() const { return out_off < out.size(); }

    void enqueue_tick(const TickMsg& host_msg) {
        const TickMsg wire = tick_to_wire(host_msg);
        const char* p = reinterpret_cast<const char*>(&wire);
        out.insert(out.end(), p, p + sizeof(wire));
    }

    void enqueue_heartbeat(const HeartbeatMsg& host_msg) {
        const HeartbeatMsg wire = heartbeat_to_wire(host_msg);
        const char* p = reinterpret_cast<const char*>(&wire);
        out.insert(out.end(), p, p + sizeof(wire));
    }

    // Returns false if the peer is dead / hard error (caller should drop session).
    bool flush() {
        while (out_off < out.size()) {
            const ssize_t n = ::send(
                sock.get(),
                out.data() + out_off,
                out.size() - out_off,
                0);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true; // try later
                alive = false;
                return false;
            }
            if (n == 0) {
                alive = false;
                return false;
            }
            out_off += static_cast<size_t>(n);
        }
        out.clear();
        out_off = 0;
        return true;
    }

    // Drain readable data or detect peer close. Returns false if disconnected.
    bool on_readable() {
        char buf[256];
        for (;;) {
            const ssize_t n = ::recv(sock.get(), buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                alive = false;
                return false;
            }
            if (n == 0) {
                alive = false;
                return false;
            }
            // One-way feed: discard any inbound payload; close is what matters.
        }
    }
};
