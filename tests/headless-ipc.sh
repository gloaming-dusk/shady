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

# Plugins and keys go through the same paths as Lua and real input.
expect '"name":"frozen-window","spec":"frozen-window","state":"active"' "$(ctl plugins)"
expect '"handled":true' "$(ctl key keys=Super+z)"
expect '"handled":false' "$(ctl key keys=Super+F12)"
ctl plugin.reload name=frozen-window >/dev/null
ctl plugin.unload name=frozen-window >/dev/null
expect '"handled":false' "$(ctl key keys=Super+z)"
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
echo "headless-ipc: PASS"
