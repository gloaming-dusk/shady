#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "spatial-daily-smoke: missing binaries in $BUILD_DIR" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_LUA_INIT="$ROOT/tests/headless-spatial-daily-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_SPATIAL_DAILY_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 8s "$COMPOSITOR" -c "$ROOT/tests/headless-spatial-daily-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "spatial-daily-smoke: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "spatial-daily-smoke: FAIL sanitizer diagnostic" >&2
  exit 1
fi
for marker in LIFECYCLE FOCUS MAXIMIZE FULLSCREEN CLOSE; do
  grep -q "^$marker=PASS$" "$status"
done
grep -q 'spatial-daily-smoke: PASS' "$log"
echo "spatial-daily-smoke: PASS"
