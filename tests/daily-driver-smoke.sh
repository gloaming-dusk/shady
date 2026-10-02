#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
if [[ ! -x "$COMPOSITOR" ]]; then
  echo "daily-driver: $COMPOSITOR is missing; run ./daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
status="$tmp/status"
log="$tmp/compositor.log"
: >"$status"

export SHADY_TEST_STATUS="$status"
export SHADY_LUA_INIT="$ROOT/tests/daily-driver-init.lua"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=pixman
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 30s "$COMPOSITOR" --safe -c "$ROOT/tests/daily-driver-config.lua" \
  -s "exec '$ROOT/tests/daily-driver-window-churn.sh'" >"$log" 2>&1
rc=$?
set -e

cat "$log"

if [[ $rc -eq 124 ]]; then
  echo "daily-driver: FAIL: compositor test timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  echo "daily-driver: FAIL: compositor exited with status $rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "daily-driver: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
if grep -q 'text input interface not implemented by compositor' "$log"; then
  echo "daily-driver: FAIL: text-input-v3 support is not advertised" >&2
  exit 1
fi
if grep -q 'compositor does not implement XDG activation' "$log"; then
  echo "daily-driver: FAIL: xdg-activation support is not advertised" >&2
  exit 1
fi
if [[ "$(grep -c 'output .* enabled:' "$log" || true)" -lt 2 ]]; then
  echo "daily-driver: FAIL: expected two headless outputs" >&2
  exit 1
fi
if grep -q '^OUTPUT_DISABLE=PASS' "$status" && \
   ! grep -q 'recovering window onto active output' "$log"; then
  echo "daily-driver: FAIL: output disable did not exercise window recovery" >&2
  cat "$status" >&2
  exit 1
fi
if ! grep -q '^WINDOW_CHURN=PASS$' "$status"; then
  echo "daily-driver: FAIL: window churn did not complete" >&2
  cat "$status" >&2
  exit 1
fi
if ! grep -q 'daily-driver-test: mapped' "$log" || \
   ! grep -q 'daily-driver-test: unmapped' "$log" || \
   ! grep -q 'daily-driver-test: destroyed' "$log"; then
  echo "daily-driver: FAIL: expected window lifecycle events were not observed" >&2
  exit 1
fi
if ! grep -q '^PASS$' "$status"; then
  echo "daily-driver: FAIL: helper did not report success" >&2
  exit 1
fi

echo
echo "daily-driver: PASS"
cat "$status"
