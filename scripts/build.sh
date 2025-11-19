#!/usr/bin/env bash
# Usage: ./scripts/build.sh [Debug|Release]
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

BUILD_TYPE="${1:-}"
if [[ -z "$BUILD_TYPE" ]]; then
  read -r -p "Build type [Debug/Release] (default Debug): " BUILD_TYPE
fi
if [[ -z "$BUILD_TYPE" ]]; then
  BUILD_TYPE="Debug"
fi
if [[ "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
  echo "Invalid build type: $BUILD_TYPE" >&2
  exit 1
fi

echo "Configuring build ($BUILD_TYPE)..."
cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "Building..."
cmake --build build --config "$BUILD_TYPE"
