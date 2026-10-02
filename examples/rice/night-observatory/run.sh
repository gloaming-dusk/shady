#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

if [[ ! -d build ]]; then
  echo "Night Observatory: build/ does not exist. Run ./build.sh first." >&2
  exit 1
fi

ninja -C build libshady-plugin-orbit-layout.so

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/examples/rice/night-observatory/init.lua"
export WLR_BACKENDS="${WLR_BACKENDS:-wayland}"

# Start the standalone shell plus one terminal by default. SHADY_STARTUP can
# replace the entire startup command, or explicit Shady arguments can take over.
if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-foot}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/build/shady" \
  -c "$ROOT/examples/rice/night-observatory/config.lua" \
  "$@"
