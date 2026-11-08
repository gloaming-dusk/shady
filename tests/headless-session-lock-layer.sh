#!/usr/bin/env bash
# A layer surface mapped over a locked session gets neither the keyboard nor
# the pointer, and does not cover the lock.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-session-lock-probe"
if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "session-lock-layer: missing compositor or probe; run ./tools/daily-driver.sh build first" >&2
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
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export SHADY_LUA_INIT="$ROOT/tests/headless-session-lock-layer-init.lua"
export SHADY_SESSION_LOCK_PROBE="$ROOT/$PROBE"
export SHADY_SESSION_LOCK_STATUS="$status"
export SHADY_SESSION_LOCK_HOLD_MS=1000
export SHADY_SESSION_LOCK_LAYER=1
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 12s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-automation-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"

if [[ $rc -ne 0 ]]; then
  cat "$log"
  echo "session-lock-layer: FAIL: compositor exited with status $rc" >&2
  exit 1
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log"
  echo "session-lock-layer: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
if grep -q '=FAIL$' "$status"; then
  echo "session-lock-layer: FAIL: the layer surface took input from the lock" >&2
  exit 1
fi
for marker in LOCKED SURFACE_CONFIGURED LAYER_MAPPED KEY_TO_LOCK POINTER_ON_LOCK UNLOCKED; do
  if ! grep -q "^$marker=PASS$" "$status"; then
    echo "session-lock-layer: FAIL: missing $marker marker" >&2
    exit 1
  fi
done

echo "session-lock-layer: PASS"
