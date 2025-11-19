# Human Debris – Orbital Server MVP

This repository contains:

* A headless C++20 simulation server that models ships, asteroids, stations, and a central planet.
* A Python command-line client (`client.py`) for debugging gameplay commands.
* A pygame-powered GUI client (`client_gui.py`) with real-time keyboard controls and live world-state visualisation.

All TCP communication uses the JSON line protocol documented in the original MVP requirements so the CLI and GUI clients remain fully compatible.

## Quick start

```bash
# Build the server (prompts for Debug/Release when omitted)
./scripts/build.sh

# Launch the simulation (auto-detects build output)
./scripts/run_server_test.sh

# In a second terminal, start the GUI client
./scripts/run_client_test.sh
```

Windows users can run the equivalent batch files (`build.bat`, `run_server_test.bat`, `run_client_test.bat`). All scripts keep their terminals open so logs remain visible after the program exits.

## Building manually (optional)

The helper scripts wrap these commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

Server binaries are emitted under `build/server/<config>/orbital_server[.exe]` depending on the generator and platform.

## Clients

### GUI client (`client_gui.py`)

* Navigate to `client_python` and install the requirements (`pip install -r requirements.txt`) if needed.
* Run `python client_gui.py` or use `./scripts/run_client_test.sh`.
* Controls:
  * `W`/`Up` – thrust forward continuously while held.
  * `A`/`Left` and `D`/`Right` – rotate the ship.
  * `Space` – toggle the mining laser (locks to the nearest asteroid in range).
  * `E` – dock/undock when close to a station.
  * `B` – buy a SCOUT-class ship while docked (placeholder economy hook).
  * `Esc` – exit the client.
* The GUI automatically subscribes to periodic `world_state` updates so it can render the live simulation and show HUD data such as credits, fuel, cargo mass, and docking status.

### CLI client (`client.py`)

The original REPL client is still available for text-based testing. Launch it via:

```bash
cd client_python
python client.py
```

All existing commands (`login`, `thrust`, `radar`, `mine`, `dock`, `sell`, `buy`, etc.) continue to work unchanged.

## Packaging the GUI for playtests

To build a standalone Windows executable of the GUI client (via PyInstaller):

```bash
./scripts/build_client_exe.sh
# or on Windows
build_client_exe.bat
```

The resulting binary lives under `client_python/dist/client_gui.exe` and bundles pygame plus the networking glue for easy distribution to playtesters.
