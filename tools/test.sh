#!/usr/bin/env bash
# Run the development build nested with tools/test-shady.lua.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHADY_LUA_INIT="$ROOT/tools/test-shady.lua" WLR_BACKENDS=wayland exec "$ROOT/build/shady" -s ghostty
