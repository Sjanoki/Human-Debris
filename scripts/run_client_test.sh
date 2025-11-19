#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT/client_python"

if ! python -c "import pygame" >/dev/null 2>&1; then
  echo "pygame not found. Installing..."
  python -m pip install pygame
fi

echo "Starting GUI client..."
python client_gui.py
