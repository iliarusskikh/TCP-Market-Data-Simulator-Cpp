#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "config.hpp"
#include "protocol.hpp"

// Synthetic last-trade feed (not exchange-realistic microstructure).
//
// Decision: per-symbol scaled int64 prices (micros) + small random walk, round-robin
// across config symbols. Same generator fans out identical ticks to every client.
// Fixed seed (passed from server) makes demos reproducible.

class TickGenerator {
public:
    TickGenerator(const AppConfig& cfg, uint32_t seed)
        : rng_(seed)
        , delta_dist_(-5000, 5000)   // ±0.005 if scale is 1e6
        , volume_dist_(1, 100)
    {
        symbols_.reserve(cfg.symbol_ids.size());
        const int64_t start = 100000000; // 100.0 with micro scale
        for (uint32_t id : cfg.symbol_ids) {
            SymbolState s;
            s.id = id;
            s.price = start + static_cast<int64_t>(id) * 1000000; // slight offset per symbol
            symbols_.push_back(s);
        }
    }

    // Advance one symbol (round-robin), apply walk, return a host-order TickMsg.
    TickMsg next() {
        SymbolState& s = symbols_[rr_ % symbols_.size()];
        ++rr_;

        s.price += delta_dist_(rng_);
        const int64_t floor_px = 1000;            // 0.001
        const int64_t ceil_px  = 1000000000000LL;
        if (s.price < floor_px) s.price = floor_px;
        if (s.price > ceil_px)  s.price = ceil_px;

        TickMsg tick{};
        tick.msg_type     = static_cast<uint8_t>(MsgType::TICK);
        tick.symbol_id    = s.id;
        tick.price        = s.price;
        tick.timestamp_ns = now_ns_steady();
        tick.volume       = static_cast<uint32_t>(volume_dist_(rng_));
        return tick;
    }

private:
    struct SymbolState {
        uint32_t id;
        int64_t  price;
    };

    static uint64_t now_ns_steady() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }

    std::vector<SymbolState> symbols_;
    std::mt19937 rng_;
    std::uniform_int_distribution<int> delta_dist_;
    std::uniform_int_distribution<int> volume_dist_;
    size_t rr_ = 0;
};
