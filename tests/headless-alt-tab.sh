#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "alt-tab: missing compositor or probe; run ./daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
runtime="$tmp/runtime"
shot="$tmp/alt-tab.png"
log="$tmp/compositor.log"
mkdir -p "$runtime"
chmod 700 "$runtime"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export SHADY_LUA_INIT="$ROOT/tests/headless-alt-tab-init.lua"
export SHADY_AUTOMATION_SCREENSHOT="$shot"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 15s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-automation-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$log"

if [[ $rc -eq 124 ]]; then
  echo "alt-tab: FAIL: timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  echo "alt-tab: FAIL: compositor exited with status $rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "alt-tab: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
if ! grep -q 'alt-tab-test: PASS' "$log"; then
  echo "alt-tab: FAIL: focus cycle did not pass" >&2
  exit 1
fi
if [[ ! -s "$shot" ]]; then
  echo "alt-tab: FAIL: screenshot missing" >&2
  exit 1
fi

if [[ -n "${SHADY_ALT_TAB_KEEP_SCREENSHOT:-}" ]]; then
  cp "$shot" "$SHADY_ALT_TAB_KEEP_SCREENSHOT"
fi

echo "alt-tab: PASS"
