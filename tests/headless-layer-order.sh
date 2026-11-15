#!/usr/bin/env bash
# Layer-shell layers stack in their order whatever order their surfaces map
# in: outside spatial mode, a top layer surface that maps after an overlay
# one, over the same corner, is drawn beneath it (and so is not what a
# click there reaches: hit tests walk the same scene tree).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
for bin in shady headless-automation-probe; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "layer-order: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "layer-order: needs grim" >&2; exit 2; }

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
    export SHADY_LUA_INIT="$ROOT/tests/headless-layer-order-init.lua"
    export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
    export LAYER_ORDER_STATUS="$status"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "layer-order: $1" >&2; cat "$log" >&2; exit 1; }
for _ in $(seq 1 300); do
    grep -q "^MAPPED$" "$status" && break
    grep -q "^TIMEOUT$" "$status" && fail "timed out"
    kill -0 "$comp" 2>/dev/null || fail "compositor exited"
    sleep 0.02
done
grep -q "^MAPPED$" "$status" || fail "the probes did not map"
pixel() {
    XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 \
        grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tu1 | xargs
}
got="$(pixel 1200 650)"
[[ "$got" == "255 0 0" ]] || fail "the corner shows '$got': the top layer surface covers the overlay one"
echo "layer-order: PASS"
