#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

if [[ ! -f build/libshady-plugin-afterglow.so ]]; then
  echo "Afterglow: build/libshady-plugin-afterglow.so is missing. Run ./build.sh first." >&2
  exit 1
fi

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/examples/rice/afterglow/init.lua"
export WLR_BACKENDS="${WLR_BACKENDS:-wayland}"
source "$ROOT/examples/rice/afterglow/theme.sh"
export SHADY_SHELL_CONFIG="${SHADY_SHELL_CONFIG:-$ROOT/examples/rice/afterglow/shell.lua}"

if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-foot}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/build/shady" \
  -c "$ROOT/examples/rice/afterglow/config.lua" \
  "$@"
