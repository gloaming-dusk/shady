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
source "$ROOT/examples/rice/afterglow/theme.sh"

if [[ $# -eq 0 ]]; then
  default_startup="'$ROOT/build/shady-shell' & exec ${TERMINAL:-foot}"
  set -- -s "${SHADY_STARTUP:-$default_startup}"
fi

exec "$ROOT/run-native.sh" \
  -c "$ROOT/examples/rice/afterglow/config.lua" \
  "$@"
