# TCP Market Data Simulator — Plan

**Status:** v1 complete and publish-ready.

**Scope lock:** market data only (`TICK` + `HEARTBEAT`). Single-threaded `poll()` + non-blocking sockets. No order flow.

Docs: [README](README.md) · [protocol](docs/protocol.md) · [theory look-ups](theory/index.html)

---

## What shipped

- Berkeley TCP server/client with `SO_REUSEADDR`, `TCP_NODELAY`, signal-aware shutdown
- Fixed-size packed binary protocol + endian helpers + `recv_feed` framing
- External `key=value` config
- `SocketGuard` RAII; minimal timestamped logging
- Multi-client `poll()` loop with per-client outbound queues / `EAGAIN` handling
- Synthetic random-walk tick generator (scaled prices, round-robin symbols)
- Server heartbeats; client last-message timeout + reconnect with exponential backoff
- Client tick inter-arrival percentiles (p50 / p99 / p99.9)

---

## Completed roadmap

| Phase | Goal |
|-------|------|
| 0 | Blocking TCP echo baseline |
| 1 | Binary protocol + config |
| 2 | RAII sockets, `TCP_NODELAY`, logging |
| 3 | Non-blocking + `poll()` multi-client broadcast |
| 4 | Random-walk market-data generator |
| 5 | Heartbeat + client reconnect / connection states |
| 6 | Latency percentiles + docs polish |

Theory notes map to the concepts behind these choices (not a build tutorial).

---

## Out of scope (deliberate)

| Cut | Why |
|-----|-----|
| ORDER / ACK, order book | Market-data-only scope |
| Lock-free SPSC rings, object pools | Separate concurrency / HFT deep-dive |
| epoll / kqueue | `poll()` is portable and enough for the story |
| Thread pool / thread-per-client | Single-threaded `poll` is the architecture |
| CPU pinning, huge pages, rdtsc | Overkill for a learning simulator |
| TLS, FIX, Docker, Prometheus | Ops / protocol surface beyond the goal |
| JSON + env/CLI config matrix, spdlog | Keep config and logging minimal |

---

## Future / next phases

Ideas for a follow-on project or v2 — not required for the current CV story:

1. **Client symbol filter / subscription** — ignore ticks whose `symbol_id` is outside a client interest set (or negotiate a simple subscribe message).
2. **Slow-client policy** — cap outbound queue size; drop oldest ticks or disconnect when a client cannot keep up (today the queue can grow unbounded).
3. **Protocol unit tests** — golden vectors for `hton`/`ntoh` and `recv_feed` framing (partial reads, unknown `msg_type`).
4. **Optional `epoll` path** — `#ifdef __linux__` backend behind the same session abstraction; keep `poll` as default/macOS path.
5. **ORDER/ACK exercise** — separate small extension (or sibling project) with a tiny order state machine; keep it out of the feed core.
6. **Quieter logging** — HEARTBEAT at DEBUG / rate-limited INFO so long runs stay readable.
7. **Config seed** — expose RNG seed in `default.conf` instead of hard-coding `42` in the server.

---

## Concepts checklist

Useful when revisiting theory notes:

- Berkeley sockets lifecycle · TCP vs UDP
- Binary protocol, endianness, partial reads
- Blocking vs non-blocking · `poll`
- Nagle / `TCP_NODELAY` · `SO_REUSEADDR` / TIME_WAIT
- App heartbeat vs TCP keepalive · backoff
- Scaled prices · synthetic ticks
- Latency percentiles · RAII for socket fds
