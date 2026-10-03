#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-astral-loom.so"

for file in "$COMPOSITOR" "$PROBE" "$PLUGIN"; do
  if [[ ! -e "$file" ]]; then
    echo "astral-loom-stress: missing $file" >&2
    exit 2
  fi
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
export SHADY_LUA_INIT="$ROOT/tests/headless-astral-loom-stress-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_ASTRAL_STRESS_STATUS="$status"
export SHADY_ASTRAL_STRESS_ROUNDS="${SHADY_ASTRAL_STRESS_ROUNDS:-10}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 20s "$COMPOSITOR" -c "$ROOT/examples/rice/astral-loom/config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"

if [[ $rc -eq 124 ]]; then
  tail -n 120 "$log" >&2
  echo "astral-loom-stress: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  tail -n 120 "$log" >&2
  echo "astral-loom-stress: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  tail -n 120 "$log" >&2
  echo "astral-loom-stress: FAIL sanitizer diagnostic" >&2
  exit 1
fi
if grep -Eq '\[ERROR\].*(automation timer|event window|plugin)' "$log"; then
  tail -n 120 "$log" >&2
  echo "astral-loom-stress: FAIL runtime error" >&2
  exit 1
fi

for marker in ROUNDS WINDOW_CHURN STALE_HANDLES FOCUS_CHURN WORKSPACE_CHURN FPS_CAPTURE_RELEASE ASTRAL_STATE_CHURN; do
  if ! grep -q "^$marker=PASS" "$status"; then
    tail -n 120 "$log" >&2
    echo "astral-loom-stress: FAIL missing $marker" >&2
    exit 1
  fi
done
grep -q 'astral-loom-stress: PASS' "$log"

echo "astral-loom-stress: PASS"
