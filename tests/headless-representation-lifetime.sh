#!/usr/bin/env bash
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
export SHADY_LUA_INIT="$ROOT/tests/headless-representation-lifetime-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
for module in fps-cube fps-folded-paper fps-origami; do
    export SHADY_REPRESENTATION_TEST_MODULE="$module"
    export SHADY_REPRESENTATION_TEST_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-$module.so"
    if ! timeout 8s "$BUILD_DIR/shady" -c tests/headless-representation-lifetime-config.lua > "$tmp/log" 2>&1; then
        cat "$tmp/log" >&2
        exit 1
    fi
    if rg -q 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$tmp/log" ||
            ! rg -q "representation-lifetime: PASS $module" "$tmp/log"; then
        cat "$tmp/log" >&2
        exit 1
    fi
    echo "representation-lifetime: $module PASS"
done
