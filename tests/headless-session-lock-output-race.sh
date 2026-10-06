#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-session-lock-probe"
if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "session-lock-output-race: missing compositor or probe; run ./tools/daily-driver.sh build first" >&2
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
export WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=pixman
export SHADY_LUA_INIT="$ROOT/tests/headless-session-lock-init.lua"
export SHADY_SESSION_LOCK_PROBE="$ROOT/$PROBE"
export SHADY_SESSION_LOCK_STATUS="$status"
export SHADY_SESSION_LOCK_HOLD_MS=300
export SHADY_SESSION_LOCK_DISABLE_OUTPUT=HEADLESS-1
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 8s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-automation-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"

if [[ $rc -eq 124 ]]; then
  tail -n 120 "$log" >&2
  echo "session-lock-output-race: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  tail -n 120 "$log" >&2
  echo "session-lock-output-race: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  tail -n 120 "$log" >&2
  echo "session-lock-output-race: FAIL sanitizer diagnostic" >&2
  exit 1
fi
for marker in OUTPUT_DISABLE_RACE OUTPUT_DISABLED_BEFORE_LOCKED LOCKED SURFACE_CONFIGURED UNLOCKED; do
  grep -q "^$marker=PASS$" "$status" || {
    tail -n 120 "$log" >&2
    echo "session-lock-output-race: FAIL missing $marker" >&2
    exit 1
  }
done
if grep -q '^OUTPUT_DISABLED_BEFORE_LOCKED=FAIL$' "$status"; then
  tail -n 120 "$log" >&2
  echo "session-lock-output-race: FAIL race did not beat locked event" >&2
  exit 1
fi
grep -q 'headless-session-lock: PASS' "$log"
echo "session-lock-output-race: PASS"
