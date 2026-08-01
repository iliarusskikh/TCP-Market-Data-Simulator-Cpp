// Market-data feed server (single thread).
//
// Architecture: non-blocking listen + clients under poll(); dual steady_clock
// deadlines for tick_rate_hz and heartbeat_interval_ms; broadcast identical
// messages to every ClientSession. Generator runs even with zero clients.
// Seed 42 keeps the random walk reproducible across demos.
//
// If a deadline is missed, next_*_at is snapped forward (catch-up) so we do not
// burst a backlog of ticks/heartbeats after a stall.

#include <csignal>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "client_session.hpp"
#include "config.hpp"
#include "log.hpp"
#include "protocol.hpp"
#include "socket_guard.hpp"
#include "socket_opts.hpp"
#include "tick_generator.hpp"

volatile sig_atomic_t g_shutdown = 0;

void on_signal(int) { g_shutdown = 1; }

static uint64_t now_ns_steady() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

static void drop_dead(std::vector<ClientSession>& clients) {
    std::vector<ClientSession> kept;
    kept.reserve(clients.size());
    for (auto& c : clients) {
        if (c.alive) kept.push_back(std::move(c));
        else         LOG_INFO("Client disconnected");
    }
    clients.swap(kept);
}

static int ms_until(std::chrono::steady_clock::time_point deadline,
                    std::chrono::steady_clock::time_point now) {
    if (now >= deadline) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::max<long long>(1, ms));
}

int main(int argc, char** argv) {
    const std::string cfg_path = config_path_from_args(argc, argv, "../config/default.conf");
    AppConfig cfg;
    const std::string cfg_err = load_config(cfg_path, cfg);
    if (!cfg_err.empty()) {
        log_error() << "Config error: " << cfg_err;
        log_error() << "Usage: " << (argc > 0 ? argv[0] : "server") << " [config_path]";
        return 1;
    }

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    SocketGuard listen_sock(socket(AF_INET, SOCK_STREAM, 0));
    if (!listen_sock.valid()) {
        LOG_ERROR("Socket creation failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(listen_sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("setsockopt(SO_REUSEADDR) failed");
    }

    if (!set_nonblocking(listen_sock.get())) {
        LOG_ERROR("Failed to set listen socket non-blocking");
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(static_cast<uint16_t>(cfg.port));

    if (bind(listen_sock.get(), reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0) {
        LOG_ERROR("Bind failed (port in use? permissions?)");
        return 1;
    }

    if (listen(listen_sock.get(), 64) < 0) {
        LOG_ERROR("Listen failed");
        return 1;
    }

    log_info() << "Server listening on port " << cfg.port
               << " (poll, non-blocking; config: " << cfg_path << ")";
    log_info() << "symbols_count=" << cfg.symbol_ids.size()
               << " tick_rate_hz=" << cfg.tick_rate_hz
               << " heartbeat_interval_ms=" << cfg.heartbeat_interval_ms;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    std::vector<ClientSession> clients;
    TickGenerator generator(cfg, /*seed=*/42);
    size_t tick_index = 0;
    using clock = std::chrono::steady_clock;
    auto next_tick_at = clock::now();
    auto next_hb_at   = clock::now() + std::chrono::milliseconds(cfg.heartbeat_interval_ms);
    const auto tick_period = std::chrono::microseconds(
        1000000 / std::max(1, cfg.tick_rate_hz));
    const auto hb_period = std::chrono::milliseconds(cfg.heartbeat_interval_ms);

    while (!g_shutdown) {
        std::vector<pollfd> pfds;
        pfds.reserve(1 + clients.size());

        pollfd listen_pfd{};
        listen_pfd.fd = listen_sock.get();
        listen_pfd.events = POLLIN;
        pfds.push_back(listen_pfd);

        for (auto& c : clients) {
            pollfd p{};
            p.fd = c.fd();
            p.events = POLLIN;
            if (c.has_pending()) p.events = static_cast<short>(p.events | POLLOUT);
            pfds.push_back(p);
        }

        // Wake for I/O or whichever deadline is sooner (tick vs heartbeat).
        const auto now = clock::now();
        const int timeout_ms = std::min(ms_until(next_tick_at, now), ms_until(next_hb_at, now));

        const int ready = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll() failed");
            return 1;
        }

        if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            for (;;) {
                addrlen = sizeof(address);
                const int cfd = accept(
                    listen_sock.get(),
                    reinterpret_cast<struct sockaddr*>(&address),
                    &addrlen);
                if (cfd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
                    LOG_WARN("accept() failed");
                    break;
                }
                SocketGuard csock(cfd);
                set_tcp_nodelay(csock.get());
                set_nonblocking(csock.get());
                clients.emplace_back(std::move(csock));
                log_info() << "Client connected (clients=" << clients.size() << ")";
            }
        }

        for (size_t i = 0; i < clients.size(); ++i) {
            const short rev = pfds[i + 1].revents;
            if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
                clients[i].alive = false;
                continue;
            }
            if (rev & POLLIN) {
                if (!clients[i].on_readable()) continue;
            }
            if (rev & POLLOUT) {
                clients[i].flush();
            }
        }
        drop_dead(clients);

        if (clock::now() >= next_tick_at) {
            const TickMsg tick = generator.next();
            ++tick_index;
            for (auto& c : clients) {
                c.enqueue_tick(tick);
                if (!c.flush()) { /* marked dead */ }
            }
            drop_dead(clients);

            next_tick_at += tick_period;
            // Avoid bursting a backlog after a long stall.
            if (next_tick_at < clock::now()) {
                next_tick_at = clock::now() + tick_period;
            }

            if (tick_index == 1 || tick_index % static_cast<size_t>(cfg.tick_rate_hz) == 0) {
                log_info() << "Broadcast tick#" << tick_index
                           << " symbol_id=" << tick.symbol_id
                           << " price=" << tick.price
                           << " clients=" << clients.size();
            }
        }

        if (clock::now() >= next_hb_at) {
            HeartbeatMsg hb{};
            hb.msg_type     = static_cast<uint8_t>(MsgType::HEARTBEAT);
            hb.timestamp_ns = now_ns_steady();
            for (auto& c : clients) {
                c.enqueue_heartbeat(hb);
                if (!c.flush()) { /* marked dead */ }
            }
            drop_dead(clients);

            next_hb_at += hb_period;
            if (next_hb_at < clock::now()) {
                next_hb_at = clock::now() + hb_period;
            }
        }
    }

    LOG_INFO("Server shutting down.");
    return 0;
}
