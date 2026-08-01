#pragma once

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Minimal key=value config loader (no JSON / env matrix).
// Lines: key=value | # comments | blank lines ignored.
// Unknown keys ignored; missing keys keep defaults below.
//
// Typical use: server and client share one file. Server needs symbols,
// tick_rate_hz, heartbeat_interval_ms. Client needs host, port,
// heartbeat_timeout_ms, reconnect_*.

struct AppConfig {
    std::string host         = "127.0.0.1";   // client
    int         port         = 8080;          // both
    std::vector<uint32_t> symbol_ids = {1};   // server generator
    int         tick_rate_hz = 10;            // server broadcast rate
    int         heartbeat_interval_ms = 1000; // server → clients
    int         heartbeat_timeout_ms  = 3000; // client silence limit
    int         reconnect_max_retries = 10;   // client
    int         reconnect_initial_ms  = 100;  // client backoff base
};

namespace detail {

inline std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

inline bool parse_u32_list(const std::string& value, std::vector<uint32_t>& out) {
    out.clear();
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (item.empty()) continue;
        try {
            const unsigned long v = std::stoul(item);
            out.push_back(static_cast<uint32_t>(v));
        } catch (...) {
            return false;
        }
    }
    return !out.empty();
}

} // namespace detail

// Returns empty string on success, or an error message.
inline std::string load_config(const std::string& path, AppConfig& cfg) {
    std::ifstream in(path);
    if (!in) {
        return "cannot open config file: " + path;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = detail::trim(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            return path + ":" + std::to_string(line_no) + ": expected key=value";
        }

        const std::string key = detail::trim(line.substr(0, eq));
        const std::string val = detail::trim(line.substr(eq + 1));
        if (key.empty()) {
            return path + ":" + std::to_string(line_no) + ": empty key";
        }

        try {
            if (key == "host") {
                if (val.empty()) return path + ":" + std::to_string(line_no) + ": host is empty";
                cfg.host = val;
            } else if (key == "port") {
                const int p = std::stoi(val);
                if (p <= 0 || p > 65535) {
                    return path + ":" + std::to_string(line_no) + ": port out of range";
                }
                cfg.port = p;
            } else if (key == "symbols") {
                if (!detail::parse_u32_list(val, cfg.symbol_ids)) {
                    return path + ":" + std::to_string(line_no) + ": invalid symbols list";
                }
            } else if (key == "tick_rate_hz") {
                const int r = std::stoi(val);
                if (r <= 0) {
                    return path + ":" + std::to_string(line_no) + ": tick_rate_hz must be > 0";
                }
                cfg.tick_rate_hz = r;
            } else if (key == "heartbeat_interval_ms") {
                const int v = std::stoi(val);
                if (v <= 0) {
                    return path + ":" + std::to_string(line_no) + ": heartbeat_interval_ms must be > 0";
                }
                cfg.heartbeat_interval_ms = v;
            } else if (key == "heartbeat_timeout_ms") {
                const int v = std::stoi(val);
                if (v <= 0) {
                    return path + ":" + std::to_string(line_no) + ": heartbeat_timeout_ms must be > 0";
                }
                cfg.heartbeat_timeout_ms = v;
            } else if (key == "reconnect_max_retries") {
                const int v = std::stoi(val);
                if (v < 0) {
                    return path + ":" + std::to_string(line_no) + ": reconnect_max_retries must be >= 0";
                }
                cfg.reconnect_max_retries = v;
            } else if (key == "reconnect_initial_ms") {
                const int v = std::stoi(val);
                if (v <= 0) {
                    return path + ":" + std::to_string(line_no) + ": reconnect_initial_ms must be > 0";
                }
                cfg.reconnect_initial_ms = v;
            }
            // unknown keys ignored intentionally
        } catch (...) {
            return path + ":" + std::to_string(line_no) + ": invalid value for " + key;
        }
    }

    if (cfg.port <= 0 || cfg.port > 65535) return "invalid port";
    if (cfg.symbol_ids.empty()) return "symbols list is empty";
    if (cfg.tick_rate_hz <= 0) return "tick_rate_hz must be > 0";
    if (cfg.heartbeat_timeout_ms < cfg.heartbeat_interval_ms) {
        return "heartbeat_timeout_ms should be >= heartbeat_interval_ms";
    }
    return {};
}

// Resolve config path: argv[1] if present, else default_path.
inline std::string config_path_from_args(int argc, char** argv, const char* default_path) {
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') return argv[1];
    return default_path;
}
