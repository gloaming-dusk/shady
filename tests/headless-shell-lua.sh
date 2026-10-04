#!/usr/bin/env bash
# The shell's Lua runtime: shell.poll and shell.listen feed views, a config
# change on disk is hot-reloaded, a broken config keeps the running one, and
# a config that fails at startup falls back to shell/default.lua.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-lua: missing $BUILD_DIR/$bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime" "$tmp/config"
log="$tmp/log"
config="$tmp/config/shell.lua"

write_config() { # write_config <generation>
    cat >"$config.new" <<LUA
local last = {}
local function note(key, value)
    if value ~= "" and last[key] ~= value then
        last[key] = value
        shell.log(key .. "=" .. value)
    end
end
shell.log("generation $1")
shell.bar {
    name = "test-bar",
    size = 30,
    view = function(ctx)
        local polled = shell.poll("echo hello-poll-$1", 200)
        local heard = shell.listen("printf 'first\\\\nsecond-$1\\\\n'; sleep 30")
        note("poll", polled)
        note("listen", heard)
        return shell.row {
            padding = { 0, 8 }, gap = 8,
            shell.text(polled), shell.text(heard), shell.spacer(), shell.text(shell.date("%H:%M")),
        }
    end,
}
LUA
    mv "$config.new" "$config" # a rename, as editors do
}

write_config 1
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
fail() { echo "shell-lua: $1" >&2; cat "$log" >&2; exit 1; }
wait_for() { # wait_for <count> <pattern>
    for _ in $(seq 1 300); do
        (( $(grep -c -- "$2" "$log" || true) >= $1 )) && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
        sleep 0.02
    done
    fail "timed out waiting for $1x '$2'"
}

wait_for 1 "shady-shell: loaded $config"
wait_for 1 "shady-shell: test-bar on HEADLESS-1"
wait_for 1 "lua: poll=hello-poll-1"
wait_for 1 "lua: listen=second-1"

write_config 2
wait_for 1 "shady-shell: reloaded $config"
wait_for 1 "lua: poll=hello-poll-2"
wait_for 1 "lua: listen=second-2"
wait_for 2 "shady-shell: test-bar on HEADLESS-1"

printf 'shell.bar { name = "broken"\n' >"$config.new"
mv "$config.new" "$config"
wait_for 1 "shady-shell: reload failed; keeping the running config"
grep -q "test-bar removed" "$log" && fail "a failed reload removed the running bar"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -q "shell-lua: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"

# A config that fails at startup falls back to the default UI.
startup_log="$tmp/startup.log"
printf 'error("deliberately broken")\n' >"$config"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
    export SHADY_LUA_INIT="$ROOT/tests/headless-shell-lua-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$config"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
) >"$startup_log" 2>&1 &
comp=$!
log="$startup_log"
wait_for 1 "deliberately broken"
wait_for 1 "shady-shell: falling back to $ROOT/shell/default.lua"
wait_for 1 "shady-shell: shady-shell on HEADLESS-1"
XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
echo "shell-lua: PASS"
