#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

SERVER_EXE="build/server/Debug/orbital_server.exe"
SERVER_BIN="build/server/orbital_server"

if [[ -x "$SERVER_EXE" ]]; then
  echo "Running $SERVER_EXE"
  "$SERVER_EXE"
elif [[ -x "$SERVER_BIN" ]]; then
  echo "Running $SERVER_BIN"
  "$SERVER_BIN"
else
  echo "Server binary not found. Please run ./scripts/build.sh first." >&2
  exit 1
fi
