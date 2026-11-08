#!/usr/bin/env bash
# shady-shell --app: an ordinary window, ending with its last window. Needs
# wtype (in the dev shell).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell; do
  [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-window: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
log="$tmp/log"
set +e
(
  export XDG_RUNTIME_DIR="$tmp/runtime" HOME="$tmp"
  unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET SHADY_RICE
  export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
  export SHADY_LUA_INIT="$ROOT/tests/headless-shell-window-init.lua"
  export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell" SHADY_WINDOW_APP="$ROOT/tests/shell-window-app.lua"
  export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
  export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
  timeout 25s "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-shell-window-config.lua"
) >"$log" 2>&1
rc=$?
set -e
fail() {
  grep -E 'window-test|shady-shell:|SHADY LUA|AddressSanitizer|runtime error' "$log" | tail -n 30 >&2
  echo "shell-window: FAIL: $1" >&2
  exit 1
}
[[ $rc -eq 0 ]] || fail "status $rc"
! grep -Eq 'AddressSanitizer|runtime error:|window-test: FAIL' "$log" || fail "see above"
grep -q 'window-test: finished' "$log" || fail "it did not finish"
grep -Eq 'window-test: clicked at width [0-9]+' "$log" || fail "the click did not arrive"
grep -q 'window-test: clicked at width 400$' "$log" && fail "the window kept its own size"
grep -q 'window-test: typed x' "$log" || fail "typing did not arrive"
grep -q 'window-test: the application ended' "$log" || fail "the application outlived its window"
echo "shell-window: PASS"
