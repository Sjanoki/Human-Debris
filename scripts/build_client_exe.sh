#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT/client_python"

if ! python -m PyInstaller --version >/dev/null 2>&1; then
  echo "PyInstaller not found. Installing..."
  python -m pip install pyinstaller
fi

echo "Building standalone GUI client..."
python -m PyInstaller --onefile client_gui.py

echo "Build complete. Executable located at $(pwd)/dist/client_gui.exe"
