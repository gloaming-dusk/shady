#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/examples/rice/astral-loom/init.lua"
export SHADY_ASTRAL_BUILD_DIR="${SHADY_ASTRAL_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$SHADY_ASTRAL_BUILD_DIR/shady" || ! -f "$SHADY_ASTRAL_BUILD_DIR/libshady-plugin-astral-loom.so" ]]; then
    echo "Astral Loom: run ./build.sh inside nix develop first." >&2
    exit 1
fi
if [[ $# -eq 0 ]]; then
    default_startup="'$SHADY_ASTRAL_BUILD_DIR/shady-shell' & ${TERMINAL:-foot} & ${TERMINAL:-foot} & ${TERMINAL:-foot} & wait"
    set -- -s "${SHADY_STARTUP:-$default_startup}"
fi
if [[ "${SHADY_ASTRAL_NATIVE:-0}" == 1 ]]; then
    # The shared native launcher uses the repository's default build directory.
    exec "$ROOT/run-native.sh" -c "$ROOT/examples/rice/astral-loom/config.lua" "$@"
fi
export WLR_BACKENDS="${WLR_BACKENDS:-wayland}"
exec "$SHADY_ASTRAL_BUILD_DIR/shady" -c "$ROOT/examples/rice/astral-loom/config.lua" "$@"
