# Binary Protocol

Wire format for the TCP Market Data Simulator. All multi-byte integers are **network byte order** (big-endian). Structs are packed (`#pragma pack(1)`).

## Message types

| `msg_type` | Name | Size |
|------------|------|------|
| `1` | `TICK` | 25 bytes |
| `2` | `HEARTBEAT` | 9 bytes |

## Framing

TCP delivers a byte stream. The client uses `recv_feed` in `protocol.hpp`:

1. Read **1 byte** `msg_type` (consumed, not peeked)
2. Read the remaining `sizeof(msg) - 1` bytes for that type
3. Convert multi-byte fields with `ntoh*` helpers

The server builds host-order structs, converts with `*_to_wire`, and queues bytes via `ClientSession::enqueue_tick` / `enqueue_heartbeat`.

## `TickMsg` (25 bytes)

| Offset | Field | Type | Notes |
|--------|-------|------|--------|
| 0 | `msg_type` | `uint8_t` | `1` |
| 1 | `symbol_id` | `uint32_t` | Instrument id |
| 5 | `price` | `int64_t` | Scaled integer (micros: `100000000` = 100.0) |
| 13 | `timestamp_ns` | `uint64_t` | Server `steady_clock` nanos since epoch |
| 21 | `volume` | `uint32_t` | Synthetic size |

## `HeartbeatMsg` (9 bytes)

| Offset | Field | Type | Notes |
|--------|-------|------|--------|
| 0 | `msg_type` | `uint8_t` | `2` |
| 1 | `timestamp_ns` | `uint64_t` | Server stamp |

## Direction

- **Server → client:** `TICK` (market data) and `HEARTBEAT` (liveness)
- **Client → server:** none in current scope (market-data feed only)

See `include/protocol.hpp` for serialize helpers and `recv_feed`.
