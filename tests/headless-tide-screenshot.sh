#!/usr/bin/env bash
# Tide's screenshot picker takes what was chosen, and only that, and says so.
# Three runs: every way of taking a picture (seven, of the right sizes, each
# reported); a directory it cannot write to (reported as a failure); and an
# output at 1.75, where a region is in logical pixels and the picture in real
# ones. Needs grim and wtype (in the dev shell) and dbus-run-session, so the
# shell has its own notification server. SHADY_TEST_KEEP_SHOTS=<dir> keeps the
# pictures and what the screen showed after each run.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell headless-automation-probe; do
  [[ -x "$BUILD_DIR/$bin" ]] || { echo "tide-screenshot: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
log=
fail() {
  grep -E 'shot-test|lua:|SHADY LUA|AddressSanitizer|runtime error' "$log" | tail -n 30 >&2
  echo "tide-screenshot: FAIL: $1" >&2
  exit 1
}

# run <mode> <pictures dir> [VAR=value...]; the log is left in $log.
run() {
  local mode="$1" pictures="$2"
  shift 2
  log="$tmp/$mode.log"
  mkdir -m 700 "$tmp/$mode"
  mkdir -p "$tmp/$mode-home"
  set +e
  (
    export XDG_RUNTIME_DIR="$tmp/$mode" HOME="$tmp/$mode-home" XDG_PICTURES_DIR="$pictures"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
    export SHADY_ROOT="$ROOT" SHADY_LUA_INIT="$ROOT/tests/headless-tide-screenshot-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
    export SHOT_TEST_MODE="$mode" SHOT_TEST_SCREEN="${SHADY_TEST_KEEP_SHOTS:+$SHADY_TEST_KEEP_SHOTS/screen-$mode.png}"
    export TIDE_IDLE_LOCK=0 TIDE_IDLE_SCREEN_OFF=0
    source "$ROOT/rices/tide/theme.sh"
    export SHADY_SHELL_CONFIG="$ROOT/rices/tide/shell.lua"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    [[ $# -gt 0 ]] && export "$@"
    dbus-run-session -- timeout 70s "$BUILD_DIR/shady" -c "$ROOT/rices/tide/config.lua"
  ) >"$log" 2>&1
  local rc=$?
  set -e
  [[ $rc -eq 0 ]] || fail "$mode: status $rc"
  grep -q 'shot-test: DONE' "$log" || fail "$mode: it did not finish"
  ! grep -Eq 'AddressSanitizer|runtime error:' "$log" || fail "$mode: sanitizer error"
}

size() { # width x height of a PNG, from its header
  local bytes
  read -r -a bytes < <(od -An -tu1 -j16 -N8 "$1")
  echo "$(( bytes[2] * 256 + bytes[3] ))x$(( bytes[6] * 256 + bytes[7] ))"
}
shopt -s nullglob

# ---- every way of taking a picture
run all "$tmp/all-pictures"
window="$(grep -oE 'shot-test: WINDOW -?[0-9]+,-?[0-9]+ [0-9]+x[0-9]+' "$log" | sed 's/.*WINDOW //' || true)"
[[ -n "$window" ]] || fail "no window was measured"
# The window clicked, not the focused one, which is the other.
grep -qF "screenshot: -g '$window'" "$log" || fail "the click did not take the window under it ($window)"
window="${window#* }"
shots=("$tmp/all-pictures/Screenshots/"*.png)
[[ ${#shots[@]} -eq 7 ]] || fail "${#shots[@]} pictures, not 7"
# The user is told: without this it looks like the picker just closing.
[[ "$(grep -c 'lua: screenshot saved: ' "$log")" == 7 ]] || fail "not every picture was reported"
! grep -q 'lua: screenshot failed' "$log" || fail "a picture was reported as failed"
[[ "$(size "${shots[0]}")" == 400x300 ]] || fail "the region is $(size "${shots[0]}"), not 400x300"
[[ "$(size "${shots[1]}")" == "$window" ]] || fail "the window is $(size "${shots[1]}"), not $window"
[[ "$(size "${shots[2]}")" == 1280x720 ]] || fail "the screen is $(size "${shots[2]}"), not 1280x720"
[[ "$(size "${shots[3]}")" == 200x100 ]] || fail "the second region is $(size "${shots[3]}"), not 200x100"
grep -qF "screenshot: -g '850,350 200x100'" "$log" || fail "dragging inside did not move the region"
[[ "$(size "${shots[5]}")" == 1280x720 ]] || fail "Ctrl+Print took $(size "${shots[5]}"), not 1280x720"
[[ "$(size "${shots[6]}")" == "$window" ]] || fail "Alt+Print took $(size "${shots[6]}"), not $window"
[[ -n "${SHADY_TEST_KEEP_SHOTS:-}" ]] && cp "${shots[@]}" "$SHADY_TEST_KEEP_SHOTS/"

# ---- nowhere to put it
run failure /proc/shady-no-such-directory
grep -q 'lua: screenshot failed: ' "$log" || fail "a picture that could not be saved was not reported"
! grep -q 'lua: screenshot saved: ' "$log" || fail "a picture that could not be saved was reported as saved"

# ---- a fractional scale
run scale "$tmp/scale-pictures" SHADY_OUTPUT_SCALE=1.75
shots=("$tmp/scale-pictures/Screenshots/"*.png)
[[ ${#shots[@]} -eq 1 ]] || fail "at 1.75: ${#shots[@]} pictures, not 1"
[[ "$(size "${shots[0]}")" == 700x525 ]] || fail "at 1.75 the region is $(size "${shots[0]}"), not 700x525"
[[ -n "${SHADY_TEST_KEEP_SHOTS:-}" ]] && cp "${shots[0]}" "$SHADY_TEST_KEEP_SHOTS/scale.png"
echo "tide-screenshot: PASS"
