#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
GOOD="$BUILD_DIR/libshady-plugin-counter.so"
BAD="$BUILD_DIR/libshady-plugin-counter-bad.so"

for file in "$COMPOSITOR" "$GOOD" "$BAD"; do
  if [[ ! -e "$file" ]]; then
    echo "plugin-rollback: missing $file" >&2
    exit 2
  fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
runtime="$tmp/runtime"
plugin="$tmp/counter.so"
log="$tmp/compositor.log"
mkdir -p "$runtime"
chmod 700 "$runtime"
cp "$GOOD" "$plugin"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export SHADY_TEST_PLUGIN_PATH="$plugin"
export SHADY_TEST_BAD_PLUGIN_PATH="$ROOT/$BAD"
export SHADY_LUA_INIT="$ROOT/tests/plugin-rollback-init.lua"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 10s "$COMPOSITOR" --safe -c "$ROOT/tests/plugin-rollback-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$log"

if [[ $rc -eq 124 ]]; then
  echo "plugin-rollback: FAIL: compositor timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  echo "plugin-rollback: FAIL: compositor exited with status $rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "plugin-rollback: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
if ! grep -q 'plugin: reload failed, rolling back counter-plugin' "$log"; then
  echo "plugin-rollback: FAIL: rollback path was not exercised" >&2
  exit 1
fi
if ! grep -q 'plugin-rollback-test: PASS' "$log"; then
  echo "plugin-rollback: FAIL: rollback verification did not pass" >&2
  exit 1
fi
if ! grep -q 'counter starts=2' "$log"; then
  echo "plugin-rollback: FAIL: original plugin was not restarted after rollback" >&2
  exit 1
fi

echo "plugin-rollback: PASS"
