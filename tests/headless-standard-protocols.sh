#!/usr/bin/env bash
# Standard protocols for third-party bars: wlr-foreign-toplevel-management,
# ext-foreign-toplevel-list and ext-workspace mirror Shady's windows and
# workspaces, and their requests (activate, maximize, close, workspace
# activation) act on the compositor. A runtime title change reaches both
# toplevel protocols and the IPC socket.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shadyctl protocol-probe headless-automation-probe; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "standard-protocols: missing $BUILD_DIR/$bin" >&2; exit 2; }
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
    export SHADY_LUA_INIT="$ROOT/tests/headless-standard-protocols-init.lua"
    export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
    export STD_STATUS="$status"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
    exec "$BUILD_DIR/shady" --safe -c "$ROOT/tests/headless-automation-config.lua"
) >"$log" 2>&1 &
comp=$!
cleanup() { kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
fail() { echo "standard-protocols: $1" >&2; cat "$log" >&2; exit 1; }
# A one-shot client: it leaves its proxies to the disconnect.
probe() { ASAN_OPTIONS=detect_leaks=0 XDG_RUNTIME_DIR="$tmp/runtime" WAYLAND_DISPLAY=wayland-0 "$BUILD_DIR/protocol-probe" "$@"; }
# expect_list <pattern> <what>: retry briefly, the compositor acts asynchronously.
expect_list() {
    local out=""
    for _ in $(seq 1 100); do
        out="$(probe list)"
        grep -qE -- "$1" <<<"$out" && return 0
        sleep 0.03
    done
    fail "$2: no line matching '$1' in:"$'\n'"$out"
}
reject_list() {
    local out
    out="$(probe list)"
    grep -qE -- "$1" <<<"$out" && fail "$2: unexpected '$1' in:"$'\n'"$out"
    return 0
}

for _ in $(seq 1 300); do grep -q READY "$status" && break; sleep 0.02; done
grep -q READY "$status" || fail "probe windows never mapped"

expect_list "^wlr std-a \| Std A \|$" "std-a listed, not focused"
expect_list "^wlr std-b \| Std B \| activated$" "std-b focused"
expect_list "^ext std-a \| Std A$" "ext list has std-a"
expect_list "^ext std-b \| Std B$" "ext list has std-b"
expect_list "^workspace main active$" "main is active"
expect_list "^workspace x inactive$" "x is listed"

probe activate std-a
expect_list "^wlr std-a \| Std A \| activated$" "activate request"
expect_list "^wlr std-b \| Std B \|$" "focus moved off std-b"

probe maximize std-a
expect_list "^wlr std-a \| .* \| activated maximized$" "maximize request"

probe workspace x
expect_list "^workspace x active$" "workspace activation"
expect_list "^workspace main inactive$" "main deactivated"
probe workspace main
expect_list "^workspace main active$" "back to main"

# std-a renames itself 2.5 s after it starts.
expect_list "^wlr std-a \| Std A renamed \|" "title change (wlr)"
expect_list "^ext std-a \| Std A renamed$" "title change (ext)"
XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" windows |
    grep -q '"title":"Std A renamed"' || fail "title change missing from IPC"

probe close std-b
for _ in $(seq 1 100); do probe list | grep -q "std-b" || break; sleep 0.03; done
reject_list "std-b" "close request"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -q "standard-protocols: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "standard-protocols: PASS"
