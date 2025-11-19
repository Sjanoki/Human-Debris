#!/usr/bin/env python3
import json
import socket
import sys

HOST = "127.0.0.1"
PORT = 7777

COMMANDS = {
    "world": "request_world_summary",
    "spawn_scout": "spawn_test_ship",
    "radar": "radar_scan",
    "thrust": "thrust",
    "dock": "dock",
    "undock": "undock",
    "mine": "mine",
    "sell": "sell_ore",
    "buy": "buy_ship",
    "switch": "switch_ship",
}


def send(sock, payload):
    sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
    data = sock.recv(65536)
    if not data:
        print("Connection closed by server")
        sys.exit(0)
    for line in data.decode("utf-8").splitlines():
        try:
            parsed = json.loads(line)
            print(json.dumps(parsed, indent=2))
        except json.JSONDecodeError:
            print(line)


def main():
    name = input("Player name: ").strip() or "TestPilot"
    with socket.create_connection((HOST, PORT)) as sock:
        send(sock, {"type": "login", "player_name": name})
        print("Type 'help' for a list of commands.")
        while True:
            try:
                raw = input("> ").strip()
            except EOFError:
                break
            if not raw:
                continue
            if raw in ("quit", "exit"):
                send(sock, {"type": "logout"})
                break
            if raw == "help":
                print("Available commands:")
                print("  world -> request summary")
                print("  spawn_scout -> spawn a scout ship")
                print("  thrust <ship_id> <throttle> <duration>")
                print("  radar <ship_id>")
                print("  dock <ship_id> <station_id>")
                print("  undock <ship_id>")
                print("  mine <ship_id> <asteroid_body_id> <duration>")
                print("  sell <ship_id> <station_id>")
                print("  buy <station_id> <class_id>")
                print("  switch <ship_id>")
                print("  logout -> disconnect")
                continue
            parts = raw.split()
            cmd = parts[0]
            if cmd == "world":
                send(sock, {"type": "request_world_summary"})
            elif cmd == "spawn_scout":
                send(sock, {"type": "spawn_test_ship", "ship_class_id": "SCOUT"})
            elif cmd == "thrust" and len(parts) == 4:
                send(sock,
                     {
                         "type": "thrust",
                         "ship_id": int(parts[1]),
                         "throttle": float(parts[2]),
                         "duration": float(parts[3]),
                     })
            elif cmd == "radar" and len(parts) == 2:
                send(sock, {"type": "radar_scan", "ship_id": int(parts[1])})
            elif cmd == "dock" and len(parts) == 3:
                send(sock, {"type": "dock", "ship_id": int(parts[1]), "station_id": int(parts[2])})
            elif cmd == "undock" and len(parts) == 2:
                send(sock, {"type": "undock", "ship_id": int(parts[1])})
            elif cmd == "mine" and len(parts) == 4:
                send(sock,
                     {
                         "type": "mine",
                         "ship_id": int(parts[1]),
                         "asteroid_id": int(parts[2]),
                         "duration": float(parts[3]),
                     })
            elif cmd == "sell" and len(parts) == 3:
                send(sock, {"type": "sell_ore", "ship_id": int(parts[1]), "station_id": int(parts[2])})
            elif cmd == "buy" and len(parts) == 3:
                send(sock, {"type": "buy_ship", "station_id": int(parts[1]), "ship_class_id": parts[2]})
            elif cmd == "switch" and len(parts) == 2:
                send(sock, {"type": "switch_ship", "new_ship_id": int(parts[1])})
            elif cmd == "logout":
                send(sock, {"type": "logout"})
                break
            else:
                print("Unknown or malformed command. Type 'help'.")


if __name__ == "__main__":
    main()
