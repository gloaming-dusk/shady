#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

if [[ ! -d build ]]; then
  echo "Neon Transit: build/ does not exist. Run ./build.sh first." >&2
  exit 1
fi

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/examples/rice/neon-transit/init.lua"

if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-foot}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/run-native.sh" \
  -c "$ROOT/examples/rice/neon-transit/config.lua" \
  "$@"
