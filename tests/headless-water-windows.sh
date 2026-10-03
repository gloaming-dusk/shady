#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
PLUGIN="$BUILD_DIR/libshady-plugin-water-windows.so"
GRIM="${GRIM:-$(command -v grim || true)}"
if [[ -z "$GRIM" ]]; then
  echo "water-windows: grim not found" >&2
  exit 2
fi

if [[ ! -x "$COMPOSITOR" || ! -x "$PROBE" || ! -f "$PLUGIN" ]]; then
  echo "water-windows: missing spatial test binaries/plugin in $BUILD_DIR" >&2
  exit 2
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
runtime="$tmp/runtime"
status="$tmp/status"
log="$tmp/compositor.log"
mkdir -p "$runtime"
chmod 700 "$runtime"
: >"$status"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=gles2
export LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT"
export SHADY_LUA_INIT="$ROOT/tests/headless-water-windows-init.lua"
export SHADY_AUTOMATION_PROBE="$ROOT/$PROBE"
export SHADY_WATER_PLUGIN="$ROOT/$PLUGIN"
export SHADY_WATER_STATUS="$status"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

"$COMPOSITOR" -c "$ROOT/tests/headless-water-windows-config.lua" >"$log" 2>&1 &
comp=$!
trap 'kill "$comp" 2>/dev/null || true; wait "$comp" 2>/dev/null || true; rm -rf "$tmp"' EXIT

for i in $(seq 1 200); do
  [[ -S "$runtime/wayland-0" ]] && break
  sleep 0.02
done
export WAYLAND_DISPLAY=wayland-0

wait_mark() {
  local mark="$1"
  for i in $(seq 1 300); do
    grep -q "^$mark$" "$status" && return 0
    kill -0 "$comp" 2>/dev/null || break
    sleep 0.02
  done
  echo "water-windows: FAIL waiting for $mark" >&2
  cat "$log" >&2
  return 1
}

wait_mark WATER_ON_A
"$GRIM" -g '0,0 1280x720' "$tmp/on-a.png"
wait_mark WATER_ON_B
"$GRIM" -g '0,0 1280x720' "$tmp/on-b.png"
wait_mark WATER_OFF_A
"$GRIM" -g '0,0 1280x720' "$tmp/off-a.png"
wait_mark WATER_OFF_B
"$GRIM" -g '0,0 1280x720' "$tmp/off-b.png"
wait_mark DONE

wait "$comp"
trap 'rm -rf "$tmp"' EXIT

if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  cat "$log" >&2
  echo "water-windows: FAIL sanitizer diagnostic" >&2
  exit 1
fi

if cmp -s "$tmp/on-a.png" "$tmp/on-b.png"; then
  echo "water-windows: FAIL animated frames are identical" >&2
  exit 1
fi

if ! cmp -s "$tmp/off-a.png" "$tmp/off-b.png"; then
  echo "water-windows: FAIL disabled frames still change" >&2
  exit 1
fi

echo "WATER_ANIMATION=PASS"
echo "WATER_TOGGLE=PASS"
echo "water-windows: PASS"
