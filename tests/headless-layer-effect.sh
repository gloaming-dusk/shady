#!/usr/bin/env bash
# Compositor backdrop effects on layer surfaces (shady.layer_effect): the
# scene behind a layer is sampled at the right place and orientation, a
# custom shader covers exactly the layer, the built-in blur averages the
# backdrop, u_time animates, a broken shader is refused, and removing the
# effect restores plain drawing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "layer-effect: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "layer-effect: needs grim" >&2; exit 2; }

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
    export SHADY_LUA_INIT="$ROOT/tests/headless-layer-effect-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$ROOT/tests/layer-effect/shell.lua"
    export SHADY_SHELL_RENDERER=shm
    export LAYER_EFFECT_STATUS="$status"
    export LAYER_EFFECT_DIR="$ROOT/tests/layer-effect"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" -c "$ROOT/tests/headless-layer-effect-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "layer-effect: $1" >&2; cat "$log" >&2; exit 1; }
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
near() { # near <x> <y> <r g b> <what>: within 12 per channel
    local got
    got="$(pixel "$1" "$2")"
    read -r r g b <<<"$got"
    read -r er eg eb <<<"$3"
    (( ${r#-} >= 0 && (r - er) * (r - er) <= 144 && (g - eg) * (g - eg) <= 144 && (b - eb) * (b - eb) <= 144 )) ||
        fail "$4: pixel ($1,$2) is '$got', expected about '$3'"
}

# The fx-bar draws #00000010 on top, so effect colours arrive at ~94%.
wait_mark IDENTITY
near 10 8 "239 239 239" "identity: white stripe"
near 30 8 "0 0 0" "identity: black stripe"
near 10 32 "0 0 239" "identity: blue row (orientation)"
echo NEXT1 >>"$status"

wait_mark HALVES
near 600 8 "239 0 239" "custom shader: top half"
near 600 32 "0 239 0" "custom shader: bottom half"
below="$(pixel 600 60)"
[[ "$below" != "239 0 239" && "$below" != "0 239 0" ]] || fail "effect drawn outside the layer ($below)"
echo NEXT2 >>"$status"

wait_mark BLUR
read -r w _ _ <<<"$(pixel 10 8)"
read -r k _ _ <<<"$(pixel 30 8)"
(( w < 230 && k > 15 && (w - k) * (w - k) < 100 * 100 )) || fail "blur did not mix the stripes ($w vs $k)"
echo NEXT3 >>"$status"

wait_mark TIME
first="$(pixel 600 20)"
sleep 0.4
second="$(pixel 600 20)"
[[ "$first" != "$second" ]] || fail "u_time effect did not animate ($first)"
echo NEXT4 >>"$status"

wait_mark REFRACT
wait_mark REMOVED
grep -q "^BROKEN_REJECTED$" "$status" || fail "a broken shader was accepted"
near 10 8 "239 239 239" "removed: plain drawing"
echo DONE >>"$status"

wait "$comp" || fail "compositor exited with an error"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "layer-effect: PASS"
