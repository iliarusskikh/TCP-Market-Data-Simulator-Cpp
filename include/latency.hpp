#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "log.hpp"

// Client-side timing stats: local inter-arrival between consecutive TICKs.
//
// Decision: measure with steady_clock deltas on the client — honest without
// synced clocks. This is NOT one-way wire latency (that would need a shared
// time base between server timestamp_ns and the client).

class LatencyStats {
public:
    void add_ns(int64_t sample_ns) {
        if (sample_ns < 0) return;
        samples_ns_.push_back(sample_ns);
    }

    std::size_t count() const { return samples_ns_.size(); }

    // Returns false if there are no samples to report.
    bool report(const char* label) const {
        if (samples_ns_.empty()) {
            log_info() << label << ": no samples";
            return false;
        }

        std::vector<int64_t> sorted = samples_ns_;
        std::sort(sorted.begin(), sorted.end());

        const double p50 = percentile_ns(sorted, 0.50);
        const double p99 = percentile_ns(sorted, 0.99);
        const double p999 = percentile_ns(sorted, 0.999);

        log_info() << label << ": n=" << sorted.size()
                   << " p50=" << format_us(p50)
                   << " p99=" << format_us(p99)
                   << " p99.9=" << format_us(p999)
                   << " min=" << format_us(static_cast<double>(sorted.front()))
                   << " max=" << format_us(static_cast<double>(sorted.back()));
        return true;
    }

private:
    static double percentile_ns(const std::vector<int64_t>& sorted, double p) {
        if (sorted.empty()) return 0.0;
        if (sorted.size() == 1) return static_cast<double>(sorted[0]);
        const double idx = p * static_cast<double>(sorted.size() - 1);
        const std::size_t lo = static_cast<std::size_t>(idx);
        const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
        const double frac = idx - static_cast<double>(lo);
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }

    static std::string format_us(double ns) {
        const double us = ns / 1000.0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1fµs", us);
        return buf;
    }

    std::vector<int64_t> samples_ns_;
};
