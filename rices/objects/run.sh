#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ ! -x build/shady-shell ]]; then
  echo "Objects: build/shady-shell is missing. Run ./tools/build.sh first." >&2
  exit 1
fi

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/rices/objects/init.lua"
export WLR_BACKENDS="${WLR_BACKENDS:-wayland}"
export SHADY_SHELL_CONFIG="${SHADY_SHELL_CONFIG:-$ROOT/rices/objects/shell.lua}"

if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-foot}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/build/shady" \
  -c "$ROOT/rices/objects/config.lua" \
  "$@"
