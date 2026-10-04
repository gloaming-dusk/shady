#!/usr/bin/env bash
# The core loads environments only through registered loader plugins.
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
export SHADY_LUA_INIT="$ROOT/tests/headless-environment-init.lua"
export SHADY_ENVIRONMENT_OBJ="$ROOT/assets/test-room.obj"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
if ! timeout 8s "$BUILD_DIR/shady" -c tests/headless-environment-config.lua > "$tmp/log" 2>&1; then
    cat "$tmp/log" >&2
    exit 1
fi
fail() { echo "headless-environment: $1" >&2; cat "$tmp/log" >&2; exit 1; }
if rg -q 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$tmp/log"; then
    fail "sanitizer report"
fi
loaded="environment: 'obj' loaded .*test-room.obj \(22 triangles, 1 collision boxes, 12 collision triangles\)"
# Startup, after reload, and after restoring the path.
[[ "$(rg -c "$loaded" "$tmp/log")" == 3 ]] || fail "expected three scene loads"
rg -q "environment: no loader registered for /nonexistent/scene.gltf" "$tmp/log" ||
    fail "unhandled extension was not reported"
[[ "$(rg -c "environment: registered loader 'obj'" "$tmp/log")" == 2 ]] ||
    fail "expected registration at startup and after reload"
rg -q "environment: unregistered loader 'obj'" "$tmp/log" || fail "unload did not unregister"
echo "headless-environment: PASS"
