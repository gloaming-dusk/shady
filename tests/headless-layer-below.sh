#!/usr/bin/env bash
# Background and bottom layer surfaces in spatial mode: a wallpaper is drawn
# in front of the built-in sky but behind the windows, and a bottom-layer
# widget takes clicks where it is drawn, also when it is turned in 3D.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell headless-automation-probe; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "layer-below: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "layer-below: needs grim" >&2; exit 2; }

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"
# A solid red sky.
{ printf 'P6\n4 2\n255\n'; for _ in $(seq 8); do printf '\377\000\000'; done; } >"$tmp/sky.ppm"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
    export LIBGL_ALWAYS_SOFTWARE=1
    export SHADY_LUA_INIT="$ROOT/tests/headless-layer-below-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$ROOT/tests/layer-below/shell.lua"
    export SHADY_SHELL_RENDERER=shm
    export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
    export LAYER_BELOW_STATUS="$status" LAYER_BELOW_SKY="$tmp/sky.ppm"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" -c "$ROOT/tests/layer-below/config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "layer-below: $1" >&2; cat "$log" >&2; exit 1; }
wait_mark() {
    for _ in $(seq 1 400); do
        grep -q "^$1$" "$status" && return 0
        grep -q "^TIMEOUT$" "$status" && fail "timed out before $1"
        kill -0 "$comp" 2>/dev/null || fail "compositor exited before $1"
        sleep 0.02
    done
    fail "timed out waiting for $1"
}
pixel() {
    XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 \
        grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tu1 | xargs
}
wall() { [[ "$(pixel "$1" "$2")" == "0 255 128" ]]; }
clicks() { grep -c "lua: widget" "$log" || true; }

wait_mark MAPPED
wall 1200 60 || fail "wallpaper hidden (by the sky?): $(pixel 1200 60)"
[[ "$(pixel 120 560)" == "255 0 255" ]] || fail "bottom-layer widget missing: $(pixel 120 560)"
center="$(pixel 640 360)"
[[ "$center" != "0 255 128" ]] || fail "the wallpaper covers the window"
echo NEXT1 >>"$status"
wait_mark CLICK_PLAIN
(( $(clicks) == 1 )) || fail "click on the widget was not delivered ($(clicks))"
echo NEXT2 >>"$status"
wait_mark CLICK_TURNED
(( $(clicks) == 2 )) || fail "click on the turned widget was not delivered ($(clicks))"
echo NEXT3 >>"$status"
wait_mark MISS_TURNED
(( $(clicks) == 2 )) || fail "a click beside the turned widget reached it"
echo DONE >>"$status"

wait "$comp" || fail "compositor exited with an error"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "layer-below: PASS"
