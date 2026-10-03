#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-astral-loom.so"
for file in "$COMPOSITOR" "$PROBE" "$PLUGIN"; do
  [[ -e "$file" ]] || { echo "astral-input-focus-stress: missing $file" >&2; exit 2; }
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_ASTRAL_BUILD_DIR="$ROOT/$BUILD_DIR"
export SHADY_LUA_INIT="$ROOT/tests/headless-astral-input-focus-stress-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_ASTRAL_INPUT_STATUS="$status"
export SHADY_ASTRAL_INPUT_ROUNDS="${SHADY_ASTRAL_INPUT_ROUNDS:-8}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 20s "$COMPOSITOR" -c "$ROOT/examples/rice/astral-loom/config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -eq 124 ]]; then
  tail -n 140 "$log" >&2
  echo "astral-input-focus-stress: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  tail -n 140 "$log" >&2
  echo "astral-input-focus-stress: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  tail -n 140 "$log" >&2
  echo "astral-input-focus-stress: FAIL sanitizer diagnostic" >&2
  exit 1
fi
if grep -Eq '\[ERROR\].*(automation timer|event window|plugin)' "$log"; then
  tail -n 140 "$log" >&2
  echo "astral-input-focus-stress: FAIL runtime error" >&2
  exit 1
fi
for marker in ROUNDS WINDOWS TAB_CYCLES WORKSPACE_FOCUS FPS_MODE_INPUT CAPTURE_RELEASE DESTROY_REFOCUS; do
  grep -q "^$marker=PASS" "$status" || {
    tail -n 140 "$log" >&2
    echo "astral-input-focus-stress: FAIL missing $marker" >&2
    exit 1
  }
done
grep -q 'astral-input-focus-stress: PASS' "$log"

echo "astral-input-focus-stress: PASS"
