#!/usr/bin/env bash
# Compositor IPC: shadyctl queries state, receives subscribed events, moves
# and focuses a window, switches workspaces, presses a plugin key, reloads
# and unloads the plugin, rejects bad requests, and quits the compositor,
# which removes the socket.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
CTL="$ROOT/$BUILD_DIR/shadyctl"
tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
export XDG_RUNTIME_DIR="$tmp/runtime"
# Spawned clients inherit our environment; let them find the nested socket.
unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-ipc-init.lua"
export SHADY_TEST_PLUGIN_DIR="$ROOT/$BUILD_DIR"
export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
log="$tmp/log"
sock="$XDG_RUNTIME_DIR/shady-wayland-0.sock"

"$BUILD_DIR/shady" -c tests/headless-ipc-config.lua >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

fail() { echo "headless-ipc: $1" >&2; cat "$log" >&2; exit 1; }
ctl() { "$CTL" -s "$sock" "$@"; }
expect() { # expect <pattern> <output>
    [[ "$2" == *"$1"* ]] || fail "expected '$1' in: $2"
}

for _ in $(seq 1 500); do
    grep -q "ipc-test: ready" "$log" && break
    kill -0 "$comp" 2>/dev/null || fail "compositor exited early"
    sleep 0.02
done
grep -q "ipc-test: ready" "$log" || fail "probe window never mapped"
[[ -S "$sock" ]] || fail "socket missing"
[[ "$(stat -c %a "$sock")" == 700 ]] || fail "socket is not private"

expect '"protocol":1' "$(ctl version)"
windows="$(ctl windows)"
expect '"app_id":"ipc-probe"' "$windows"
expect '"title":"IPC \"probe\""' "$windows"
expect '"focused":true' "$windows"
id="$(sed -n 's/.*"id":\([0-9]*\),"app_id":"ipc-probe".*/\1/p' <<<"$windows")"
[[ -n "$id" ]] || fail "no window id in: $windows"
expect '"current":"main"' "$(ctl workspaces)"
expect '"name":"HEADLESS-1"' "$(ctl outputs)"

# Lua runs in init.lua's state and returns JSON values, including tables.
[[ "$(ctl lua '')" == '[]' ]] || fail "empty Lua chunk failed"
[[ "$(ctl lua 'return 42, true, nil, "hello\nworld", "a\0b"')" == '[42,true,null,"hello\nworld","a\u0000b"]' ]] || fail "Lua scalar results"
[[ "$(ctl lua 'return {1, {answer = 42}}, {}')" == '[[1,{"answer":42}],{}]' ]] || fail "Lua table results"
expect 'IPC \"probe\"' "$(ctl lua 'return ipc_test_title()')"
ctl lua 'ipc_test_counter = 41' >/dev/null
[[ "$(ctl lua 'ipc_test_counter = ipc_test_counter + 1; return ipc_test_counter')" == '[42]' ]] || fail "Lua state did not persist"
expect '"ok":true,"data":[42]' "$(ctl --json lua 'return 42')"
[[ "$(ctl lua 'return "false"')" == '["false"]' ]] || fail "Lua source coerced to a JSON value"
[[ "$(printf 'return 6 * 7\n' | ctl lua -)" == '[42]' ]] || fail "Lua stdin failed"
printf 'return ipc_test_counter, "file"\n' >"$tmp/script.lua"
[[ "$(ctl lua -f "$tmp/script.lua")" == '[42,"file"]' ]] || fail "Lua file failed"
ctl lua 'assert(shady.workspace("lua-test"))' >/dev/null
expect '"current":"lua-test"' "$(ctl workspaces)"
ctl lua 'assert(shady.workspace("main"))' >/dev/null
ctl lua 'ipc_test_handler = shady.on("workspace.changed", function(name) shady.publish("ipc.lua.workspace", name) end)' >/dev/null
ctl lua 'shady.workspace("lua-callback")' >/dev/null
[[ "$(ctl value key=ipc.lua.workspace)" == '"lua-callback"' ]] || fail "Lua event handler did not persist"
ctl lua 'shady.off(ipc_test_handler); shady.workspace("main")' >/dev/null

lua_fails() {
    local out
    if out="$(ctl lua "$1")"; then fail "Lua unexpectedly succeeded: $1"; fi
    expect '"ok":false' "$out"
    expect "$2" "$out"
}
lua_fails 'return )' 'shadyctl:1:'
lua_fails 'error("ipc lua failure")' 'ipc lua failure'
lua_fails 'error({})' 'non-string value'
lua_fails 'return function() end' 'unsupported type'
lua_fails 'return shady.focused_window()' 'unsupported type'
lua_fails 'return 0/0' 'non-finite'
lua_fails 'return {[2] = "sparse"}' 'consecutive integer keys'
lua_fails 'local t = {}; t.self = t; return t' 'serialization limits'
lua_fails 'return string.rep("x", 1024 * 1024)' 'too large'
[[ "$(ctl lua 'return setmetatable({x = 1}, {__pairs = function() error("metamethod") end}), "safe"')" == '[{"x":1},"safe"]' ]] || fail "Lua serialization invoked metamethods"
expect 'missing string argument: code' "$(ctl --raw '{"cmd":"lua","code":true}' || true)"
# Errors leave the runtime stack usable, and preserve preceding side effects.
lua_fails 'ipc_test_counter = 43; error("after mutation")' 'after mutation'
[[ "$(ctl lua 'return ipc_test_counter')" == '[43]' ]] || fail "Lua error corrupted runtime state"
for _ in $(seq 1 30); do
    [[ "$(ctl lua 'return 42')" == '[42]' ]] || fail "Lua stack leaked between calls"
done
if ctl lua -f "$tmp/missing.lua" >"$tmp/out" 2>"$tmp/error"; then fail "missing Lua file accepted"; fi
expect 'cannot read' "$(cat "$tmp/error")"
if head -c 65536 /dev/zero | tr '\0' 'x' | ctl lua - >"$tmp/out" 2>"$tmp/error"; then fail "oversized Lua source accepted"; fi
expect '64 KiB' "$(cat "$tmp/error")"
if printf '\0' | ctl lua - >"$tmp/out" 2>"$tmp/error"; then fail "binary Lua source accepted"; fi
expect 'must be text' "$(cat "$tmp/error")"

# Events arrive while another client acts.
ctl subscribe events=window.focused,workspace.changed >"$tmp/events" &
sub=$!
for _ in $(seq 1 100); do [[ -s "$tmp/events" ]] && break; sleep 0.02; done
expect '"workspace.changed"' "$(cat "$tmp/events")"

ctl workspace.switch name=two >/dev/null
expect '"current":"two"' "$(ctl workspaces)"
ctl window.move window="$id" workspace=two >/dev/null
expect '"workspace":"two"' "$(ctl windows)"
ctl workspace.switch name=main >/dev/null
ctl window.focus window="$id" >/dev/null # follows the window to its workspace
expect '"current":"two"' "$(ctl workspaces)"
ctl window.maximize window="$id" state=true >/dev/null
expect '"maximized":true' "$(ctl focused)"
ctl window.maximize window="$id" >/dev/null # toggles back
expect '"maximized":false' "$(ctl focused)"

for _ in $(seq 1 100); do
    grep -q '"window.focused"' "$tmp/events" && grep -q '"workspace":"two"' "$tmp/events" && break
    sleep 0.02
done
grep -q '"event":"workspace.changed","workspace":"two"' "$tmp/events" || fail "no workspace event: $(cat "$tmp/events")"
grep -q '"event":"window.focused","window":{"id":'"$id" "$tmp/events" || fail "no focus event: $(cat "$tmp/events")"

# The shortcut list gathers the compositor's bindings, Lua's and plugins'.
shortcuts="$(ctl shortcuts)"
expect '{"keys":"Super+Shift+slash","group":"Shady","description":"Show these shortcuts"}' "$shortcuts"
expect '{"keys":"Alt+Left/Right/Up/Down","group":"Camera","description":"Pan the camera"}' "$shortcuts"
expect '{"keys":"Super+Z","group":"frozen-window","description":"Freeze or thaw the focused window"}' "$shortcuts"
expect '{"keys":"Super+F9","group":"Desktop","description":"Probe binding"}' "$shortcuts"
expect '{"keys":"Super+F10","group":"Probes","description":"Grouped probe"}' "$shortcuts"
[[ "$shortcuts" != *'Super+F11'* ]] || fail "undescribed binding listed: $shortcuts"
[[ "$shortcuts" != *'"First person"'* ]] || fail "inactive fps module listed: $shortcuts"
ctl subscribe events=shortcuts.toggle >"$tmp/shortcut-events" &
shortcut_sub=$!
for _ in $(seq 1 100); do [[ -s "$tmp/shortcut-events" ]] && break; sleep 0.02; done
expect '"handled":true' "$(ctl key keys=Super+Shift+slash)"
for _ in $(seq 1 100); do grep -q '"event":"shortcut",' "$tmp/shortcut-events" && break; sleep 0.02; done
count="$(sed -n 's/.*"event":"shortcuts.toggle","count":\([0-9]*\).*/\1/p' "$tmp/shortcut-events")"
[[ -n "$count" ]] || fail "no toggle event: $(cat "$tmp/shortcut-events")"
for _ in $(seq 1 100); do
    [[ "$(grep -c '"event":"shortcut",' "$tmp/shortcut-events")" == "$count" ]] && break
    sleep 0.02
done
[[ "$(grep -c '"event":"shortcut",' "$tmp/shortcut-events")" == "$count" ]] ||
    fail "toggle announced $count shortcuts: $(cat "$tmp/shortcut-events")"
grep -q '"event":"shortcut","keys":"Super+Z","group":"frozen-window"' "$tmp/shortcut-events" ||
    fail "plugin shortcut missing from the toggle: $(cat "$tmp/shortcut-events")"
kill "$shortcut_sub" 2>/dev/null || true

# Plugins and keys go through the same paths as Lua and real input.
expect '"name":"frozen-window","spec":"frozen-window","state":"active"' "$(ctl plugins)"
expect '"handled":true' "$(ctl key keys=Super+z)"
expect '"handled":false' "$(ctl key keys=Super+F12)"
ctl plugin.reload name=frozen-window >/dev/null
expect '"keys":"Super+Z","group":"frozen-window"' "$(ctl shortcuts)"
ctl plugin.unload name=frozen-window >/dev/null
expect '"handled":false' "$(ctl key keys=Super+z)"
[[ "$(ctl shortcuts)" != *'"frozen-window"'* ]] || fail "unloaded plugin's shortcuts still listed"
expect '"error":"unload failed"' "$(ctl plugin.unload name=frozen-window || true)"

# Errors are reported, and a bad request does not end the connection.
out="$(ctl window.close window=999999 || true)"
expect '"ok":false' "$out"
expect '"error":"no such window"' "$out"
expect '"unknown command"' "$(ctl nope || true)"
expect '"unknown event name"' "$(ctl subscribe events=nope || true)"
expect '"nested objects are not supported"' "$(ctl --raw '{"cmd":"version","a":{}}' || true)"
two="$(printf '%s\n' 'not json' '{"id":"x","cmd":"version"}' | timeout 5 socat -t 3 - "UNIX-CONNECT:$sock" 2>/dev/null || true)"
if command -v socat >/dev/null; then
    expect '"ok":false' "$two"
    expect '"id":"x","ok":true' "$two"
fi

ctl quit >/dev/null
for _ in $(seq 1 250); do kill -0 "$comp" 2>/dev/null || break; sleep 0.02; done
kill -0 "$comp" 2>/dev/null && fail "compositor did not quit"
wait "$comp" || fail "compositor exited with an error"
wait "$sub" 2>/dev/null || true
[[ -e "$sock" ]] && fail "socket left behind"
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer|ipc-test: timed out' "$log"; then
    fail "sanitizer error or timeout"
fi

# Lua can be disabled at runtime without disabling the IPC server.
printf 'shady.modules({lua = false, physics = false, fps = false})\n' >"$tmp/no-lua.lua"
"$BUILD_DIR/shady" --safe -c "$tmp/no-lua.lua" >"$log" 2>&1 &
comp=$!
for _ in $(seq 1 250); do
    [[ -S "$sock" ]] && break
    kill -0 "$comp" 2>/dev/null || fail "compositor without Lua exited early"
    sleep 0.02
done
[[ -S "$sock" ]] || fail "socket missing without Lua"
lua_fails 'return 42' 'Lua runtime is disabled or unavailable'
expect '"protocol":1' "$(ctl version)"
ctl quit >/dev/null
wait "$comp" || fail "compositor without Lua exited with an error"
[[ -e "$sock" ]] && fail "socket left behind without Lua"
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
    fail "sanitizer error without Lua"
fi
echo "headless-ipc: PASS"
