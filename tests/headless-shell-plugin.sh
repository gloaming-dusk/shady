#!/usr/bin/env bash
# Native shell plugins: the probe plugin publishes values from options, an
# fd watch and an action, draws a clipped custom widget, and survives a Lua
# reload without a second init; sysinfo publishes CPU/memory/load and draws
# its graph; an unknown plugin is reported without stopping the config; and
# plugins are destroyed when the shell exits.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl libshady-shell-plugin-probe.so libshady-shell-plugin-sysinfo.so; do
    [[ -e "$BUILD_DIR/$bin" ]] || { echo "shell-plugin: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
command -v grim >/dev/null || { echo "shell-plugin: needs grim" >&2; exit 2; }

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime" "$tmp/config"
log="$tmp/log"
config="$tmp/config/shell.lua"

write_config() { # write_config <generation>
    cat >"$config.new" <<LUA
assert(shell.plugin("probe", { greeting = "hello" }))
assert(shell.plugin("sysinfo", { interval = 300 }))
local ok, err = shell.plugin("does-not-exist")
assert(not ok and err:find("not found"), err)
assert(shell.action("probe.echo", "generation-$1"))
assert(not shell.action("probe.nothing"))
local seen = {}
local function note(key)
    local v = shell.value(key, "")
    if v ~= "" and seen[key] ~= v then
        seen[key] = v
        shell.log(key .. "=" .. v)
    end
end
shell.bar {
    name = "plugin-bar",
    size = 40,
    view = function()
        for _, key in ipairs({ "probe.greeting", "probe.pipe", "probe.unwatched",
                "probe.cancel", "probe.echo", "sysinfo.cpu", "sysinfo.memory" }) do
            note(key)
        end
        return shell.row {
            padding = { 0, 10 }, gap = 10, background = "#202020",
            shell.widget("probe.box", { width = 40, height = 30, color = "#ff00ff" }),
            shell.widget("sysinfo.graph", { width = 120, height = 30, color = "#00ff00" }),
            shell.spacer(),
        }
    end,
}
LUA
    mv "$config.new" "$config"
}

write_config 1
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET SHADY_SHELL_PLUGIN_PATH
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
    export SHADY_LUA_INIT="$ROOT/tests/headless-shell-lua-init.lua"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$config"
    export SHADY_SHELL_PLUGIN_PATH="$ROOT/$BUILD_DIR"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "shell-plugin: $1" >&2; cat "$log" >&2; exit 1; }
wait_for() { # wait_for <count> <pattern>
    for _ in $(seq 1 300); do
        (( $(grep -c -- "$2" "$log" || true) >= $1 )) && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
        sleep 0.02
    done
    fail "timed out waiting for $1x '$2'"
}
pixel() {
    XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 \
        grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tu1 | xargs
}

wait_for 1 "shady-shell: loaded plugin probe"
wait_for 1 "shady-shell: loaded plugin sysinfo"
wait_for 1 "plugin does-not-exist not found"
wait_for 1 "shady-shell: plugin-bar on HEADLESS-1"
wait_for 1 "lua: probe.greeting=hello"
wait_for 1 "lua: probe.echo=generation-1"
wait_for 1 "lua: probe.pipe=tick"
wait_for 1 "lua: probe.unwatched=yes"
wait_for 1 "lua: sysinfo.memory=[0-9]"
wait_for 1 "lua: sysinfo.cpu=[0-9]"
sleep 1.5 # let the graph collect a few samples
sleep 0.3
[[ "$(pixel 30 20)" == "255 0 255" ]] || fail "probe.box not drawn: $(pixel 30 20)"
[[ "$(pixel 30 2)" == "32 32 32" ]] || fail "probe.box not clipped: $(pixel 30 2)"
# The graph is a green sparkline over a green fill: count clearly green pixels.
green="$(XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 grim -g "60,5 120x30" -t ppm - |
    tail -c $((120 * 30 * 3)) | od -An -tu1 -v -w3 | awk '$2 > $1 + 60 && $2 > $3 + 60 { n++ } END { print n + 0 }')"
(( green >= 5 )) || fail "sysinfo.graph drew nothing ($green green pixels)"
[[ -n "${SHADY_SHELL_PLUGIN_SHOT:-}" ]] && XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 grim -g "0,0 400x40" "$SHADY_SHELL_PLUGIN_SHOT"

write_config 2
wait_for 1 "shady-shell: reloaded $config"
wait_for 1 "lua: probe.echo=generation-2"
(( $(grep -c "shady-shell: probe: init" "$log") == 1 )) || fail "probe was initialised again on reload"
grep -q "probe.cancel=fired" "$log" && fail "a cancelled timer fired"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -q "shady-shell: probe: destroyed" "$log" || fail "probe was not destroyed on exit"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "shell-plugin: PASS"
