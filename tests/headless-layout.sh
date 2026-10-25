#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" ]]; then
  echo "layout: missing compositor or probe; run ./tools/daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/runtime"
chmod 700 "$tmp/runtime"
log="$tmp/compositor.log"

export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export SHADY_LUA_INIT="$ROOT/tests/headless-layout-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 15s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-layout-config.lua" >"$log" 2>&1
rc=$?
set -e

if [[ $rc -ne 0 ]] || grep -Eq 'AddressSanitizer|runtime error:' "$log" ||
    ! grep -q 'layout-test: PASS' "$log"; then
  cat "$log"
  echo "layout: FAIL (status $rc)" >&2
  exit 1
fi
echo "layout: PASS"
