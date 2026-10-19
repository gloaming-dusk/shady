#!/usr/bin/env bash
# Wrapped text that layout makes narrower than its max_width re-wraps and
# grows: a popup whose 600 px text is squeezed into a 160 px column ends up
# as tall as one whose text wraps at 160 px to begin with.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-wrap: missing $BUILD_DIR/$bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
log="$tmp/log"
config="$tmp/shell.lua"
cat >"$config" <<'LUA'
local words = "Three new releases landed this week, including a compositor " ..
    "update, a new rice and notification support across every shell."
local function card(name, wrap_at, top)
    shell.popup {
        name = name, anchor = { "top", "left" }, margin = { top = top, left = 20 },
        persistent = true,
        view = function()
            return shell.column {
                width = 160, align = "stretch",
                shell.text(words, { wrap = true, max_width = wrap_at }),
            }
        end,
    }
end
card("squeezed", 600, 20)
card("narrow", 160, 300)
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
fail() { echo "shell-wrap: $1" >&2; cat "$log" >&2; exit 1; }
wait_for() {
    for _ in $(seq 1 300); do
        grep -q -- "$1" "$log" && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$1'"
        sleep 0.02
    done
    fail "timed out waiting for '$1'"
}

wait_for "shady-shell: squeezed size"
wait_for "shady-shell: narrow size"
sleep 0.5 # let any re-wrap settle
size() { grep "shady-shell: $1 size" "$log" | tail -n1 | sed 's/.* size //'; }
squeezed="$(size squeezed)" narrow="$(size narrow)"
[[ "$squeezed" == "$narrow" ]] || fail "squeezed text is $squeezed, the same text wrapped at 160 px is $narrow"
height="${narrow#*x}"
(( height > 40 )) || fail "the text did not wrap at all ($narrow)"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "shell-wrap: PASS ($narrow)"
