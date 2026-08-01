#pragma once

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "log.hpp"

// Socket option helpers used by server and client.

// Decision: disable Nagle on connected data sockets so small TICK/HEARTBEAT
// messages are not delayed. Set after accept()/connect() — do not rely on the
// listen socket inheriting TCP_NODELAY to accepted fds.
inline bool set_tcp_nodelay(int fd) {
    int opt = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        LOG_WARN("setsockopt(TCP_NODELAY) failed");
        return false;
    }
    return true;
}

// Non-blocking mode for the server's poll()-driven I/O loop.
inline bool set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_WARN("fcntl(F_GETFL) failed");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_WARN("fcntl(O_NONBLOCK) failed");
        return false;
    }
    return true;
}
