#!/usr/bin/env bash
# Afterglow's shell follows the sky: the plugin publishes the hour and a
# palette, the rice's shell.lua reads them, and an hour change (Super+T)
# cross-fades the bar along with the sky.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell shadyctl libshady-plugin-afterglow.so libshady-plugin-fps-folded-paper.so \
        libshady-plugin-spatial-overview.so; do
    [[ -e "$BUILD_DIR/$bin" ]] || { echo "afterglow-shell: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "afterglow-shell: needs grim" >&2; exit 2; }

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET SHADY_SHELL_CONFIG
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
    export SHADY_ROOT="$ROOT"
    export SHADY_LUA_INIT="$ROOT/tests/headless-afterglow-shell-init.lua"
    export SHADY_AFTERGLOW_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-afterglow.so"
    export SHADY_FPS_REPRESENTATION_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-fps-folded-paper.so"
    export SHADY_OVERVIEW_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-spatial-overview.so"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export AFTERGLOW_STATUS="$status"
    # What run.sh does for the shell.
    source "$ROOT/examples/rice/afterglow/theme.sh"
    export SHADY_SHELL_CONFIG="$ROOT/examples/rice/afterglow/shell.lua"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" -c "$ROOT/examples/rice/afterglow/config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "afterglow-shell: $1" >&2; tail -n 80 "$log" >&2; exit 1; }
ctl() { XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" "$@"; }
wait_for() {
    for _ in $(seq 1 400); do
        grep -q -- "$1" "$log" && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$1'"
        sleep 0.02
    done
    fail "timed out waiting for '$1'"
}
pixel() {
    XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 \
        grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tu1 | xargs
}
shot() {
    [[ -n "${SHADY_AFTERGLOW_SHELL_SHOTS:-}" ]] || return 0
    XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 grim "$SHADY_AFTERGLOW_SHELL_SHOTS-$1.png"
}

wait_for "afterglow: sky rendered"
wait_for "shady-shell: following compositor values"
wait_for "shady-shell: shady-shell on HEADLESS-1"
[[ "$(ctl value key=afterglow.hour)" == '"afterglow"' ]] || fail "hour not published: $(ctl value key=afterglow.hour)"
accent="$(ctl value key=afterglow.accent)"
[[ "$accent" =~ ^\"#[0-9A-F]{6}\"$ ]] || fail "accent not published: $accent"
sleep 1
# The badge is opaque, so its centre is the palette colour, not the sky.
badge_before="$(pixel 21 20)"
shot afterglow

echo NEXT_HOUR >>"$status"
wait_for "afterglow: drifting to blue hour"
# Mid-fade, the accent is between the two hours.
sleep 1.0
mid_accent="$(ctl value key=afterglow.accent)"
for _ in $(seq 1 200); do
    [[ "$(ctl value key=afterglow.hour)" == '"blue-hour"' ]] && break
    sleep 0.02
done
[[ "$(ctl value key=afterglow.hour)" == '"blue-hour"' ]] || fail "hour did not change"
sleep 2.5
final_accent="$(ctl value key=afterglow.accent)"
[[ "$mid_accent" != "$accent" && "$mid_accent" != "$final_accent" ]] ||
    fail "no cross-fade: $accent -> $mid_accent -> $final_accent"
badge_after="$(pixel 21 20)"
[[ "$badge_before" != "$badge_after" ]] || fail "the bar did not follow the sky ($badge_before)"
shot blue-hour

echo DONE >>"$status"
wait "$comp" || fail "compositor exited with an error"
grep -q "afterglow-shell: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "afterglow-shell: PASS (badge $badge_before -> $badge_after, accent $accent -> $final_accent)"
