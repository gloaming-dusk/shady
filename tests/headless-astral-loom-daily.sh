#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
LOCK_PROBE="$BUILD_DIR/headless-session-lock-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-astral-loom.so"
HELPER="$ROOT/tests/headless-astral-loom-daily-output.sh"

for file in "$COMPOSITOR" "$PROBE" "$LOCK_PROBE" "$PLUGIN" "$HELPER"; do
  if [[ ! -e "$file" ]]; then
    echo "astral-loom-daily: missing $file" >&2
    exit 2
  fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
lock_status="$tmp/lock-status"
output_status="$tmp/output-status"
log="$tmp/log"
: >"$status"
: >"$lock_status"
: >"$output_status"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_ASTRAL_BUILD_DIR="$ROOT/$BUILD_DIR"
export SHADY_LUA_INIT="$ROOT/tests/headless-astral-loom-daily-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_SESSION_LOCK_PROBE="$ROOT/$LOCK_PROBE"
export SHADY_SESSION_LOCK_STATUS="$lock_status"
export SHADY_SESSION_LOCK_HOLD_MS=500
WLR_RANDR_BIN="$(command -v wlr-randr || true)"
if [[ -z "$WLR_RANDR_BIN" ]]; then
  echo "astral-loom-daily: wlr-randr is required" >&2
  exit 2
fi
export SHADY_WLR_RANDR="$WLR_RANDR_BIN"
export SHADY_ASTRAL_OUTPUT_HELPER="$HELPER"
export SHADY_ASTRAL_OUTPUT_STATUS="$output_status"
export SHADY_ASTRAL_DAILY_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 12s "$COMPOSITOR" -c "$ROOT/examples/rice/astral-loom/config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
cat "$lock_status"
cat "$output_status"

if [[ $rc -eq 124 ]]; then
  tail -n 100 "$log" >&2
  echo "astral-loom-daily: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  tail -n 100 "$log" >&2
  echo "astral-loom-daily: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  tail -n 100 "$log" >&2
  echo "astral-loom-daily: FAIL sanitizer diagnostic" >&2
  exit 1
fi
for marker in BASELINE MAXIMIZE FULLSCREEN RELOAD LOCK_BLOCK LOCK_RECOVERY OUTPUT_RECOVERY; do
  grep -q "^$marker=PASS$" "$status"
done
grep -q '^LOCKED=PASS$' "$lock_status"
grep -q '^UNLOCKED=PASS$' "$lock_status"
grep -q '^OUTPUT_DISABLE=PASS' "$output_status"
grep -q 'session-lock: all locked frames presented; sending locked event' "$log"
grep -q 'plugin: reloaded astral-loom' "$log"
grep -q 'recovering window onto active output' "$log"
grep -q 'astral-loom-daily: PASS' "$log"

echo "astral-loom-daily: PASS"
