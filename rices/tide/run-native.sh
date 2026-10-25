#!/usr/bin/env bash
# Tide on a real VT (Ctrl+Alt+F3, log in, run this), from a build tree.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/rices/tide/init.lua"
source "$ROOT/rices/tide/theme.sh"
export SHADY_SHELL_CONFIG="${SHADY_SHELL_CONFIG:-$ROOT/rices/tide/shell.lua}"

if [[ $# -eq 0 ]]; then
  set -- -s "${SHADY_STARTUP:-'$ROOT/build/shady-shell'}"
fi

exec "$ROOT/tools/run-native.sh" -c "$ROOT/rices/tide/config.lua" "$@"
