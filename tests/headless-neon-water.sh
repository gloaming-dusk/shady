#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
export SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR"
export SHADY_WATER_TEST_CONFIG="$ROOT/rices/neon-transit/config.lua"
export SHADY_OVERVIEW_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-spatial-overview.so"
export SHADY_NEON_WATER=1
export SHADY_WATER_TEST_DYNAMIC_BASE=1

bash ./tests/headless-water-windows.sh
echo "neon-water: PASS"
