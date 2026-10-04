#!/usr/bin/env bash
# Shell shader effects: a surface shader and widget shaders draw where they
# should, a u_time shader animates without repaints from Lua, a shader that
# fails to compile leaves its widget as drawn, an edited shader is reloaded,
# and the shm renderer draws without effects.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-effects: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "shell-effects: needs grim" >&2; exit 2; }

run() {
    local renderer="$1"
    local tmp
    tmp="$(mktemp -d)"
    mkdir -m 700 "$tmp/runtime"
    cp -r tests/shell-effects "$tmp/config"
    local log="$tmp/log"
    (
        export XDG_RUNTIME_DIR="$tmp/runtime"
        unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
        export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
        export SHADY_LUA_INIT="$ROOT/tests/headless-shell-lua-init.lua"
        export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
        export SHADY_SHELL_CONFIG="$tmp/config/shell.lua"
        export SHADY_SHELL_RENDERER="$renderer"
        export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
        export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
        exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
    ) >"$log" 2>&1 &
    local comp=$!
    fail() {
        echo "shell-effects[$renderer]: $1" >&2
        cat "$log" >&2
        kill "$comp" 2>/dev/null || true
        rm -rf "$tmp"
        exit 1
    }
    wait_for() { # wait_for <count> <pattern>
        for _ in $(seq 1 300); do
            (( $(grep -c -- "$2" "$log" || true) >= $1 )) && return 0
            kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
            sleep 0.02
        done
        fail "timed out waiting for $1x '$2'"
    }
    pixel() { # pixel <x> <y> -> "r g b"
        XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 \
            grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tu1 | xargs
    }
    expect_pixel() { # expect_pixel <x> <y> <r g b> <what>
        local got
        got="$(pixel "$1" "$2")"
        [[ "$got" == "$3" ]] || fail "$4: pixel ($1,$2) is '$got', expected '$3'"
    }

    wait_for 1 "shady-shell: renderer $renderer"
    wait_for 1 "shady-shell: fx-bar on HEADLESS-1"
    sleep 0.5

    if [[ "$renderer" == gl ]]; then
        wait_for 1 "compiled shader .*pulse.frag (animated)"
        wait_for 1 "broken.frag failed to compile"
        expect_pixel 600 20 "32 32 32" "bar content"
        expect_pixel 1270 20 "0 255 0" "surface shader"
        expect_pixel 40 20 "255 0 255" "widget shader"
        expect_pixel 180 20 "255 255 255" "broken shader keeps the widget"
        local first second
        first="$(pixel 110 20)"
        sleep 0.4
        second="$(pixel 110 20)"
        [[ "$first" != "$second" ]] || fail "u_time shader did not animate ($first)"

        # Edit a shader: the config reloads and the new shader is compiled.
        sed -i 's/gl_FragColor = u_color;/gl_FragColor = vec4(0.0, 1.0, 1.0, 1.0);/' "$tmp/config/tint.frag"
        wait_for 1 "shady-shell: reloaded"
        sleep 0.5
        expect_pixel 40 20 "0 255 255" "reloaded widget shader"
    else
        wait_for 1 "shader effects need the gl renderer"
        expect_pixel 40 20 "255 255 255" "shm draws without effects"
        expect_pixel 1270 20 "32 32 32" "shm draws without the surface shader"
    fi

    XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
    wait "$comp" || fail "compositor exited with an error"
    grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
    rm -rf "$tmp"
    echo "shell-effects[$renderer]: PASS"
}

run gl
run shm
