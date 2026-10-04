#!/usr/bin/env bash
# Plugin manager: name resolution, deferred defaults, list/reload/unload.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_LUA_INIT="$ROOT/tests/headless-plugin-manager-init.lua"
export SHADY_TEST_PLUGIN_DIR="$ROOT/$BUILD_DIR"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
if ! timeout 8s "$BUILD_DIR/shady" -c tests/headless-plugin-manager-config.lua > "$tmp/log" 2>&1; then
    cat "$tmp/log" >&2
    exit 1
fi
fail() { echo "headless-plugin-manager: $1" >&2; cat "$tmp/log" >&2; exit 1; }
if rg -q 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$tmp/log"; then
    fail "sanitizer report"
fi
rg -q "plugin-manager-test: PASS" "$tmp/log" || fail "runtime checks did not pass"
# Before defaults load: disabled default, pending default, loaded-but-not-started plugin.
for state in "window-motion=disabled" "obj-loader=pending" "counter-plugin=inactive" "does-not-exist=failed"; do
    rg -q "plugin-manager-test: bootstrap $state" "$tmp/log" || fail "bootstrap state $state"
done
# A disabled default must never be dlopen'ed.
if rg -q "plugin: loaded window-motion" "$tmp/log"; then fail "disabled default was loaded"; fi
# Overriding a shipped plugin by path replaces the default.
if ! SHADY_LUA_INIT="$ROOT/tests/headless-plugin-manager-override-init.lua" \
        timeout 8s "$BUILD_DIR/shady" -c tests/headless-plugin-manager-override-config.lua \
        > "$tmp/override.log" 2>&1; then
    cat "$tmp/override.log" >&2
    exit 1
fi
rg -q "plugin-manager-override: PASS" "$tmp/override.log" || {
    cat "$tmp/override.log" >&2
    fail "override of a shipped plugin"
}
echo "headless-plugin-manager: PASS"
