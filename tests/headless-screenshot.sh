#!/usr/bin/env bash
# The compositor saves its own frames: shady.screenshot() from Lua and the
# IPC screenshot command, in the 3D renderer and in --safe (2D) mode.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
COMPOSITOR="$BUILD_DIR/shady"
CTL="$BUILD_DIR/shadyctl"
PROBE="$BUILD_DIR/headless-automation-probe"
for bin in "$COMPOSITOR" "$CTL" "$PROBE"; do
  [[ -x "$bin" ]] || { echo "screenshot: missing $bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT" SHADY_AUTOMATION_PROBE="$ROOT/$PROBE" SHOT_DIR="$tmp"
export SHADY_LUA_INIT="$ROOT/tests/headless-screenshot-init.lua"
export SHADY_CTL="$ROOT/$CTL"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

png_size() { python3 -c 'import struct,sys; d=open(sys.argv[1],"rb").read(24); assert d[:8]==b"\x89PNG\r\n\x1a\n"; print("%dx%d" % struct.unpack(">II", d[16:24]))' "$1"; }

for mode in spatial safe; do
  mkdir -m 700 "$tmp/rt-$mode"
  args=()
  [[ $mode == safe ]] && args=(--safe)
  set +e
  XDG_RUNTIME_DIR="$tmp/rt-$mode" SHOT_TAG="$mode" timeout 20s "$COMPOSITOR" "${args[@]}" \
    -c "$ROOT/tests/headless-screenshot-config.lua" >"$tmp/$mode.log" 2>&1
  rc=$?
  set -e
  fail() { tail -n 40 "$tmp/$mode.log" >&2; echo "screenshot[$mode]: FAIL $1" >&2; exit 1; }
  [[ $rc -eq 0 ]] || fail "status=$rc"
  grep -Eq 'AddressSanitizer|runtime error:|automation timer:' "$tmp/$mode.log" && fail "sanitizer or Lua error"
  grep -q 'screenshot-test: PASS' "$tmp/$mode.log" || fail "scenario did not complete"
  for shot in lua ipc; do
    f="$tmp/$mode-$shot.png"
    [[ -s $f ]] || fail "$shot capture missing"
    [[ "$(png_size "$f")" == 1280x720 ]] || fail "$shot capture is $(png_size "$f"), not 1280x720"
  done
  echo "screenshot[$mode]: PASS"
done
