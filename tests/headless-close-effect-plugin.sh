#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_CLOSE_TEST_BUILD_DIR:-build}"

COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-close-slide-fade.so"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" || ! -f "$PLUGIN" ]]; then
  echo "close-effect-plugin: missing build artifacts in $BUILD_DIR" >&2
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
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-close-effect-plugin-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_CLOSE_PLUGIN="$ROOT/$PLUGIN"
export SHADY_CLOSE_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 8s "$COMPOSITOR" -c "$ROOT/tests/headless-close-effect-plugin-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "close-effect-plugin: FAIL status=$rc" >&2
  exit "$rc"
fi

grep -q '^CLOSE_API=PASS$' "$status"
grep -q '^MID_ALIVE=PASS$' "$status"
grep -q '^CLOSED=PASS$' "$status"
grep -q 'close-slide-fade: active' "$log"
if grep -q 'verification failed' "$log"; then
  cat "$log" >&2
  echo "close-effect-plugin: FAIL API verification" >&2
  exit 1
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "close-effect-plugin: FAIL sanitizer diagnostic" >&2
  exit 1
fi

echo "close-effect-plugin: PASS"
