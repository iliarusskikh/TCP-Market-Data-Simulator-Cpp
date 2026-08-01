#pragma once

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

// Minimal leveled logger with wall-clock (system_clock) timestamps.
// Decision: human-readable log stamps use wall time; control-loop timing
// elsewhere uses steady_clock. No third-party logger.

enum class LogLevel { Info, Warn, Error };

namespace detail {

inline const char* level_tag(LogLevel level) {
    switch (level) {
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

inline std::string timestamp_hhmmss() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[16];
    if (std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf) == 0) {
        return "??:??:??";
    }
    return buf;
}

inline void write(LogLevel level, const std::string& msg) {
    std::ostream& out = (level == LogLevel::Info) ? std::cout : std::cerr;
    out << timestamp_hhmmss() << " [" << level_tag(level) << "] " << msg << '\n';
    out.flush();
}

} // namespace detail

inline void LOG_INFO(const std::string& msg)  { detail::write(LogLevel::Info, msg); }
inline void LOG_WARN(const std::string& msg)  { detail::write(LogLevel::Warn, msg); }
inline void LOG_ERROR(const std::string& msg) { detail::write(LogLevel::Error, msg); }

// Build a message with operator<< : log_info() << "port=" << port;
// Move-only so returning a temporary cannot double-flush.
class LogStream {
public:
    explicit LogStream(LogLevel level) : level_(level), active_(true) {}

    ~LogStream() {
        if (active_) detail::write(level_, ss_.str());
    }

    LogStream(const LogStream&)            = delete;
    LogStream& operator=(const LogStream&) = delete;

    LogStream(LogStream&& other) noexcept
        : level_(other.level_), ss_(std::move(other.ss_)), active_(other.active_) {
        other.active_ = false;
    }

    LogStream& operator=(LogStream&&) = delete;

    template <typename T>
    LogStream& operator<<(const T& v) {
        ss_ << v;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream ss_;
    bool active_;
};

inline LogStream log_info()  { return LogStream(LogLevel::Info); }
inline LogStream log_warn()  { return LogStream(LogLevel::Warn); }
inline LogStream log_error() { return LogStream(LogLevel::Error); }
