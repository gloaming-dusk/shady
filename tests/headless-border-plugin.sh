#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_BORDER_TEST_BUILD_DIR:-build}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-border-accent.so"

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" || ! -f "$PLUGIN" ]]; then
  echo "border-plugin: missing binaries/plugin in $BUILD_DIR" >&2
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
export WLR_RENDERER=pixman
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-border-plugin-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_BORDER_PLUGIN="$ROOT/$PLUGIN"
export SHADY_BORDER_STATUS="$status"

set +e
timeout 7s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-border-plugin-config.lua" >"$log" 2>&1
rc=$?
set -e

cat "$status"
if [[ $rc -ne 0 ]]; then
  cat "$log" >&2
  echo "border-plugin: FAIL status=$rc" >&2
  exit "$rc"
fi

grep -q '^OVERRIDE_KEY=PASS$' "$status"
grep -q '^RESET_KEY=PASS$' "$status"
grep -q 'border-accent: override' "$log"
grep -q 'border-accent: reset' "$log"
if grep -q 'verification failed' "$log"; then
  cat "$log" >&2
  echo "border-plugin: FAIL API verification" >&2
  exit 1
fi

echo "border-plugin: PASS"
