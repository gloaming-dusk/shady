#!/usr/bin/env bash
# shady-shell on two outputs: one bar per output, a HiDPI bar at scale 2,
# and a bar that goes away and comes back when its output is turned off and
# on. Runs once per renderer (gl, shm).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-outputs: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v wlr-randr >/dev/null && command -v grim >/dev/null ||
    { echo "shell-outputs: needs wlr-randr and grim" >&2; exit 2; }

run() {
    local renderer="$1"
    local tmp
    tmp="$(mktemp -d)"
    mkdir -m 700 "$tmp/runtime"
    local log="$tmp/log"
    (
        export XDG_RUNTIME_DIR="$tmp/runtime"
        unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
        export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman
        export SHADY_LUA_INIT="$ROOT/tests/headless-shell-outputs-init.lua"
        export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
        export SHADY_SHELL_RENDERER="$renderer"
        export SHADY_SHELL_CONFIG="$ROOT/shell/default.lua"
        export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
        export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
        exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
    ) >"$log" 2>&1 &
    local comp=$!
    fail() {
        echo "shell-outputs[$renderer]: $1" >&2
        cat "$log" >&2
        kill "$comp" 2>/dev/null || true
        rm -rf "$tmp"
        exit 1
    }
    wait_for() { # wait_for <count> <pattern>
        for _ in $(seq 1 250); do
            (( $(grep -c -- "$2" "$log" || true) >= $1 )) && return 0
            kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
            sleep 0.02
        done
        fail "timed out waiting for $1x '$2'"
    }
    export XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0

    wait_for 1 "shady-shell: renderer $renderer"
    wait_for 1 "shady-shell: shady-shell on HEADLESS-1"
    wait_for 1 "shady-shell: shady-shell on HEADLESS-2"

    wlr-randr --output HEADLESS-2 --scale 2
    wait_for 1 "shady-shell: shady-shell scale 2"
    # The compositor re-arranges layer surfaces when an output changes, so
    # the bar follows the output's new logical width.
    wait_for 1 "shady-shell: shady-shell size 640x38"
    sleep 0.3
    grim -o HEADLESS-1 "$tmp/one.png"
    grim -o HEADLESS-2 "$tmp/two.png"
    if [[ -n "${SHADY_SHELL_OUTPUTS_SHOTS:-}" ]]; then
        cp "$tmp/one.png" "$SHADY_SHELL_OUTPUTS_SHOTS-$renderer-1.png"
        cp "$tmp/two.png" "$SHADY_SHELL_OUTPUTS_SHOTS-$renderer-2.png"
    fi

    wlr-randr --output HEADLESS-2 --off
    wait_for 1 "shady-shell: shady-shell removed from HEADLESS-2"
    wlr-randr --output HEADLESS-2 --on
    wait_for 2 "shady-shell: shady-shell on HEADLESS-2"

    "$BUILD_DIR/shadyctl" quit >/dev/null
    wait "$comp" || fail "compositor exited with an error"
    grep -q "shell-outputs: timed out" "$log" && fail "timed out"
    grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer|shady-shell: .*failed' "$log" &&
        fail "sanitizer or shell error"
    rm -rf "$tmp"
    echo "shell-outputs[$renderer]: PASS"
}

run gl
run shm
