#!/usr/bin/env bash
# The shell's notification server on a private session bus: Notify returns
# ids and replaces in place, notifications expire and close, the history
# reaches Lua, dismissing or invoking an action emits the spec's signals,
# and a config reload shows notifications that are still active.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
for bin in shady shady-shell shadyctl; do
    [[ -x "$BUILD_DIR/$bin" ]] || { echo "shell-notify: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
for tool in dbus-run-session busctl; do
    command -v "$tool" >/dev/null || { echo "shell-notify: needs $tool" >&2; exit 2; }
done

if [[ -z "${SHADY_NOTIFY_PRIVATE_BUS:-}" ]]; then
    exec env SHADY_NOTIFY_PRIVATE_BUS=1 dbus-run-session -- "$0" "$@"
fi

tmp="$(mktemp -d)"
mkdir -m 700 "$tmp/runtime"
log="$tmp/log"
signals="$tmp/signals"
config="$tmp/shell.lua"
write_config() { # write_config <generation>
    sed "s/@GEN@/$1/" >"$config.new" <<'LUA'
shell.log("server " .. tostring(shell.notification_server { timeout = 400 }))
shell.popup {
    name = "toasts",
    view = function()
        shell.log("toasts shown @GEN@")
        return shell.box { width = 10, height = 10 }
    end,
}
-- The history outlives reloads: sync once now, then on every change.
local function sync()
    local any = false
    for _, n in ipairs(shell.notifications()) do any = any or n.active end
    if any ~= shell.is_open("toasts") then
        if any then shell.open("toasts") else shell.close("toasts") end
    end
end
shell.on("notifications", sync)
sync()
shell.on("notifications", function()
    local parts = {}
    for _, n in ipairs(shell.notifications()) do
        parts[#parts + 1] = string.format("%d:%s:%s:%s:%d", n.id, n.summary,
            n.active and "on" or "off", n.urgency, #n.actions)
        if n.active and n.summary == "invoke me" then
            shell.invoke_notification(n.id, n.actions[1].key)
        end
        if n.active and n.summary == "dismiss me" then
            shell.dismiss_notification(n.id)
        end
    end
    shell.log("list " .. table.concat(parts, " "))
end)
shell.bar { name = "notify-bar", size = 24, view = function() return shell.row {} end }
LUA
    mv "$config.new" "$config"
}
write_config 1

busctl --user monitor --match "type='signal',interface='org.freedesktop.Notifications'" \
    >"$signals" 2>&1 &
monitor=$!
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
cleanup() {
    kill "$comp" "$monitor" 2>/dev/null || true
    wait "$comp" "$monitor" 2>/dev/null || true
    rm -rf "$tmp"
}
trap cleanup EXIT
fail() { echo "shell-notify: $1" >&2; cat "$log" >&2; echo "--- signals" >&2; cat "$signals" >&2; exit 1; }
wait_for() { # wait_for <file> <pattern>
    for _ in $(seq 1 300); do
        grep -q -- "$2" "$1" && return 0
        kill -0 "$comp" 2>/dev/null || fail "compositor exited waiting for '$2'"
        sleep 0.02
    done
    fail "timed out waiting for '$2'"
}
notify() { # notify <replaces> <summary> <timeout> [action key, label]
    local replaces="$1" summary="$2" timeout="$3"
    shift 3
    busctl --user -- call org.freedesktop.Notifications /org/freedesktop/Notifications \
        org.freedesktop.Notifications Notify susssasa{sv}i \
        test-app "$replaces" "" "$summary" "body text" "$#" "$@" \
        1 urgency y 2 "$timeout"
}

wait_for "$log" "lua: server true"
wait_for "$log" "serving notifications"

[[ "$(notify 0 first 0)" == "u 1" ]] || fail "first id is not 1"
wait_for "$log" "list 1:first:on:critical:0"
[[ "$(notify 1 replaced 0)" == "u 1" ]] || fail "replacing changed the id"
wait_for "$log" "list 1:replaced:on:critical:0"

[[ "$(notify 0 short 300)" == "u 2" ]] || fail "second id is not 2"
wait_for "$log" "list 2:short:off:critical:0 1:replaced:on:critical:0"

busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications \
    org.freedesktop.Notifications CloseNotification u 1 >/dev/null
wait_for "$log" "1:replaced:off"

notify 0 "invoke me" 0 open Open >/dev/null
wait_for "$signals" "ActionInvoked"
notify 0 "dismiss me" 0 >/dev/null
wait_for "$log" "4:dismiss me:off"

info="$(busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications \
    org.freedesktop.Notifications GetServerInformation)"
[[ "$info" == *shady-shell* ]] || fail "server information: $info"

# A critical notification stays; after a reload its toast comes back.
notify 0 "stays" -1 >/dev/null
wait_for "$log" "toasts shown 1"
write_config 2
wait_for "$log" "shady-shell: reloaded"
wait_for "$log" "toasts shown 2"

# The spec's close reasons: expired (1), dismissed (2), closed by a call (3).
sleep 0.3
for reason in "UINT32 1" "UINT32 2" "UINT32 3"; do
    grep -q "$reason" "$signals" || fail "no NotificationClosed with reason ${reason#UINT32 }"
done
grep -q 'STRING "open"' "$signals" || fail "ActionInvoked did not carry the action key"

XDG_RUNTIME_DIR="$tmp/runtime" "$BUILD_DIR/shadyctl" -s "$tmp/runtime/shady-wayland-0.sock" quit >/dev/null
wait "$comp" || fail "compositor exited with an error"
grep -q "shell-lua: timed out" "$log" && fail "timed out"
grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log" && fail "sanitizer error"
echo "shell-notify: PASS"
