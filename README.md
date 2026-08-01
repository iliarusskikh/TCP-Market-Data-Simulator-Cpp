# TCP Market Data Simulator (C++)

Educational C++ project: a **TCP market data feed simulator** with a binary protocol, single-threaded multi-client `poll()` server, synthetic random-walk ticks, application heartbeats, client reconnect, and inter-arrival latency percentiles.

Built for portfolio / interview depth in quant infrastructure and systems programming — not a real exchange feed. **No third-party libraries** beyond POSIX sockets and the C++ standard library (CMake to build).

**Scope (v1):** `TICK` + `HEARTBEAT` only. Single-threaded `poll()` + non-blocking sockets. No order flow.

| Doc | Purpose |
|-----|---------|
| [`PLAN.md`](PLAN.md) | What shipped, deliberate cuts, future ideas |
| [`docs/protocol.md`](docs/protocol.md) | Wire layouts |
| [`theory/index.html`](theory/index.html) | Concept / decision look-up notes |
| [`LICENSE`](LICENSE) | MIT |

---

## Features

- Single-threaded **`poll()`** server: non-blocking listen + many concurrent clients
- Fixed-size **binary** protocol with endian-safe helpers (`recv_feed`)
- **Random-walk** tick generator (scaled integer prices, round-robin symbols)
- Periodic server **`HEARTBEAT`**; client last-message timeout + **reconnect** (exponential backoff)
- Client **tick inter-arrival** stats: p50 / p99 / p99.9 on exit
- `SocketGuard` RAII, `TCP_NODELAY`, `key=value` config, timestamped logging
- Shared headers are mostly **header-only** helpers under `include/`

## Architecture

```text
                    ┌─────────────────────────────────────┐
                    │  Server (one thread)                │
  config ──────────►│  TickGenerator (random walk)        │
                    │  deadlines: tick_rate + heartbeat   │
                    │  poll(listen + clients)              │
                    │  broadcast TICK / HEARTBEAT         │
                    └──────────────┬──────────────────────┘
                                   │ TCP (binary msgs)
                    ┌──────────────▼──────────────────────┐
                    │  Client(s)                          │
                    │  recv_feed → TICK | HEARTBEAT       │
                    │  last_rx timeout → reconnect        │
                    │  inter-arrival LatencyStats         │
                    └─────────────────────────────────────┘
```

## Project Structure

```
├── CMakeLists.txt
├── LICENSE
├── PLAN.md
├── README.md
├── config/default.conf
├── docs/protocol.md
├── src/server.cpp
├── src/client.cpp
├── include/          # protocol, config, sockets, tick gen, latency, log
└── theory/           # HTML look-up notes
```

## Requirements

- C++11+, POSIX (Linux / macOS / WSL), CMake 3.10+, g++/clang++

## Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..   # -O3 (default if type omitted)
cmake --build .
```

Debug:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

## Run

From `build/`:

```bash
./server ../config/default.conf

# Stop after 50 ticks (prints inter-arrival percentiles on exit):
./client ../config/default.conf 50

# Run until Ctrl+C (omit max_ticks, or pass 0):
./client ../config/default.conf
./client ../config/default.conf 0
```

Reconnect demo: run the client without a tick limit, stop/restart the server — the client backs off and reconnects (until `reconnect_max_retries`).

## Design Decisions

| Choice | Why |
|--------|-----|
| TCP | Reliable ordered stream; focus on framing/protocol, not loss recovery |
| Fixed-size binary | No text parsing; known `sizeof`; explicit endianness |
| Scaled `int64` prices | Deterministic, portable; avoid float on the wire |
| `poll()` + `O_NONBLOCK` | Portable multi-client I/O (no epoll/kqueue requirement) |
| `TCP_NODELAY` | Small messages should not wait on Nagle coalescing |
| App heartbeat | Faster dead-peer detection than relying on TCP alone |
| Inter-arrival latency | Honest local metric; one-way needs synced clocks (out of scope) |
