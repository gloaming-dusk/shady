#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
export XDG_RUNTIME_DIR="$tmp/runtime"
export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT" SHADY_ASTRAL_BUILD_DIR="$ROOT/$BUILD_DIR"
export SHADY_LUA_INIT="$ROOT/tests/headless-astral-loom-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$BUILD_DIR/headless-automation-probe"
export SHADY_ASTRAL_SCREENSHOT="${SHADY_ASTRAL_SCREENSHOT:-$tmp/astral.png}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
if ! timeout 20s "$BUILD_DIR/shady" -c examples/rice/astral-loom/config.lua >"$tmp/log" 2>&1; then
    tail -n 60 "$tmp/log" >&2
    exit 1
fi
if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$tmp/log"; then
    tail -n 60 "$tmp/log" >&2
    exit 1
fi
if ! grep -q 'astral-loom-test: PASS' "$tmp/log" || [[ ! -s "$SHADY_ASTRAL_SCREENSHOT" ]]; then
    tail -n 60 "$tmp/log" >&2
    exit 1
fi
echo "astral-loom: PASS"
