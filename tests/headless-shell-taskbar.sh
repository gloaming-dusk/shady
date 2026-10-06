#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
SHELL_BIN="$BUILD_DIR/shady-shell"
PROBE="$BUILD_DIR/headless-automation-probe"

if [[ ! -x "$COMPOSITOR" || ! -x "$SHELL_BIN" || ! -x "$PROBE" ]]; then
  echo "shell-taskbar: missing test binaries; run ./tools/daily-driver.sh build first" >&2
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
export SHADY_LUA_INIT="$ROOT/tests/headless-shell-taskbar-init.lua"
export SHADY_SHELL_BIN="$ROOT/$SHELL_BIN"
export SHADY_SHELL_CONFIG="$ROOT/shell/default.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_SHELL_TASKBAR_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

set +e
timeout 15s "$COMPOSITOR" --safe -c "$ROOT/tests/headless-automation-config.lua" >"$log" 2>&1
rc=$?
set -e
cat "$log"
cat "$status"

if [[ $rc -eq 124 || $rc -ne 0 ]]; then
  echo "shell-taskbar: FAIL: compositor status $rc" >&2
  exit 1
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "shell-taskbar: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi
grep -q '^ACTIVATE=PASS$' "$status"
grep -q '^CLOSE=PASS$' "$status"
grep -q 'headless-shell-taskbar: PASS' "$log"

echo "shell-taskbar: PASS"
