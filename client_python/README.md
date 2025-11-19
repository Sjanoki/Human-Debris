# Python Clients

This folder hosts both the legacy REPL client (`client.py`) and the pygame GUI (`client_gui.py`). Each talks to the TCP server on `localhost:7777` using the shared JSON line protocol.

## GUI client

Install the dependency and launch the graphical client:

```bash
pip install -r requirements.txt
python client_gui.py
```

Controls: `W/Up` (thrust), `A/Left` & `D/Right` (rotate), `Space` (mine), `E` (dock/undock), `B` (buy SCOUT while docked). The GUI subscribes to `world_state` updates for live rendering and automatically mirrors your active ship.

## CLI client

The lightweight REPL remains available for scripted testing:

```bash
python client.py
```

Type `help` inside the client to see the supported commands. All original gameplay verbs continue to function.
