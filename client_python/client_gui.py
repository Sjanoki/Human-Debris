#!/usr/bin/env python3
"""Simple pygame client for visualizing and piloting ships in the Human Debris simulation."""
import json
import math
import queue
import socket
import sys
import threading
from dataclasses import dataclass
from typing import Dict, List, Optional

import pygame

HOST = "127.0.0.1"
PORT = 7777
FPS = 60
WORLD_SCALE = 1.0 / 20000.0  # meters to pixels


@dataclass
class ControlState:
    thrust: bool = False
    turn_left: bool = False
    turn_right: bool = False
    mine: bool = False

    def to_payload(self, ship_id: int) -> Dict[str, object]:
        return {
            "type": "control_state",
            "ship_id": ship_id,
            "thrust": self.thrust,
            "turn_left": self.turn_left,
            "turn_right": self.turn_right,
            "mine": self.mine,
        }


class NetworkClient:
    def __init__(self, host: str, port: int) -> None:
        self.socket = socket.create_connection((host, port))
        self.lock = threading.Lock()
        self.incoming: "queue.Queue[Dict[str, object]]" = queue.Queue()
        self.running = True
        self.thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.thread.start()

    def _reader_loop(self) -> None:
        buffer = ""
        try:
            while self.running:
                data = self.socket.recv(65536)
                if not data:
                    break
                buffer += data.decode("utf-8")
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        payload = json.loads(line)
                        self.incoming.put(payload)
                    except json.JSONDecodeError:
                        print(f"Malformed JSON from server: {line}")
        except OSError:
            pass
        finally:
            self.running = False

    def send(self, payload: Dict[str, object]) -> None:
        data = (json.dumps(payload) + "\n").encode("utf-8")
        with self.lock:
            try:
                self.socket.sendall(data)
            except OSError:
                print("Failed to send payload; connection closed?", file=sys.stderr)

    def get_messages(self) -> List[Dict[str, object]]:
        messages: List[Dict[str, object]] = []
        while True:
            try:
                messages.append(self.incoming.get_nowait())
            except queue.Empty:
                break
        return messages

    def close(self) -> None:
        self.running = False
        try:
            self.socket.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.socket.close()
        self.thread.join(timeout=1.0)


class GuiClient:
    def __init__(self) -> None:
        pygame.init()
        pygame.display.set_caption("Human Debris GUI Client")
        self.screen = pygame.display.set_mode((1200, 800))
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont("consolas", 18)
        self.small_font = pygame.font.SysFont("consolas", 14)
        self.control_state = ControlState()
        self.client = NetworkClient(HOST, PORT)
        self.running = True
        self.player_info: Dict[str, object] = {}
        self.world_state: Dict[str, object] = {}
        self.active_ship_id: int = -1
        self.log_lines: List[str] = []
        self.camera_center = [0.0, 0.0]
        self.focused_station_id: Optional[int] = None

    def log(self, text: str) -> None:
        print(text)
        self.log_lines.append(text)
        self.log_lines = self.log_lines[-8:]

    def request_login(self) -> None:
        name = input("Player name (GUI): ").strip() or "PilotGUI"
        self.client.send({"type": "login", "player_name": name})
        self.client.send({"type": "request_world_summary"})
        self.client.send({"type": "subscribe_world_state", "enabled": True})

    def update_control_flag(self, attr: str, value: bool) -> None:
        if getattr(self.control_state, attr) == value:
            return
        setattr(self.control_state, attr, value)
        self.send_control_state()

    def send_control_state(self) -> None:
        if self.active_ship_id < 0:
            return
        payload = self.control_state.to_payload(self.active_ship_id)
        self.client.send(payload)

    def handle_events(self) -> None:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.running = False
            elif event.type == pygame.KEYDOWN:
                self.on_key_change(event.key, True)
            elif event.type == pygame.KEYUP:
                self.on_key_change(event.key, False)
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                self.handle_mouse_click(event.pos)

    def on_key_change(self, key: int, pressed: bool) -> None:
        if key in (pygame.K_ESCAPE,):
            if pressed:
                self.running = False
            return
        if key in (pygame.K_w, pygame.K_UP):
            self.update_control_flag("thrust", pressed)
        elif key in (pygame.K_a, pygame.K_LEFT):
            self.update_control_flag("turn_left", pressed)
        elif key in (pygame.K_d, pygame.K_RIGHT):
            self.update_control_flag("turn_right", pressed)
        elif key == pygame.K_SPACE:
            self.update_control_flag("mine", pressed)
        elif pressed and key == pygame.K_e:
            self.handle_dock_toggle()
        elif pressed and key == pygame.K_b:
            self.handle_buy_ship()

    def handle_mouse_click(self, screen_pos) -> None:
        if self.active_ship_id >= 0:
            return
        self.try_focus_station(screen_pos)

    def handle_dock_toggle(self) -> None:
        ship_info = self.get_active_ship_info()
        if not ship_info:
            self.log("No active ship for docking commands.")
            return
        if ship_info.get("docked"):
            self.client.send({"type": "undock", "ship_id": self.active_ship_id})
            self.log("Requested undock.")
            return
        station_id = self.find_nearby_station_id()
        if station_id is None:
            self.log("No station nearby to dock with.")
            return
        self.client.send({"type": "dock", "ship_id": self.active_ship_id, "station_id": station_id})
        self.log(f"Requested dock with station {station_id}.")

    def handle_buy_ship(self) -> None:
        ship_info = self.get_active_ship_info()
        if not ship_info or not ship_info.get("docked"):
            self.log("You must be docked to buy ships.")
            return
        station_id = ship_info.get("docked_station_id")
        if station_id is None:
            self.log("Unknown station while docked.")
            return
        self.client.send({"type": "buy_ship", "station_id": station_id, "ship_class_id": "SCOUT"})
        self.log("Requested purchase of SCOUT class ship.")

    def find_nearby_station_id(self) -> Optional[int]:
        if not self.world_state:
            return None
        bodies = {body["body_id"]: body for body in self.world_state.get("bodies", [])}
        stations = self.world_state.get("stations", [])
        ship_body = self.get_active_ship_body()
        if not ship_body:
            return None
        best_station = None
        best_distance = float("inf")
        for station in stations:
            body = bodies.get(station["body_id"])
            if not body:
                continue
            dx = ship_body["x"] - body["x"]
            dy = ship_body["y"] - body["y"]
            distance = math.hypot(dx, dy)
            if distance < best_distance:
                best_distance = distance
                best_station = station["station_id"]
        if best_station is not None and best_distance < 500.0:
            return best_station
        return None

    def get_active_ship_info(self) -> Optional[Dict[str, object]]:
        for ship in self.world_state.get("ships", []):
            if ship.get("ship_id") == self.active_ship_id:
                return ship
        return None

    def get_active_ship_body(self) -> Optional[Dict[str, object]]:
        ship_info = self.get_active_ship_info()
        if not ship_info:
            return None
        body_map = {body["body_id"]: body for body in self.world_state.get("bodies", [])}
        return body_map.get(ship_info.get("body_id"))

    def _station_position(self, preferred_station_id: Optional[int]) -> Optional[tuple]:
        stations = self.world_state.get("stations", [])
        if not stations:
            return None
        body_map = {body["body_id"]: body for body in self.world_state.get("bodies", [])}
        if preferred_station_id is not None:
            for station in stations:
                if station.get("station_id") == preferred_station_id:
                    body = body_map.get(station.get("body_id"))
                    if body:
                        return station.get("station_id"), body.get("x", 0.0), body.get("y", 0.0)
                    break
        for station in stations:
            body = body_map.get(station.get("body_id"))
            if body:
                return station.get("station_id"), body.get("x", 0.0), body.get("y", 0.0)
        return None

    def update_camera_focus(self) -> None:
        if not self.world_state:
            return
        if self.active_ship_id >= 0:
            body = self.get_active_ship_body()
            if body:
                self.camera_center[0] = body.get("x", self.camera_center[0])
                self.camera_center[1] = body.get("y", self.camera_center[1])
            return
        station_id = self.focused_station_id
        ship_info = self.get_active_ship_info()
        if ship_info and ship_info.get("docked"):
            station_id = ship_info.get("docked_station_id", station_id)
        result = self._station_position(station_id)
        if result:
            station_id_resolved, x, y = result
            self.focused_station_id = station_id_resolved
            self.camera_center[0] = x
            self.camera_center[1] = y

    def world_to_screen(self, x: float, y: float) -> pygame.math.Vector2:
        width, height = self.screen.get_size()
        sx = (x - self.camera_center[0]) * WORLD_SCALE + width / 2
        sy = (y - self.camera_center[1]) * WORLD_SCALE + height / 2
        return pygame.math.Vector2(sx, sy)

    def try_focus_station(self, screen_pos) -> None:
        if not self.world_state:
            return
        stations = self.world_state.get("stations", [])
        if not stations:
            return
        body_map = {body["body_id"]: body for body in self.world_state.get("bodies", [])}
        best_station = None
        best_distance = float("inf")
        for station in stations:
            body = body_map.get(station.get("body_id"))
            if not body:
                continue
            screen_point = self.world_to_screen(body.get("x", 0.0), body.get("y", 0.0))
            dist = math.hypot(screen_point.x - screen_pos[0], screen_point.y - screen_pos[1])
            if dist < best_distance and dist <= 40:
                best_distance = dist
                best_station = station.get("station_id")
        if best_station is not None:
            self.focused_station_id = best_station
            self.update_camera_focus()
            self.log(f"Camera focusing station {best_station}.")

    def process_messages(self) -> None:
        for message in self.client.get_messages():
            msg_type = message.get("type")
            if msg_type == "world_summary":
                self.player_info = message.get("player", {})
                new_ship = self.player_info.get("active_ship_id", -1)
                if new_ship != self.active_ship_id:
                    self.active_ship_id = new_ship
                    self.send_control_state()
                self.log("Received world summary.")
            elif msg_type == "world_state":
                self.world_state = message
                player_block = message.get("player", {})
                self.player_info = player_block
                new_ship = player_block.get("active_ship_id", self.active_ship_id)
                if new_ship != self.active_ship_id:
                    self.active_ship_id = new_ship
                    self.send_control_state()
                self.update_camera_focus()
            elif msg_type == "action_result":
                self.log(f"Action: {message.get('message')}")
            elif msg_type == "error":
                self.log(f"Error: {message.get('message')}")

    def draw(self) -> None:
        self.screen.fill((4, 6, 16))
        self.draw_world()
        self.draw_hud()
        pygame.display.flip()

    def draw_world(self) -> None:
        if not self.world_state:
            text = self.font.render("Waiting for world state...", True, (255, 255, 255))
            self.screen.blit(text, (20, 20))
            return
        self.update_camera_focus()

        def project(x: float, y: float) -> pygame.math.Vector2:
            return self.world_to_screen(x, y)

        # Planet
        planet = self.world_state.get("planet", {})
        pr = planet.get("radius", 0.0) * WORLD_SCALE
        pr = max(10, min(pr, 400))
        planet_pos = project(0.0, 0.0)
        pygame.draw.circle(self.screen, (30, 90, 150), (int(planet_pos.x), int(planet_pos.y)), int(pr), width=0)

        # Bodies
        for body in self.world_state.get("bodies", []):
            position = project(body.get("x", 0.0), body.get("y", 0.0))
            center = (int(position.x), int(position.y))
            btype = body.get("type")
            if btype == "Asteroid":
                pygame.draw.circle(self.screen, (200, 200, 100), center, 6)
            elif btype == "Station":
                rect = pygame.Rect(0, 0, 40, 20)
                rect.center = (int(position.x), int(position.y))
                pygame.draw.rect(self.screen, (120, 200, 255), rect, border_radius=4)
            elif btype == "Ship":
                self.draw_ship_shape(body, position)

    def draw_ship_shape(self, body: Dict[str, object], position: pygame.math.Vector2) -> None:
        angle = body.get("angle", 0.0)
        size = 16
        points = []
        for local in ((0, size), (-size / 2, -size / 2), (size / 2, -size / 2)):
            px = position.x + local[0] * math.cos(angle) - local[1] * math.sin(angle)
            py = position.y + local[0] * math.sin(angle) + local[1] * math.cos(angle)
            points.append((px, py))
        pygame.draw.polygon(self.screen, (255, 120, 120), points, width=0)

    def draw_hud(self) -> None:
        info_lines = []
        if self.player_info:
            info_lines.append(f"Pilot: {self.player_info.get('name', 'Unknown')}")
            info_lines.append(f"Credits: {self.player_info.get('credits', 0):.0f}")
            if self.active_ship_id >= 0:
                info_lines.append(f"Active Ship: {self.active_ship_id}")
            else:
                focus_label = self.focused_station_id if self.focused_station_id is not None else "N/A"
                info_lines.append(f"Active Ship: None (focusing station {focus_label})")
        ship_info = self.get_active_ship_info()
        if ship_info:
            info_lines.append(f"Fuel: {ship_info.get('fuel_mass', 0):.1f}")
            info_lines.append(f"Cargo: {ship_info.get('cargo_mass', 0):.1f}/{ship_info.get('cargo_capacity', 0)} kg")
            if ship_info.get("docked"):
                info_lines.append(f"Docked at Station {ship_info.get('docked_station_id')}")
        info_lines.append("Controls: W/Up=Thrust, A/Left=Turn Left, D/Right=Turn Right")
        info_lines.append("Space=Mine, E=Dock/Undock, B=Buy SCOUT")
        if self.active_ship_id < 0:
            info_lines.append("Click a station to move the camera focus.")

        y = 10
        for line in info_lines:
            text = self.small_font.render(line, True, (255, 255, 255))
            self.screen.blit(text, (10, y))
            y += 18

        log_y = self.screen.get_height() - 18 * len(self.log_lines) - 10
        for line in self.log_lines:
            text = self.small_font.render(line, True, (200, 200, 200))
            self.screen.blit(text, (10, log_y))
            log_y += 18

    def run(self) -> None:
        self.request_login()
        while self.running:
            self.handle_events()
            self.process_messages()
            self.draw()
            self.clock.tick(FPS)
        self.shutdown()

    def shutdown(self) -> None:
        self.client.send({"type": "logout"})
        self.client.close()
        pygame.quit()


def main() -> None:
    try:
        app = GuiClient()
        app.run()
    except ConnectionRefusedError:
        print("Unable to connect to the server. Please start the server first.")


if __name__ == "__main__":
    main()
