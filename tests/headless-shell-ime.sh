#!/usr/bin/env bash
# Input methods in shady-shell (text-input-v3): a popup with text_input
# shows what the input method composes as ctx.preedit, receives committed
# text through on_key, and reports a caret rectangle. tests/ime-probe.c
# plays a Hangul input method.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl ime-probe; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-ime: missing $BUILD_DIR/$bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
log="$tmp/log"
probe_log="$tmp/probe"
config="$tmp/shell.lua"
cat >"$config" <<'LUA'
shell.popup {
    name = "ime-field",
    keyboard = "exclusive",
    text_input = true,
    persistent = true,
    view = function(ctx)
        if ctx.preedit then shell.log("preedit " .. ctx.preedit) end
        return shell.row {
            width = 200, height = 40,
            shell.text("query"),
            shell.box { width = 2, height = 20, caret = true },
        }
    end,
    on_key = function(key, text) shell.log("key [" .. key .. "] [" .. text .. "]") end,
}
LUA
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
    export SHADY_LUA_INIT="$ROOT/tests/headless-shell-lua-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$config"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "shell-ime: $1" >&2; cat "$log" >&2; echo "--- probe" >&2; cat "$probe_log" >&2; exit 1; }
wait_for() { # wait_for <file> <pattern>
    for _ in $(seq 1 300); do
        grep -q -- "$2" "$1" && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
        sleep 0.02
    done
    fail "timed out waiting for '$2'"
}

: >"$probe_log"
wait_for "$log" "shady-shell: ime-field on"
sleep 0.3
XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 "$BUILD_DIR/ime-probe" >"$probe_log" 2>&1 &
probe=$!
wait_for "$probe_log" "ime: activated"
wait_for "$log" "lua: preedit 한"
wait_for "$log" "lua: key \[\] \[한글\]"
wait "$probe" || fail "the input method probe did not finish"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -q "shell-lua: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "shell-ime: PASS"
