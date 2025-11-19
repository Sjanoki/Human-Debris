# Human Debris – Orbital Server MVP

This repository contains a headless C++20 game server that simulates a simple two-dimensional orbital sandbox plus a tiny Python REPL client used for testing gameplay commands over TCP.

## Building the server

```bash
cmake -S . -B build
cmake --build build
```

The build creates the executable `orbital_server` inside `build/server`.

## Running the server

```bash
./build/server/orbital_server
```

Configuration files are located under `server/config`. They are loaded automatically at startup.

## Python client

The `client_python` directory contains a lightweight TCP client. Use it to log in, spawn ships, thrust, mine, dock, trade, and perform radar scans while the server is running.

```bash
cd client_python
python client.py
```

By default the client connects to `localhost:7777`, which matches the server's default port.
