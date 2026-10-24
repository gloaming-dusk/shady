#!/usr/bin/env bash
# Tide in a window on your current desktop (nested), from a build tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/rices/tide/init.lua"
export WLR_BACKENDS="${WLR_BACKENDS:-wayland}"
source "$ROOT/rices/tide/theme.sh"
export SHADY_SHELL_CONFIG="${SHADY_SHELL_CONFIG:-$ROOT/rices/tide/shell.lua}"

if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-ghostty}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/build/shady" -c "$ROOT/rices/tide/config.lua" "$@"
