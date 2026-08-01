# TCP-Market-Data-Simulator
C++ TCP market-data feed simulator: fixed-size binary TICK/HEARTBEAT protocol, multi-client poll() server, RAII sockets, synthetic random-walk ticks, app heartbeats with client reconnect/backoff, and tick inter-arrival p50/p99 stats. POSIX + stdlib only — portfolio systems/quant-infra project, not a live exchange.
