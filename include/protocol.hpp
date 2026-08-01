#pragma once

#include <cstdint>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>

// Fixed-size binary wire protocol: TICK + HEARTBEAT only (no ORDER/ACK).
//
// Decision: packed structs + explicit hton/ntoh (including custom 64-bit helpers)
// so layout and endianness are portable. TCP is a byte stream — always accumulate
// full messages (send_exact / recv_exact / recv_feed).
//
// Runtime paths:
//   server → ClientSession::enqueue_tick / enqueue_heartbeat then flush
//   client → recv_feed (discriminate by first msg_type byte, then rest of struct)
// Typed send_tick/recv_tick helpers remain for tests / simple blocking demos.

enum class MsgType : uint8_t {
    TICK      = 1,
    HEARTBEAT = 2
};

#pragma pack(push, 1)
struct TickMsg {
    uint8_t  msg_type;      // MsgType::TICK
    uint32_t symbol_id;
    int64_t  price;         // scaled integer (micros: 1e6 == 1.0)
    uint64_t timestamp_ns;
    uint32_t volume;
};

struct HeartbeatMsg {
    uint8_t  msg_type;      // MsgType::HEARTBEAT
    uint64_t timestamp_ns;
};
#pragma pack(pop)

static_assert(sizeof(TickMsg) == 25, "TickMsg must be packed to 25 bytes");
static_assert(sizeof(HeartbeatMsg) == 9, "HeartbeatMsg must be packed to 9 bytes");

// ─── 64-bit endian helpers (portable; avoids relying on htobe64) ───

inline uint64_t host_to_net64(uint64_t value) {
    const uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
    const uint32_t low  = htonl(static_cast<uint32_t>(value & 0xffffffffULL));
    return (static_cast<uint64_t>(low) << 32) | high;
}

inline uint64_t net_to_host64(uint64_t value) {
    return host_to_net64(value); // swap is involution on both LE and BE hosts
}

inline int64_t host_to_net_i64(int64_t value) {
    uint64_t u;
    std::memcpy(&u, &value, sizeof(u));
    u = host_to_net64(u);
    int64_t out;
    std::memcpy(&out, &u, sizeof(out));
    return out;
}

inline int64_t net_to_host_i64(int64_t value) {
    return host_to_net_i64(value);
}

// ─── Host-order ↔ wire-order ───

inline TickMsg tick_to_wire(TickMsg m) {
    m.msg_type     = static_cast<uint8_t>(MsgType::TICK);
    m.symbol_id    = htonl(m.symbol_id);
    m.price        = host_to_net_i64(m.price);
    m.timestamp_ns = host_to_net64(m.timestamp_ns);
    m.volume       = htonl(m.volume);
    return m;
}

inline TickMsg tick_from_wire(TickMsg m) {
    m.symbol_id    = ntohl(m.symbol_id);
    m.price        = net_to_host_i64(m.price);
    m.timestamp_ns = net_to_host64(m.timestamp_ns);
    m.volume       = ntohl(m.volume);
    return m;
}

inline HeartbeatMsg heartbeat_to_wire(HeartbeatMsg m) {
    m.msg_type     = static_cast<uint8_t>(MsgType::HEARTBEAT);
    m.timestamp_ns = host_to_net64(m.timestamp_ns);
    return m;
}

inline HeartbeatMsg heartbeat_from_wire(HeartbeatMsg m) {
    m.timestamp_ns = net_to_host64(m.timestamp_ns);
    return m;
}

// ─── Exact send / recv (TCP is a byte stream) ───

enum class IoStatus {
    Ok,
    Closed,   // peer closed (recv returned 0)
    Error
};

inline IoStatus send_exact(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < n) {
        const ssize_t r = ::send(fd, p + sent, n - sent, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return IoStatus::Error;
        }
        if (r == 0) return IoStatus::Error;
        sent += static_cast<size_t>(r);
    }
    return IoStatus::Ok;
}

inline IoStatus recv_exact(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, p + got, n - got, 0);
        if (r == 0) return IoStatus::Closed;
        if (r < 0) {
            if (errno == EINTR) continue;
            return IoStatus::Error;
        }
        got += static_cast<size_t>(r);
    }
    return IoStatus::Ok;
}

inline IoStatus send_tick(int fd, TickMsg host_msg) {
    const TickMsg wire = tick_to_wire(host_msg);
    return send_exact(fd, &wire, sizeof(wire));
}

inline IoStatus recv_tick(int fd, TickMsg& host_out) {
    TickMsg wire{};
    const IoStatus st = recv_exact(fd, &wire, sizeof(wire));
    if (st != IoStatus::Ok) return st;
    host_out = tick_from_wire(wire);
    return IoStatus::Ok;
}

inline IoStatus send_heartbeat(int fd, HeartbeatMsg host_msg) {
    const HeartbeatMsg wire = heartbeat_to_wire(host_msg);
    return send_exact(fd, &wire, sizeof(wire));
}

inline IoStatus recv_heartbeat(int fd, HeartbeatMsg& host_out) {
    HeartbeatMsg wire{};
    const IoStatus st = recv_exact(fd, &wire, sizeof(wire));
    if (st != IoStatus::Ok) return st;
    host_out = heartbeat_from_wire(wire);
    return IoStatus::Ok;
}

// Read one framed message: consume msg_type byte, then the remainder for that type.
// FeedEvent holds either a tick or a heartbeat for this call (not a history).
enum class FeedKind { Tick, Heartbeat };

struct FeedEvent {
    FeedKind kind;
    TickMsg tick{};
    HeartbeatMsg hb{};
};

inline IoStatus recv_feed(int fd, FeedEvent& ev) {
    uint8_t type = 0;
    const IoStatus st0 = recv_exact(fd, &type, 1);
    if (st0 != IoStatus::Ok) return st0;

    if (type == static_cast<uint8_t>(MsgType::TICK)) {
        TickMsg wire{};
        wire.msg_type = type;
        const IoStatus st = recv_exact(
            fd,
            reinterpret_cast<char*>(&wire) + 1,
            sizeof(wire) - 1);
        if (st != IoStatus::Ok) return st;
        ev.kind = FeedKind::Tick;
        ev.tick = tick_from_wire(wire);
        return IoStatus::Ok;
    }
    if (type == static_cast<uint8_t>(MsgType::HEARTBEAT)) {
        HeartbeatMsg wire{};
        wire.msg_type = type;
        const IoStatus st = recv_exact(
            fd,
            reinterpret_cast<char*>(&wire) + 1,
            sizeof(wire) - 1);
        if (st != IoStatus::Ok) return st;
        ev.kind = FeedKind::Heartbeat;
        ev.hb = heartbeat_from_wire(wire);
        return IoStatus::Ok;
    }
    return IoStatus::Error; // unknown type — framing is broken
}
