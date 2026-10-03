#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "afterglow: missing binaries in $BUILD_DIR" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
log="$tmp/compositor.log"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-afterglow.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_AFTERGLOW_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-afterglow.so"
export SHADY_FPS_REPRESENTATION_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-fps-folded-paper.so"
export SHADY_OVERVIEW_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-spatial-overview.so"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 25s "$COMPOSITOR" -c "$ROOT/examples/rice/afterglow/config.lua" >"$log" 2>&1
rc=$?
set -e

fail() { tail -n 60 "$log" >&2; echo "afterglow: FAIL $1" >&2; exit 1; }
[[ $rc -eq 124 ]] && fail "timed out"
[[ $rc -ne 0 ]] && fail "status=$rc"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer|automation timer:' "$log" &&
  fail "sanitizer or Lua runtime error"
grep -q 'afterglow: sky rendered' "$log" || fail "sky hook never drew"
grep -q 'afterglow: drifting to blue hour' "$log" || fail "Super+T did not advance the hour"
grep -q 'afterglow: drifting to afterglow' "$log" || fail "Super+Shift+T did not go back"
grep -q 'afterglow-test: PASS' "$log" || fail "scenario did not complete"
echo "afterglow: PASS"
