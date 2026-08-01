// Market-data feed client.
//
// Usage: ./client [config_path] [max_ticks]
//   max_ticks omitted or 0 → run until Ctrl+C / give-up on reconnect
//
// Liveness: any successfully parsed message (TICK or HEARTBEAT) refreshes
// last_rx. Silence >= heartbeat_timeout_ms → treat as dead and reconnect with
// exponential backoff. Inter-arrival samples reset when a new connection opens.

#include <csignal>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.hpp"
#include "latency.hpp"
#include "log.hpp"
#include "protocol.hpp"
#include "socket_guard.hpp"
#include "socket_opts.hpp"

volatile sig_atomic_t g_shutdown = 0;

void on_signal(int) { g_shutdown = 1; }

enum class ConnState { Disconnected, Connecting, Connected };

static const char* state_name(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "disconnected";
        case ConnState::Connecting:   return "connecting";
        case ConnState::Connected:    return "connected";
    }
    return "?";
}

static bool connect_to_server(const AppConfig& cfg, SocketGuard& sock) {
    sock.reset(socket(AF_INET, SOCK_STREAM, 0));
    if (!sock.valid()) {
        LOG_ERROR("Failed to create socket");
        return false;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(static_cast<uint16_t>(cfg.port));
    if (inet_pton(AF_INET, cfg.host.c_str(), &serv_addr.sin_addr) <= 0) {
        log_error() << "Invalid IP address: " << cfg.host;
        sock.reset();
        return false;
    }

    if (connect(sock.get(), reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        sock.reset();
        return false;
    }

    set_tcp_nodelay(sock.get());
    return true;
}

int main(int argc, char** argv) {
    const std::string cfg_path = config_path_from_args(argc, argv, "../config/default.conf");
    AppConfig cfg;
    const std::string cfg_err = load_config(cfg_path, cfg);
    if (!cfg_err.empty()) {
        log_error() << "Config error: " << cfg_err;
        log_error() << "Usage: " << (argc > 0 ? argv[0] : "client")
                    << " [config_path] [max_ticks]";
        return 1;
    }

    int max_ticks = 0;
    if (argc >= 3) {
        max_ticks = std::atoi(argv[2]);
        if (max_ticks < 0) max_ticks = 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    using clock = std::chrono::steady_clock;
    ConnState state = ConnState::Disconnected;
    SocketGuard sock;
    int got_ticks = 0;
    int attempt = 0;
    int backoff_ms = cfg.reconnect_initial_ms;
    auto last_rx = clock::now();
    auto last_tick_at = clock::time_point{}; // unset until first tick
    LatencyStats interarrival;

    log_info() << "Client starting → " << cfg.host << ":" << cfg.port
               << " timeout_ms=" << cfg.heartbeat_timeout_ms
               << " max_retries=" << cfg.reconnect_max_retries;

    while (!g_shutdown) {
        if (state != ConnState::Connected) {
            if (attempt > cfg.reconnect_max_retries) {
                LOG_ERROR("Gave up reconnecting");
                return 1;
            }

            state = ConnState::Connecting;
            log_info() << "state=" << state_name(state)
                       << " attempt=" << (attempt + 1)
                       << "/" << (cfg.reconnect_max_retries + 1);

            if (!connect_to_server(cfg, sock)) {
                state = ConnState::Disconnected;
                log_warn() << "Connect failed; backing off " << backoff_ms << "ms"
                           << " state=" << state_name(state);
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                backoff_ms = std::min(backoff_ms * 2, 5000);
                ++attempt;
                continue;
            }

            state = ConnState::Connected;
            attempt = 0;
            backoff_ms = cfg.reconnect_initial_ms;
            last_rx = clock::now();
            last_tick_at = clock::time_point{}; // reset inter-arrival across reconnects
            log_info() << "state=" << state_name(state)
                       << " (Ctrl+C to stop"
                       << (max_ticks > 0 ? ", max_ticks=" + std::to_string(max_ticks) : "")
                       << ")";
        }

        // Wait for data or heartbeat timeout
        const auto now = clock::now();
        const auto silent = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx);
        int wait_ms = cfg.heartbeat_timeout_ms - static_cast<int>(silent.count());
        if (wait_ms < 0) wait_ms = 0;

        pollfd pfd{};
        pfd.fd = sock.get();
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, wait_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll() failed");
            return 1;
        }

        if (pr == 0 || (clock::now() - last_rx) >=
                std::chrono::milliseconds(cfg.heartbeat_timeout_ms)) {
            LOG_WARN("Heartbeat timeout — connection treated as dead");
            sock.reset();
            state = ConnState::Disconnected;
            ++attempt;
            continue;
        }

        if (!(pfd.revents & POLLIN)) continue;

        FeedEvent ev{};
        const IoStatus rx = recv_feed(sock.get(), ev);
        if (rx == IoStatus::Closed) {
            LOG_WARN("Server closed connection");
            sock.reset();
            state = ConnState::Disconnected;
            ++attempt;
            continue;
        }
        if (rx != IoStatus::Ok) {
            if (g_shutdown) break;
            LOG_ERROR("Failed to receive feed message");
            sock.reset();
            state = ConnState::Disconnected;
            ++attempt;
            continue;
        }

        last_rx = clock::now(); // tick or heartbeat both count as liveness

        if (ev.kind == FeedKind::Heartbeat) {
            log_info() << "HEARTBEAT timestamp_ns=" << ev.hb.timestamp_ns;
            continue;
        }

        ++got_ticks;
        const auto tick_at = clock::now();
        if (last_tick_at.time_since_epoch().count() != 0) {
            const auto delta = tick_at - last_tick_at;
            interarrival.add_ns(
                std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count());
        }
        last_tick_at = tick_at;

        log_info() << "TICK #" << got_ticks
                   << " symbol_id=" << ev.tick.symbol_id
                   << " price=" << ev.tick.price
                   << " volume=" << ev.tick.volume;

        if (max_ticks > 0 && got_ticks >= max_ticks) break;
    }

    log_info() << "Client exiting after " << got_ticks << " tick(s)."
               << " final_state=" << state_name(state);
    // Inter-arrival of ticks on this host (not one-way wire latency).
    interarrival.report("tick inter-arrival");
    return 0;
}
