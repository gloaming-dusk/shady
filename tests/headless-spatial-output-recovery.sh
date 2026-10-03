#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "spatial-output-recovery: missing binaries in $BUILD_DIR" >&2
  exit 2
fi
if ! command -v wlr-randr >/dev/null 2>&1; then
  echo "spatial-output-recovery: wlr-randr is required" >&2
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
export WLR_HEADLESS_OUTPUTS=2
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_LUA_INIT="$ROOT/tests/headless-spatial-output-recovery-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_SPATIAL_OUTPUT_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 8s "$COMPOSITOR" -c "$ROOT/tests/headless-spatial-daily-config.lua" \
  -s "exec '$ROOT/tests/headless-spatial-output-recovery-helper.sh'" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -eq 124 ]]; then
  cat "$log" >&2
  echo "spatial-output-recovery: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "spatial-output-recovery: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "spatial-output-recovery: FAIL sanitizer diagnostic" >&2
  exit 1
fi
grep -q '^OUTPUT_DISABLE=PASS' "$status"
grep -q '^PASS$' "$status"
grep -q 'recovering window onto active output' "$log"
echo "spatial-output-recovery: PASS"
