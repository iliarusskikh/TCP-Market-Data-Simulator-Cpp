#pragma once

#include <unistd.h>

// Move-only owner of a POSIX socket file descriptor.
//
// Decision: RAII for every listen / accept / connect fd — destructor closes exactly
// once; copy deleted to avoid double-close. Compose higher-level types (e.g.
// ClientSession) with Rule of Zero.

class SocketGuard {
public:
    SocketGuard() noexcept = default;
    explicit SocketGuard(int fd) noexcept : fd_(fd) {}

    ~SocketGuard() { reset(); }

    SocketGuard(const SocketGuard&)            = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    SocketGuard(SocketGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    SocketGuard& operator=(SocketGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    // Relinquish ownership without closing (rarely needed).
    int release() noexcept {
        const int t = fd_;
        fd_ = -1;
        return t;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_{-1};
};
