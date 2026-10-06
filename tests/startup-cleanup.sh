#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
if [[ ! -x "$COMPOSITOR" ]]; then
  echo "startup-cleanup: $COMPOSITOR is missing; run ./tools/daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
log="$tmp/compositor.log"

export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
unset XDG_RUNTIME_DIR

set +e
"$COMPOSITOR" --safe -c "$ROOT/tests/daily-driver-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$log"

if [[ $rc -eq 0 ]]; then
  echo "startup-cleanup: FAIL: compositor unexpectedly succeeded without XDG_RUNTIME_DIR" >&2
  exit 1
fi
if grep -Eq 'Assertion|AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "startup-cleanup: FAIL: cleanup triggered an assertion or sanitizer diagnostic" >&2
  exit 1
fi
if ! grep -q 'failed to allocate Wayland socket in XDG_RUNTIME_DIR' "$log"; then
  echo "startup-cleanup: FAIL: expected socket allocation failure was not observed" >&2
  exit 1
fi

echo "startup-cleanup: PASS"
