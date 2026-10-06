#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PLUGIN="$BUILD_DIR/libshady-plugin-counter.so"

if [[ ! -x "$COMPOSITOR" ]]; then
  echo "plugin-reload: $COMPOSITOR is missing; run ./tools/daily-driver.sh build first" >&2
  exit 2
fi
if [[ ! -f "$PLUGIN" ]]; then
  echo "plugin-reload: $PLUGIN is missing; build the counter plugin first" >&2
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
export WLR_RENDERER=pixman
export SHADY_TEST_PLUGIN_PATH="$ROOT/$PLUGIN"
export SHADY_LUA_INIT="$ROOT/tests/plugin-reload-init.lua"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 10s "$COMPOSITOR" --safe -c "$ROOT/tests/plugin-reload-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$log"

if [[ $rc -eq 124 ]]; then
  echo "plugin-reload: FAIL: compositor timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  echo "plugin-reload: FAIL: compositor exited with status $rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "plugin-reload: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
if ! grep -q 'plugin-reload-test: PASS stale=Module<dead>' "$log"; then
  echo "plugin-reload: FAIL: stale Lua module handle regression check did not pass" >&2
  exit 1
fi

echo "plugin-reload: PASS"
