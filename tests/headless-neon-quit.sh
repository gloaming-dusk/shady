#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "neon-quit: missing binaries in $BUILD_DIR" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
runtime="$tmp/runtime"
log="$tmp/compositor.log"
mkdir -p "$runtime"
chmod 700 "$runtime"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-neon-quit.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"

set +e
timeout 5s "$COMPOSITOR" -c "$ROOT/examples/rice/neon-transit/config.lua" >"$log" 2>&1
rc=$?
set -e

if [[ $rc -eq 124 ]]; then
  cat "$log" >&2
  echo "neon-quit: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "neon-quit: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'automation timer:|runtime error:' "$log"; then
  cat "$log" >&2
  echo "neon-quit: FAIL runtime error" >&2
  exit 1
fi

echo "neon-quit: PASS"
