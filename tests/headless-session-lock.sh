#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-session-lock-probe"
if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "session-lock: missing compositor or probe; run ./daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
runtime="$tmp/runtime"
status="$tmp/status"
log="$tmp/compositor.log"
mkdir -p "$runtime"
chmod 700 "$runtime"
: >"$status"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS="${SHADY_SESSION_LOCK_OUTPUTS:-1}"
export WLR_RENDERER=pixman
export SHADY_LUA_INIT="$ROOT/tests/headless-session-lock-init.lua"
export SHADY_SESSION_LOCK_PROBE="$ROOT/$PROBE"
export SHADY_SESSION_LOCK_STATUS="$status"
export SHADY_SESSION_LOCK_HOLD_MS=800
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 10s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-automation-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$log"
cat "$status"

if [[ $rc -eq 124 ]]; then
  echo "session-lock: FAIL: timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  echo "session-lock: FAIL: compositor exited with status $rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "session-lock: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
for marker in LOCKED SURFACE_CONFIGURED UNLOCKED; do
  if ! grep -q "^$marker=PASS$" "$status"; then
    echo "session-lock: FAIL: missing $marker marker" >&2
    exit 1
  fi
done
if ! grep -q 'session-lock: all locked frames presented; sending locked event' "$log"; then
  echo "session-lock: FAIL: locked event was not gated on output presentation" >&2
  exit 1
fi
if ! grep -q 'headless-session-lock: PASS' "$log"; then
  echo "session-lock: FAIL: Lua verification did not pass" >&2
  exit 1
fi

echo "session-lock: PASS"
