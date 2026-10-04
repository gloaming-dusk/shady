#!/usr/bin/env bash
# Published values: Lua (shady.publish) and a plugin (publish_value) share
# values; IPC lists them, replays them to new value.changed subscribers and
# streams changes; shady-shell follows them through shell.compositor_value;
# unloading the plugin removes its value.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell shadyctl libshady-plugin-publish-probe.so; do
    [[ -e "$BUILD_DIR/$bin" ]] || { echo "published-values: missing $BUILD_DIR/$bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
status="$tmp/status"
log="$tmp/log"
: >"$status"
(
    export XDG_RUNTIME_DIR="$tmp/runtime"
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
    export SHADY_LUA_INIT="$ROOT/tests/headless-published-values-init.lua"
    export SHADY_TEST_PLUGIN_DIR="$ROOT/$BUILD_DIR"
    export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
    export SHADY_SHELL_CONFIG="$ROOT/tests/published-values/shell.lua"
    export VALUES_STATUS="$status"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-published-values-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "published-values: $1" >&2; cat "$log" >&2; exit 1; }
ctl() { XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" "$@"; }
wait_for() {
    for _ in $(seq 1 300); do
        grep -q -- "$1" "$log" && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$1'"
        sleep 0.02
    done
    fail "timed out waiting for '$1'"
}

wait_for "publish-probe: published"
grep -q "publish-probe: an invalid key was accepted" "$log" && fail "invalid key accepted"
wait_for "shady-shell: following compositor values"
wait_for "lua: probe.greeting=hello from a plugin"
wait_for "lua: test.hour=golden"

values="$(ctl values)"
[[ "$values" == *'"test.hour":"golden"'* && "$values" == *'"probe.greeting":"hello from a plugin"'* ]] ||
    fail "values: $values"
[[ "$(ctl value key=test.hour)" == '"golden"' ]] || fail "value: $(ctl value key=test.hour)"
[[ "$(ctl value key=missing)" == "null" ]] || fail "missing value is not null"

# A new subscriber first gets every current value, then changes.
ctl subscribe events=value.changed >"$tmp/events" &
sub=$!
for _ in $(seq 1 100); do grep -q '"key":"test.hour"' "$tmp/events" && break; sleep 0.02; done
grep -q '"event":"value.changed","key":"test.hour","value":"golden"' "$tmp/events" || fail "no replay: $(cat "$tmp/events")"

echo CHANGE >>"$status"
wait_for "lua: test.hour=night"
for _ in $(seq 1 100); do grep -q '"value":"night"' "$tmp/events" && break; sleep 0.02; done
grep -q '"key":"test.hour","value":"night"' "$tmp/events" || fail "change not streamed"

echo REMOVE >>"$status"
wait_for "lua: test.hour=<none>"
for _ in $(seq 1 100); do grep -q '"key":"test.hour","value":null' "$tmp/events" && break; sleep 0.02; done
grep -q '"key":"test.hour","value":null' "$tmp/events" || fail "removal not streamed"

ctl plugin.unload name=publish-probe >/dev/null
wait_for "lua: probe.greeting=<none>"
[[ "$(ctl value key=probe.greeting)" == "null" ]] || fail "unloaded plugin's value survived"

ctl quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
kill "$sub" 2>/dev/null || true
grep -q "published-values: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "published-values: PASS"
