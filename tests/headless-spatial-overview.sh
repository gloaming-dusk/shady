#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-spatial-overview.so"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" || ! -f "$PLUGIN" ]]; then
  echo "spatial-overview: missing spatial test binaries/plugin in $BUILD_DIR" >&2
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
export SHADY_LUA_INIT="$ROOT/tests/headless-spatial-overview-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_OVERVIEW_PLUGIN="$ROOT/$PLUGIN"
export SHADY_OVERVIEW_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 10s "$COMPOSITOR" -c "$ROOT/tests/headless-spatial-overview-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "spatial-overview: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "spatial-overview: FAIL sanitizer diagnostic" >&2
  exit 1
fi

grep -q '^OVERVIEW=PASS$' "$status"
grep -q '^RESTORE=PASS$' "$status"
grep -q 'spatial-overview-test: PASS' "$log"

echo "spatial-overview: PASS"
