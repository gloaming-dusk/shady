#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
GOOD="$BUILD_DIR/libshady-plugin-counter.so"
BAD="$BUILD_DIR/libshady-plugin-counter-bad.so"
for file in "$COMPOSITOR" "$GOOD" "$BAD"; do
  if [[ ! -e "$file" ]]; then
    echo "spatial-plugin-rollback: missing $file" >&2
    exit 2
  fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
plugin="$tmp/counter.so"
log="$tmp/log"
cp "$GOOD" "$plugin"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_TEST_PLUGIN_PATH="$plugin"
export SHADY_TEST_BAD_PLUGIN_PATH="$ROOT/$BAD"
export SHADY_LUA_INIT="$ROOT/tests/plugin-rollback-init.lua"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 8s "$COMPOSITOR" -c "$ROOT/tests/headless-spatial-plugin-rollback-config.lua" >"$log" 2>&1
rc=$?
set -e

if [[ $rc -eq 124 ]]; then
  cat "$log" >&2
  echo "spatial-plugin-rollback: FAIL timed out" >&2
  exit 1
fi
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "spatial-plugin-rollback: FAIL status=$rc" >&2
  exit "$rc"
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "spatial-plugin-rollback: FAIL sanitizer diagnostic" >&2
  exit 1
fi
grep -q 'plugin: reload failed, rolling back counter-plugin' "$log"
grep -q 'plugin-rollback-test: PASS' "$log"
grep -q 'counter starts=2' "$log"
echo "spatial-plugin-rollback: PASS"
