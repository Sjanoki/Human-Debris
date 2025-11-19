#!/usr/bin/env python3
"""Simple pygame client for visualizing and piloting ships in the Human Debris simulation."""
import json
import math
import queue
import socket
import sys
import threading
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

import pygame

HOST = "127.0.0.1"
PORT = 7777
FPS = 60
BASE_WORLD_SCALE = 1.0 / 20000.0
ZOOM_STEP = 1.1
ZOOM_MIN = 0.25
ZOOM_MAX = 50.0
TARGET_PICK_RADIUS_PX = 30
SCALE_BAR_PIXELS = 120
ORBIT_STEPS = 900
MIN_ORBIT_DURATION = 600.0
MAX_ORBIT_DURATION = 20000.0


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
        self.body_map: Dict[int, Dict[str, object]] = {}
        self.station_map: Dict[int, Dict[str, object]] = {}
        self.station_body_lookup: Dict[int, int] = {}
        self.ship_classes: Dict[str, Dict[str, object]] = {}
        self.active_ship_id: int = -1
        self.log_lines: List[str] = []
        self.camera_center = [0.0, 0.0]
        self.focused_station_id: Optional[int] = None
        self.zoom = 1.0
        self.base_scale = BASE_WORLD_SCALE
        self.planet_radius = 0.0
        self.planet_mu = 0.0
        self.selected_target_id: Optional[int] = None
        self.selected_target_label: str = ""
        self.active_orbit_path: List[Tuple[float, float]] = []
        self.target_orbit_path: List[Tuple[float, float]] = []
        self.station_buttons: List[Tuple[str, pygame.Rect, Dict[str, object]]] = []
        self.logout_sent = False

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

    def adjust_zoom(self, wheel_delta: int) -> None:
        if wheel_delta > 0:
            self.zoom *= ZOOM_STEP ** wheel_delta
        elif wheel_delta < 0:
            self.zoom /= ZOOM_STEP ** (-wheel_delta)
        self.zoom = max(ZOOM_MIN, min(ZOOM_MAX, self.zoom))

    def request_exit(self) -> None:
        if not self.running:
            return
        self.ensure_logout_sent()
        self.running = False

    def ensure_logout_sent(self) -> None:
        if self.logout_sent:
            return
        self.client.send({"type": "logout"})
        self.logout_sent = True

    def handle_events(self) -> None:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.request_exit()
            elif event.type == pygame.KEYDOWN:
                self.on_key_change(event.key, True)
            elif event.type == pygame.KEYUP:
                self.on_key_change(event.key, False)
            elif event.type == pygame.MOUSEBUTTONDOWN:
                self.handle_mouse_click(event.pos, event.button)
            elif event.type == pygame.MOUSEWHEEL:
                self.adjust_zoom(event.y)

    def on_key_change(self, key: int, pressed: bool) -> None:
        if key in (pygame.K_ESCAPE, pygame.K_q):
            if pressed:
                self.request_exit()
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

    def handle_mouse_click(self, screen_pos: Tuple[int, int], button: int) -> None:
        if button != 1:
            return
        if self.handle_station_panel_click(screen_pos):
            return
        if not self.world_state:
            return
        body = self.pick_body_at_screen(screen_pos)
        if body:
            self.selected_target_id = body.get("body_id")
            self.selected_target_label = self.describe_body(body)
            self.log(f"Selected {self.selected_target_label}.")
            if self.active_ship_id < 0 and body.get("type") == "Station":
                station_id = self.find_station_id_from_body(body.get("body_id"))
                if station_id is not None:
                    self.focused_station_id = station_id
                    self.log(f"Camera focusing station {station_id}.")
        else:
            if self.selected_target_id is not None:
                self.log("Cleared target selection.")
            self.selected_target_id = None
            self.selected_target_label = ""
        self.update_orbit_paths()

    def handle_station_panel_click(self, screen_pos: Tuple[int, int]) -> bool:
        for action, rect, payload in self.station_buttons:
            if rect.collidepoint(screen_pos):
                if action == "sell":
                    self.send_sell_all(payload["station_id"])
                elif action == "buy":
                    self.send_buy_ship(payload["station_id"], payload["ship_class_id"])
                return True
        return False

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
        self.send_buy_ship(station_id, "SCOUT")

    def send_sell_all(self, station_id: int) -> None:
        if self.active_ship_id < 0:
            self.log("No active ship to sell cargo.")
            return
        self.client.send({"type": "sell_ore", "ship_id": self.active_ship_id, "station_id": station_id})
        self.log(f"Requested cargo sale at station {station_id}.")

    def send_buy_ship(self, station_id: int, ship_class_id: str) -> None:
        self.client.send({"type": "buy_ship", "station_id": station_id, "ship_class_id": ship_class_id})
        self.log(f"Requested purchase of {ship_class_id} at station {station_id}.")

    def find_nearby_station_id(self) -> Optional[int]:
        if not self.body_map or not self.world_state:
            return None
        ship_body = self.get_active_ship_body()
        if not ship_body:
            return None
        best_station = None
        best_distance = float("inf")
        for station in self.world_state.get("stations", []):
            body = self.body_map.get(station.get("body_id"))
            if not body:
                continue
            dx = ship_body["x"] - body.get("x", 0.0)
            dy = ship_body["y"] - body.get("y", 0.0)
            distance = math.hypot(dx, dy)
            if distance < best_distance:
                best_distance = distance
                best_station = station.get("station_id")
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
        return self.body_map.get(ship_info.get("body_id"))

    def get_station_by_id(self, station_id: Optional[int]) -> Optional[Dict[str, object]]:
        if station_id is None:
            return None
        return self.station_map.get(station_id)

    def find_station_id_from_body(self, body_id: Optional[int]) -> Optional[int]:
        if body_id is None:
            return None
        return self.station_body_lookup.get(body_id)

    def update_camera_focus(self) -> None:
        if not self.world_state:
            return
        body = self.get_active_ship_body()
        if body:
            self.camera_center[0] = body.get("x", self.camera_center[0])
            self.camera_center[1] = body.get("y", self.camera_center[1])
            return
        station_id = self.focused_station_id
        if station_id is None:
            station_id = self.player_info.get("docked_station_id", station_id)
        ship_info = self.get_active_ship_info()
        if ship_info and ship_info.get("docked"):
            station_id = ship_info.get("docked_station_id", station_id)
        result = self._station_position(station_id)
        if result:
            station_id_resolved, x, y = result
            self.focused_station_id = station_id_resolved
            self.camera_center[0] = x
            self.camera_center[1] = y

    def _station_position(self, preferred_station_id: Optional[int]) -> Optional[Tuple[int, float, float]]:
        if not self.station_map:
            return None
        if preferred_station_id is not None:
            station = self.station_map.get(preferred_station_id)
            if station:
                body = self.body_map.get(station.get("body_id"))
                if body:
                    return preferred_station_id, body.get("x", 0.0), body.get("y", 0.0)
        for station_id, station in self.station_map.items():
            body = self.body_map.get(station.get("body_id"))
            if body:
                return station_id, body.get("x", 0.0), body.get("y", 0.0)
        return None

    def world_to_screen(self, x: float, y: float) -> pygame.math.Vector2:
        width, height = self.screen.get_size()
        scale = self.base_scale * self.zoom
        sx = (x - self.camera_center[0]) * scale + width / 2
        sy = height / 2 - (y - self.camera_center[1]) * scale
        return pygame.math.Vector2(sx, sy)

    def screen_to_world(self, sx: float, sy: float) -> Tuple[float, float]:
        width, height = self.screen.get_size()
        scale = self.base_scale * self.zoom
        if scale == 0:
            return (self.camera_center[0], self.camera_center[1])
        dx = (sx - width / 2) / scale
        dy = (height / 2 - sy) / scale
        return self.camera_center[0] + dx, self.camera_center[1] + dy

    def pick_body_at_screen(self, screen_pos: Tuple[int, int]) -> Optional[Dict[str, object]]:
        if not self.body_map:
            return None
        world_point = self.screen_to_world(screen_pos[0], screen_pos[1])
        scale = self.base_scale * self.zoom
        if scale <= 0:
            return None
        threshold_world = TARGET_PICK_RADIUS_PX / scale
        best_body: Optional[Dict[str, object]] = None
        best_distance = float("inf")
        for body in self.body_map.values():
            dx = world_point[0] - body.get("x", 0.0)
            dy = world_point[1] - body.get("y", 0.0)
            distance = math.hypot(dx, dy)
            if distance < best_distance and distance <= threshold_world:
                best_distance = distance
                best_body = body
        return best_body

    def describe_body(self, body: Dict[str, object]) -> str:
        label = f"{body.get('type', 'Body')} #{body.get('body_id')}"
        if body.get("type") == "Station":
            station_id = self.find_station_id_from_body(body.get("body_id"))
            station = self.get_station_by_id(station_id)
            if station:
                name = station.get("name") or f"Station {station_id}"
                label = f"{name} (#{station_id})"
        return label

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
                self.player_info = message.get("player", {})
                self.body_map = {body["body_id"]: body for body in message.get("bodies", [])}
                self.station_map = {station["station_id"]: station for station in message.get("stations", [])}
                self.station_body_lookup = {
                    station.get("body_id"): station_id
                    for station_id, station in self.station_map.items()
                    if station.get("body_id") is not None
                }
                self.ship_classes = {
                    sc.get("ship_class_id"): sc for sc in message.get("ship_classes", [])
                }
                planet = message.get("planet", {})
                self.planet_radius = planet.get("radius", self.planet_radius)
                self.planet_mu = planet.get("mu", self.planet_mu)
                new_ship = self.player_info.get("active_ship_id", self.active_ship_id)
                if new_ship != self.active_ship_id:
                    self.active_ship_id = new_ship
                    self.send_control_state()
                if self.focused_station_id is None and self.station_map:
                    self.focused_station_id = next(iter(self.station_map.keys()))
                if self.selected_target_id is not None and self.selected_target_id not in self.body_map:
                    self.selected_target_id = None
                    self.selected_target_label = ""
                self.update_camera_focus()
                self.update_orbit_paths()
            elif msg_type == "action_result":
                self.log(f"Action: {message.get('message')}")
            elif msg_type == "error":
                self.log(f"Error: {message.get('message')}")

    def update_orbit_paths(self) -> None:
        self.active_orbit_path = []
        self.target_orbit_path = []
        if self.planet_mu <= 0:
            return
        active_body = self.get_active_ship_body()
        if active_body:
            self.active_orbit_path = self.compute_orbit_path(active_body)
        if self.selected_target_id is not None:
            body = self.body_map.get(self.selected_target_id)
            if body:
                self.target_orbit_path = self.compute_orbit_path(body)
            else:
                self.selected_target_id = None
                self.selected_target_label = ""

    def compute_orbit_path(self, body: Dict[str, object]) -> List[Tuple[float, float]]:
        x = body.get("x", 0.0)
        y = body.get("y", 0.0)
        vx = body.get("vx", 0.0)
        vy = body.get("vy", 0.0)
        r0 = math.hypot(x, y)
        if r0 <= 0 or self.planet_mu <= 0:
            return []
        orbital_period = 2.0 * math.pi * math.sqrt(max(r0, 1.0) ** 3 / self.planet_mu)
        total_time = min(max(orbital_period, MIN_ORBIT_DURATION), MAX_ORBIT_DURATION)
        dt = total_time / ORBIT_STEPS
        points: List[Tuple[float, float]] = []
        px, py = x, y
        pvx, pvy = vx, vy
        for _ in range(ORBIT_STEPS):
            points.append((px, py))
            r = math.hypot(px, py)
            if r < max(1.0, self.planet_radius * 0.9):
                break
            if r > 2e8:
                break
            accel = -self.planet_mu / max(r ** 3, 1.0)
            ax = accel * px
            ay = accel * py
            pvx += ax * dt
            pvy += ay * dt
            px += pvx * dt
            py += pvy * dt
        return points

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
        self.draw_orbits()
        planet = self.world_state.get("planet", {})
        planet_pos = self.world_to_screen(0.0, 0.0)
        planet_radius = max(10, int(min(self.planet_radius * self.base_scale * self.zoom, 400)))
        pygame.draw.circle(self.screen, (30, 90, 150), (int(planet_pos.x), int(planet_pos.y)), planet_radius)

        active_body = self.get_active_ship_body()
        active_body_id = active_body.get("body_id") if active_body else None

        for body in self.body_map.values():
            position = self.world_to_screen(body.get("x", 0.0), body.get("y", 0.0))
            btype = body.get("type")
            if btype == "Asteroid":
                pygame.draw.circle(self.screen, (200, 200, 100), (int(position.x), int(position.y)), 6)
            elif btype == "Station":
                rect = pygame.Rect(0, 0, 40, 20)
                rect.center = (int(position.x), int(position.y))
                pygame.draw.rect(self.screen, (120, 200, 255), rect, border_radius=4)
                if body.get("body_id") == self.selected_target_id:
                    pygame.draw.rect(self.screen, (255, 230, 120), rect, width=2, border_radius=4)
            elif btype == "Ship":
                self.draw_ship_shape(body, position, active_body_id)
            if body.get("body_id") == self.selected_target_id and btype != "Station":
                pygame.draw.circle(self.screen, (255, 230, 120), (int(position.x), int(position.y)), 10, width=2)

        self.draw_scale_bar()
        self.draw_station_panel()

    def draw_orbits(self) -> None:
        if len(self.active_orbit_path) > 1:
            self.draw_orbit_path(self.active_orbit_path, (80, 220, 255))
        if len(self.target_orbit_path) > 1:
            self.draw_orbit_path(self.target_orbit_path, (255, 200, 60))

    def draw_orbit_path(self, points: List[Tuple[float, float]], color: Tuple[int, int, int]) -> None:
        projected = [self.world_to_screen(x, y) for x, y in points]
        if len(projected) >= 2:
            pygame.draw.lines(
                self.screen,
                color,
                False,
                [(int(p.x), int(p.y)) for p in projected],
                1,
            )

    def draw_ship_shape(self, body: Dict[str, object], position: pygame.math.Vector2, active_body_id: Optional[int]) -> None:
        angle = body.get("angle", 0.0)
        size = 16
        color = (255, 120, 120)
        if body.get("body_id") == active_body_id:
            color = (100, 255, 160)
        elif body.get("body_id") == self.selected_target_id:
            color = (255, 230, 80)
        points = []
        for local in ((0, size), (-size / 2, -size / 2), (size / 2, -size / 2)):
            px = position.x + local[0] * math.cos(angle) - local[1] * math.sin(angle)
            py = position.y + local[0] * math.sin(angle) + local[1] * math.cos(angle)
            points.append((px, py))
        pygame.draw.polygon(self.screen, color, points, width=0)

    def draw_scale_bar(self) -> None:
        scale = self.base_scale * self.zoom
        if scale <= 0:
            return
        world_distance = SCALE_BAR_PIXELS / scale
        label = self.format_distance(world_distance)
        x0 = 20
        y0 = self.screen.get_height() - 30
        pygame.draw.line(self.screen, (255, 255, 255), (x0, y0), (x0 + SCALE_BAR_PIXELS, y0), 2)
        pygame.draw.line(self.screen, (255, 255, 255), (x0, y0 - 5), (x0, y0 + 5), 2)
        pygame.draw.line(
            self.screen,
            (255, 255, 255),
            (x0 + SCALE_BAR_PIXELS, y0 - 5),
            (x0 + SCALE_BAR_PIXELS, y0 + 5),
            2,
        )
        text = self.small_font.render(label, True, (255, 255, 255))
        self.screen.blit(text, (x0, y0 - 25))

    def format_distance(self, meters: float) -> str:
        units = [
            (1e9, "Gm"),
            (1e6, "Mm"),
            (1000.0, "km"),
            (1.0, "m"),
        ]
        for threshold, suffix in units:
            if meters >= threshold:
                return f"{meters / threshold:.1f} {suffix}"
        return f"{meters:.2f} m"

    def draw_station_panel(self) -> None:
        self.station_buttons = []
        ship_info = self.get_active_ship_info()
        if not ship_info or not ship_info.get("docked"):
            return
        station_id = ship_info.get("docked_station_id")
        station = self.get_station_by_id(station_id)
        if not station:
            return
        panel_width = 320
        panel_rect = pygame.Rect(self.screen.get_width() - panel_width - 20, 20, panel_width, 360)
        pygame.draw.rect(self.screen, (20, 32, 64), panel_rect, border_radius=6)
        pygame.draw.rect(self.screen, (80, 120, 200), panel_rect, width=2, border_radius=6)
        y = panel_rect.top + 12
        name = station.get("name") or f"Station {station_id}"
        lines = [name, f"Station #{station_id}"]
        cargo_mass = ship_info.get("cargo_mass", 0.0)
        cargo_cap = ship_info.get("cargo_capacity", 0.0)
        lines.append(f"Cargo: {cargo_mass:.1f}/{cargo_cap:.1f} kg")
        lines.append(f"Fuel: {ship_info.get('fuel_mass', 0.0):.1f}")
        for line in lines:
            text = self.small_font.render(line, True, (255, 255, 255))
            self.screen.blit(text, (panel_rect.left + 12, y))
            y += 20
        y += 5
        sell_rect = pygame.Rect(panel_rect.left + 12, y, panel_rect.width - 24, 32)
        pygame.draw.rect(self.screen, (90, 160, 120), sell_rect, border_radius=4)
        sell_text = self.small_font.render("Sell all cargo", True, (0, 0, 0))
        text_pos = sell_text.get_rect(center=sell_rect.center)
        self.screen.blit(sell_text, text_pos)
        self.station_buttons.append(("sell", sell_rect, {"station_id": station_id}))
        y += 42
        market = station.get("market", {})
        offers = market.get("ship_offers", [])
        offer_title = self.small_font.render("Ships for sale:", True, (255, 255, 255))
        self.screen.blit(offer_title, (panel_rect.left + 12, y))
        y += 22
        for offer in offers:
            label = f"{offer.get('ship_class_id', 'SHIP')} - {offer.get('price', 0):.0f} cr"
            rect = pygame.Rect(panel_rect.left + 12, y, panel_rect.width - 24, 28)
            pygame.draw.rect(self.screen, (140, 140, 200), rect, border_radius=4)
            text = self.small_font.render(label, True, (0, 0, 0))
            text_pos = text.get_rect(center=rect.center)
            self.screen.blit(text, text_pos)
            self.station_buttons.append(("buy", rect, {"station_id": station_id, "ship_class_id": offer.get("ship_class_id", "SCOUT")}))
            y += 34

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
            if self.selected_target_label:
                info_lines.append(f"Target: {self.selected_target_label}")
            else:
                info_lines.append("Target: None")
            info_lines.append(f"Zoom: {self.zoom:.3f}x")
        ship_info = self.get_active_ship_info()
        if ship_info:
            info_lines.append(f"Fuel: {ship_info.get('fuel_mass', 0):.1f}")
            info_lines.append(
                f"Cargo: {ship_info.get('cargo_mass', 0):.1f}/{ship_info.get('cargo_capacity', 0)} kg"
            )
            if ship_info.get("docked"):
                info_lines.append(f"Docked at Station {ship_info.get('docked_station_id')}")
        info_lines.append("Controls: W/Up=Thrust, A/Left=Turn Left, D/Right=Turn Right")
        info_lines.append("Space=Mine, E=Dock/Undock, B=Quick buy SCOUT")
        info_lines.append("Mousewheel=Zoom, Click=Select target, Esc/Q=Logout")
        info_lines.append("Docked: use panel buttons to sell/buy ships")
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
        self.ensure_logout_sent()
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
