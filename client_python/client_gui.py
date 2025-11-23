#!/usr/bin/env python3
"""Simple pygame client for visualizing and piloting ships in the Human Debris simulation."""
import json
import math
import queue
import socket
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import pygame

HOST = "127.0.0.1"
PORT = 7777
FPS = 60
BASE_WORLD_SCALE = 1.0 / 20000.0
ZOOM_STEP = 1.1
ZOOM_MIN = 0.25
ZOOM_MAX = 240000.0
TARGET_PICK_RADIUS_PX = 30
ICON_THRESHOLD_PX = 20
TILE_DEBUG_THRESHOLD_PX = 8
SCALE_BAR_PIXELS = 120
ORBIT_STEPS = 900
MIN_ORBIT_DURATION = 600.0
MAX_ORBIT_DURATION = 20000.0
INTERCEPT_DISTANCE_THRESHOLD = 1e6


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
        self.ship_shapes: Dict[str, Dict[str, object]] = {}
        self.station_shapes: Dict[int, Dict[str, object]] = {}
        self.blueprints: Dict[str, Dict[str, object]] = {}
        self.default_ship_shape = {
            "vertices": [(-1.0, -0.6), (1.0, 0.0), (-1.0, 0.6)],
            "scale_m": 50.0,
        }
        self.default_station_shape = {
            "vertices": [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)],
            "scale_m": 2000.0,
        }
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
        self.ship_by_body_id: Dict[int, Dict[str, object]] = {}
        self.closest_approach_info: Optional[Dict[str, float]] = None
        self.logout_sent = False
        self.player_ships_at_station: List[Dict[str, object]] = []

        self.load_shape_definitions()
        self.load_blueprints()

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

    def load_shape_definitions(self) -> None:
        config_dir = Path(__file__).resolve().parent.parent / "server" / "config"
        ship_config = config_dir / "ship_classes.json"
        station_config = config_dir / "stations.json"

        def parse_vertices(raw_vertices: object) -> List[Tuple[float, float]]:
            vertices: List[Tuple[float, float]] = []
            if isinstance(raw_vertices, list):
                for entry in raw_vertices:
                    if isinstance(entry, list) and len(entry) == 2:
                        try:
                            vertices.append((float(entry[0]), float(entry[1])))
                        except (TypeError, ValueError):
                            continue
            return vertices

        self.ship_shapes = {}
        try:
            data = json.loads(ship_config.read_text())
            if isinstance(data, list):
                for entry in data:
                    class_id = entry.get("id") if isinstance(entry, dict) else None
                    if not class_id or not isinstance(entry, dict):
                        continue
                    vertices = parse_vertices(entry.get("shape_vertices"))
                    scale_m = float(entry.get("shape_scale_m", self.default_ship_shape["scale_m"]))
                    if vertices:
                        self.ship_shapes[class_id] = {"vertices": vertices, "scale_m": scale_m}
        except (OSError, json.JSONDecodeError):
            pass

    def load_blueprints(self) -> None:
        bp_dir = Path(__file__).resolve().parent.parent / "server" / "config" / "blueprints"
        self.blueprints = {}
        if not bp_dir.exists():
            return
        for path in bp_dir.glob("*.json"):
            try:
                data = json.loads(path.read_text())
            except (OSError, json.JSONDecodeError):
                continue
            if not isinstance(data, dict):
                continue
            bp_id = str(data.get("id") or path.stem).upper()
            grid = data.get("grid_size", {}) or {}
            layers = data.get("layers", {}) or {}
            hit_rows = layers.get("hit", []) or []
            systems_rows = layers.get("systems", []) or []
            try:
                pixel_scale = float(data.get("pixel_scale_m", 1.0))
            except (TypeError, ValueError):
                pixel_scale = 1.0
            self.blueprints[bp_id] = {
                "id": bp_id,
                "grid_w": int(grid.get("w", len(hit_rows[0]) if hit_rows else 0)),
                "grid_h": int(grid.get("h", len(hit_rows))),
                "pixel_scale_m": pixel_scale,
                "hit": hit_rows,
                "systems": systems_rows,
            }

        self.station_shapes = {}
        try:
            data = json.loads(station_config.read_text())
            if isinstance(data, list):
                for entry in data:
                    if not isinstance(entry, dict):
                        continue
                    station_id = entry.get("id")
                    if station_id is None:
                        continue
                    vertices = parse_vertices(entry.get("shape_vertices"))
                    scale_m = float(entry.get("shape_scale_m", self.default_station_shape["scale_m"]))
                    if vertices:
                        self.station_shapes[int(station_id)] = {"vertices": vertices, "scale_m": scale_m}
        except (OSError, json.JSONDecodeError):
            pass

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
                elif action == "switch":
                    self.client.send({"type": "switch_ship", "new_ship_id": payload.get("ship_id", -1)})
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
                self.player_ships_at_station = message.get("player_ships_at_station", [])
                new_ship = self.player_info.get("active_ship_id", -1)
                if new_ship != self.active_ship_id:
                    self.active_ship_id = new_ship
                    self.send_control_state()
                self.log("Received world summary.")
            elif msg_type == "world_state":
                self.world_state = message
                self.player_info = message.get("player", {})
                self.player_ships_at_station = message.get("player_ships_at_station", [])
                self.body_map = {body["body_id"]: body for body in message.get("bodies", [])}
                self.station_map = {station["station_id"]: station for station in message.get("stations", [])}
                self.station_body_lookup = {
                    station.get("body_id"): station_id
                    for station_id, station in self.station_map.items()
                    if station.get("body_id") is not None
                }
                self.ship_by_body_id = {
                    ship.get("body_id"): ship for ship in message.get("ships", []) if ship.get("body_id") is not None
                }
                self.ship_classes = {
                    sc.get("ship_class_id"): sc for sc in message.get("ship_classes", [])
                }
                for station in message.get("stations", []):
                    station_id = station.get("station_id")
                    verts = station.get("shape_vertices")
                    if station_id is not None and verts:
                        try:
                            parsed = [(float(v[0]), float(v[1])) for v in verts]
                        except (TypeError, ValueError):
                            parsed = []
                        if parsed:
                            self.station_shapes[int(station_id)] = {"vertices": parsed, "scale_m": 1.0}
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

    def estimate_orbit_horizon(self, body: Dict[str, object]) -> float:
        r0 = math.hypot(body.get("x", 0.0), body.get("y", 0.0))
        if r0 <= 0 or self.planet_mu <= 0:
            return 0.0
        orbital_period = 2.0 * math.pi * math.sqrt(max(r0, 1.0) ** 3 / self.planet_mu)
        return min(max(orbital_period, MIN_ORBIT_DURATION), MAX_ORBIT_DURATION)

    def predict_positions(
        self, body: Dict[str, object], total_time: float
    ) -> Tuple[List[Tuple[float, float]], float]:
        if total_time <= 0.0 or self.planet_mu <= 0.0 or ORBIT_STEPS <= 0:
            return [], 0.0
        dt = total_time / ORBIT_STEPS
        points: List[Tuple[float, float]] = []
        px = body.get("x", 0.0)
        py = body.get("y", 0.0)
        pvx = body.get("vx", 0.0)
        pvy = body.get("vy", 0.0)
        for _ in range(ORBIT_STEPS):
            points.append((px, py))
            r = math.hypot(px, py)
            if r < max(1.0, self.planet_radius * 0.9) or r > 2e8:
                break
            accel = -self.planet_mu / max(r ** 3, 1.0)
            ax = accel * px
            ay = accel * py
            pvx += ax * dt
            pvy += ay * dt
            px += pvx * dt
            py += pvy * dt
        return points, dt

    def compute_orbit_path(self, body: Dict[str, object]) -> List[Tuple[float, float]]:
        total_time = self.estimate_orbit_horizon(body)
        points, _ = self.predict_positions(body, total_time)
        return points

    def compute_closest_approach(self) -> Optional[Dict[str, float]]:
        if self.selected_target_id is None or self.active_ship_id < 0:
            return None
        active_body = self.get_active_ship_body()
        target_body = self.body_map.get(self.selected_target_id)
        if not active_body or not target_body:
            return None
        horizon = min(
            self.estimate_orbit_horizon(active_body), self.estimate_orbit_horizon(target_body)
        )
        if horizon <= 0:
            return None
        ship_positions, dt = self.predict_positions(active_body, horizon)
        target_positions, _ = self.predict_positions(target_body, horizon)
        limit = min(len(ship_positions), len(target_positions))
        if limit == 0 or dt <= 0:
            return None
        best_idx = -1
        best_distance = float("inf")
        for i in range(limit):
            dx = ship_positions[i][0] - target_positions[i][0]
            dy = ship_positions[i][1] - target_positions[i][1]
            distance = math.hypot(dx, dy)
            if distance < best_distance:
                best_distance = distance
                best_idx = i
        if best_idx < 0 or best_distance > INTERCEPT_DISTANCE_THRESHOLD:
            return None
        return {
            "position_x": ship_positions[best_idx][0],
            "position_y": ship_positions[best_idx][1],
            "distance": best_distance,
            "time": best_idx * dt,
        }

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
        self.closest_approach_info = self.compute_closest_approach()

        for body in self.body_map.values():
            position = self.world_to_screen(body.get("x", 0.0), body.get("y", 0.0))
            btype = body.get("type")
            selected = body.get("body_id") == self.selected_target_id
            if btype == "Asteroid":
                self.draw_asteroid(body, position, selected)
            elif btype == "Station":
                self.draw_station_shape(body, position, selected)
            elif btype == "Ship":
                self.draw_ship_shape(body, position, active_body_id, selected)
            elif btype == "Debris":
                self.draw_debris_icon(position, selected)
            elif selected:
                pygame.draw.circle(self.screen, (255, 230, 120), (int(position.x), int(position.y)), 10, width=2)

        self.draw_intercept_marker()
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

    def compute_shape_pixel_size(self, shape: Dict[str, object]) -> float:
        scale = self.base_scale * self.zoom
        if scale <= 0:
            return 0.0
        vertices = shape.get("vertices", [])
        if not vertices:
            return shape.get("scale_m", 0.0) * scale
        max_radius = max((math.hypot(v[0], v[1]) for v in vertices), default=1.0)
        return max_radius * shape.get("scale_m", 0.0) * 2.0 * scale

    def transform_shape_to_screen(
        self, body: Dict[str, object], shape: Dict[str, object]
    ) -> List[Tuple[int, int]]:
        vertices = shape.get("vertices") or self.default_ship_shape["vertices"]
        scale_m = shape.get("scale_m", self.default_ship_shape["scale_m"])
        angle = body.get("angle", 0.0)
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        body_x = body.get("x", 0.0)
        body_y = body.get("y", 0.0)
        points: List[Tuple[int, int]] = []
        for vx, vy in vertices:
            wx = vx * scale_m
            wy = vy * scale_m
            world_x = body_x + wx * cos_a - wy * sin_a
            world_y = body_y + wx * sin_a + wy * cos_a
            projected = self.world_to_screen(world_x, world_y)
            points.append((int(projected.x), int(projected.y)))
        return points

    def draw_blueprint_tiles(
        self, body: Dict[str, object], blueprint: Dict[str, object], color: Tuple[int, int, int], selected: bool
    ) -> bool:
        tile_world = float(blueprint.get("pixel_scale_m", 1.0))
        tile_px = tile_world * self.base_scale * self.zoom
        if tile_px < TILE_DEBUG_THRESHOLD_PX:
            return False
        grid_w = int(blueprint.get("grid_w", 0))
        grid_h = int(blueprint.get("grid_h", 0))
        hit_rows = blueprint.get("hit", []) or []
        systems_rows = blueprint.get("systems", []) or []
        angle = body.get("angle", 0.0)
        cos_a = math.cos(angle)
        sin_a = math.sin(angle)
        body_x = body.get("x", 0.0)
        body_y = body.get("y", 0.0)
        half = tile_world * 0.5

        def cell_center(ix: int, iy: int) -> Tuple[float, float]:
            cx = (ix - grid_w / 2 + 0.5) * tile_world
            cy = (grid_h / 2 - iy - 0.5) * tile_world
            return cx, cy

        for iy in range(grid_h):
            system_row = systems_rows[iy] if iy < len(systems_rows) else ""
            hit_row = hit_rows[iy] if iy < len(hit_rows) else ""
            for ix in range(grid_w):
                ch = system_row[ix] if ix < len(system_row) else "."
                if ch == ".":
                    ch = hit_row[ix] if ix < len(hit_row) else "."
                if ch == ".":
                    continue
                cx, cy = cell_center(ix, iy)
                corners_local = [
                    (cx - half, cy - half),
                    (cx + half, cy - half),
                    (cx + half, cy + half),
                    (cx - half, cy + half),
                ]
                screen_points = []
                for lx, ly in corners_local:
                    wx = body_x + lx * cos_a - ly * sin_a
                    wy = body_y + lx * sin_a + ly * cos_a
                    projected = self.world_to_screen(wx, wy)
                    screen_points.append((int(projected.x), int(projected.y)))
                fill = (120, 160, 255) if ch != "X" else (140, 140, 140)
                pygame.draw.polygon(self.screen, fill, screen_points, width=0)
                pygame.draw.polygon(self.screen, color, screen_points, width=1)
                char_surface = self.small_font.render(ch, True, (0, 0, 0))
                centroid_x = sum(p[0] for p in screen_points) / 4
                centroid_y = sum(p[1] for p in screen_points) / 4
                self.screen.blit(char_surface, char_surface.get_rect(center=(centroid_x, centroid_y)))
        if selected:
            center = self.world_to_screen(body_x, body_y)
            pygame.draw.circle(
                self.screen, (255, 230, 120), (int(center.x), int(center.y)), max(4, int(tile_px)), width=1
            )
        return True

    def draw_ship_icon(
        self, position: pygame.math.Vector2, angle: float, color: Tuple[int, int, int]
    ) -> None:
        size = 8
        points = []
        for local in ((0, size), (-size / 2, -size / 2), (size / 2, -size / 2)):
            px = position.x + local[0] * math.cos(angle) - local[1] * math.sin(angle)
            py = position.y + local[0] * math.sin(angle) + local[1] * math.cos(angle)
            points.append((px, py))
        pygame.draw.polygon(self.screen, color, points, width=0)

    def draw_ship_shape(
        self, body: Dict[str, object], position: pygame.math.Vector2, active_body_id: Optional[int], selected: bool
    ) -> None:
        angle = body.get("angle", 0.0)
        ship_info = self.ship_by_body_id.get(body.get("body_id"))
        class_id = ship_info.get("ship_class_id") if ship_info else None
        blueprint_id = ship_info.get("blueprint_id") if ship_info else None
        shape = self.ship_shapes.get(class_id, self.default_ship_shape)
        color = (255, 120, 120)
        if body.get("body_id") == active_body_id:
            color = (100, 255, 160)
        elif selected:
            color = (255, 230, 80)
        if blueprint_id:
            bp = self.blueprints.get(str(blueprint_id).upper())
            if bp and self.draw_blueprint_tiles(body, bp, color, selected):
                return
        max_dim_px = self.compute_shape_pixel_size(shape)
        if max_dim_px < ICON_THRESHOLD_PX:
            self.draw_ship_icon(position, angle, color)
            return
        points = self.transform_shape_to_screen(body, shape)
        pygame.draw.polygon(self.screen, color, points, width=0)
        if selected:
            pygame.draw.polygon(self.screen, (255, 230, 120), points, width=2)

    def draw_station_shape(self, body: Dict[str, object], position: pygame.math.Vector2, selected: bool) -> None:
        station_id = self.find_station_id_from_body(body.get("body_id"))
        shape = self.station_shapes.get(station_id or -1, self.default_station_shape)
        max_dim_px = self.compute_shape_pixel_size(shape)
        color = (120, 200, 255)
        if max_dim_px < ICON_THRESHOLD_PX:
            size = 10
            rect = pygame.Rect(0, 0, size, size)
            rect.center = (int(position.x), int(position.y))
            pygame.draw.rect(self.screen, color, rect, border_radius=3)
            if selected:
                pygame.draw.rect(self.screen, (255, 230, 120), rect, width=2, border_radius=3)
            return
        points = self.transform_shape_to_screen(body, shape)
        pygame.draw.polygon(self.screen, color, points, width=0)
        pygame.draw.polygon(self.screen, (30, 60, 90), points, width=2)
        if selected:
            pygame.draw.polygon(self.screen, (255, 230, 120), points, width=2)

    def estimate_asteroid_radius(self, body: Dict[str, object]) -> float:
        mass = float(body.get("mass", body.get("remainingMass", 2000.0)))
        density = float(body.get("density", 2600.0))
        try:
            radius = ((3.0 * mass) / (4.0 * math.pi * density)) ** (1.0 / 3.0)
        except (ZeroDivisionError, ValueError):
            radius = 10.0
        return max(5.0, radius)

    def draw_asteroid(self, body: Dict[str, object], position: pygame.math.Vector2, selected: bool) -> None:
        radius_m = self.estimate_asteroid_radius(body)
        scale = self.base_scale * self.zoom
        radius_px = radius_m * scale
        color = (200, 200, 100)
        if radius_px * 2 < ICON_THRESHOLD_PX:
            pygame.draw.circle(self.screen, color, (int(position.x), int(position.y)), 3)
        else:
            pygame.draw.circle(self.screen, color, (int(position.x), int(position.y)), max(2, int(radius_px)))
            pygame.draw.circle(self.screen, (120, 120, 80), (int(position.x), int(position.y)), max(2, int(radius_px)), width=1)
        if selected:
            pygame.draw.circle(self.screen, (255, 230, 120), (int(position.x), int(position.y)), 10, width=2)

    def draw_debris_icon(self, position: pygame.math.Vector2, selected: bool) -> None:
        size = 8
        pygame.draw.line(
            self.screen,
            (180, 180, 220),
            (position.x - size, position.y - size),
            (position.x + size, position.y + size),
            2,
        )
        pygame.draw.line(
            self.screen,
            (180, 180, 220),
            (position.x - size, position.y + size),
            (position.x + size, position.y - size),
            2,
        )
        if selected:
            pygame.draw.circle(self.screen, (255, 230, 120), (int(position.x), int(position.y)), 10, width=2)

    def draw_intercept_marker(self) -> None:
        if not self.closest_approach_info:
            return
        position = self.world_to_screen(
            self.closest_approach_info["position_x"], self.closest_approach_info["position_y"]
        )
        pygame.draw.circle(self.screen, (200, 120, 255), (int(position.x), int(position.y)), 8, width=2)
        pygame.draw.circle(self.screen, (80, 30, 120), (int(position.x), int(position.y)), 4, width=1)

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
        stored_title = self.small_font.render("Stored ships:", True, (255, 255, 255))
        self.screen.blit(stored_title, (panel_rect.left + 12, y))
        y += 22
        stored_entries = self.player_ships_at_station if self.player_info.get("docked_station_id") == station_id else []
        if not stored_entries:
            empty_text = self.small_font.render("None", True, (180, 180, 180))
            self.screen.blit(empty_text, (panel_rect.left + 12, y))
            y += 20
        else:
            for entry in stored_entries:
                ship_id = entry.get("id")
                label = f"#{ship_id} ({entry.get('class_id', 'SHIP')})"
                rect = pygame.Rect(panel_rect.left + 12, y, panel_rect.width - 24, 26)
                pygame.draw.rect(self.screen, (90, 170, 140), rect, border_radius=4)
                text = self.small_font.render(label, True, (0, 0, 0))
                self.screen.blit(text, text.get_rect(center=rect.center))
                if ship_id is not None:
                    self.station_buttons.append(("switch", rect, {"ship_id": ship_id}))
                y += 30

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
            if self.closest_approach_info:
                info_lines.append(
                    f"Closest approach: {self.format_distance(self.closest_approach_info['distance'])} in "
                    f"{self.closest_approach_info['time']:.0f} s"
                )
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
