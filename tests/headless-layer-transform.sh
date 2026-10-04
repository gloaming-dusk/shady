#!/usr/bin/env bash
# 3D placement of layer surfaces (shady.layer_transform): a tilted bar is
# drawn foreshortened and takes clicks where it is drawn and nowhere else; a
# bar pushed back in depth shrinks toward the output centre and is clickable
# there; removing the transform restores plain drawing and input.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "layer-transform: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "layer-transform: needs grim" >&2; exit 2; }

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
    export LIBGL_ALWAYS_SOFTWARE=1
    export SHADY_ROOT="$ROOT"
    export SHADY_LUA_INIT="$ROOT/tests/headless-layer-transform-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$ROOT/tests/layer-transform/shell.lua"
    export SHADY_SHELL_RENDERER=shm
    export LAYER_TRANSFORM_STATUS="$status"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" -c "$ROOT/tests/headless-layer-effect-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "layer-transform: $1" >&2; cat "$log" >&2; exit 1; }
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
magenta() { [[ "$(pixel "$1" "$2")" == "255 0 255" ]]; }
clicks() { grep -c "lua: clicked" "$log" || true; }

wait_mark TILT
magenta 100 5 || fail "tilted bar missing at the hinge: $(pixel 100 5)"
magenta 100 34 && fail "tilted bar not foreshortened: (100,34) is still magenta"
echo NEXT1 >>"$status"
wait_mark CLICK_TILT
(( $(clicks) == 1 )) || fail "click on the tilted bar was not delivered ($(clicks))"
echo NEXT2 >>"$status"
wait_mark MISS_TILT
(( $(clicks) == 1 )) || fail "a click below the tilted bar reached it"
count() { grep -c "lua: $1$" "$log" || true; }
echo STACK1 >>"$status"
wait_mark CLICK_OVER
(( $(count over) == 1 && $(count bar) == 0 )) ||
    fail "the overlay popup above the tilted bar did not take the click (over $(count over), bar $(count bar))"
echo STACK2 >>"$status"
wait_mark CLICK_UNDER
(( $(count bar) == 1 )) || fail "the tilted bar below the popup did not take the click"
echo NEXT3 >>"$status"

wait_mark DEPTH
magenta 200 80 || fail "bar pushed back is not at the expected place: $(pixel 200 80)"
magenta 200 5 && fail "bar pushed back still covers the top edge"
magenta 60 80 && fail "bar pushed back did not shrink horizontally"
echo NEXT4 >>"$status"
wait_mark CLICK_DEPTH
(( $(clicks) == 2 )) || fail "click on the receded bar was not delivered ($(clicks))"
echo NEXT5 >>"$status"

# The nearly transparent part shows the green backdrop where the receded bar
# is drawn, and nothing at its untransformed place.
wait_mark EFFECT
green() { read -r r g b <<<"$(pixel "$1" "$2")"; (( g > 200 && r < 40 && b < 40 )); }
green 640 80 || fail "effect missing on the transformed bar: $(pixel 640 80)"
green 640 20 && fail "effect drawn at the untransformed place: $(pixel 640 20)"
echo NEXT6 >>"$status"

wait_mark REMOVED
magenta 100 34 || fail "removing the transform did not restore the bar"
green 640 34 || fail "effect missing on the restored bar: $(pixel 640 34)"
echo NEXT7 >>"$status"
wait_mark CLICK_PLAIN
(( $(clicks) == 3 )) || fail "plain bar did not take the click ($(clicks))"
echo DONE >>"$status"

wait "$comp" || fail "compositor exited with an error"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "layer-transform: PASS"
