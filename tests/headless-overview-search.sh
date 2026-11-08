#!/usr/bin/env bash
# A tap of Super, then typing: the launcher opens with every key typed, and
# launches what they name. The second half does it again with an input method
# (tests/ime-probe.c grabbing the keyboard and giving the keys back, as fcitx5
# does): each must reach it once, then the launcher. Needs wtype (in the dev
# shell).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
for bin in shady shady-shell headless-automation-probe ime-probe; do
  [[ -x "$BUILD_DIR/$bin" ]] || { echo "overview-search: missing $BUILD_DIR/$bin" >&2; exit 2; }
done
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -m 700 "$tmp/runtime"
mkdir -p "$tmp/home" "$tmp/data/applications"
log="$tmp/log"
for app in eta zeta; do
  printf '#!/bin/sh\nSHADY_AUTOMATION_PROBE_APP_ID=launched-%s exec %q\n' "$app" "$ROOT/$BUILD_DIR/headless-automation-probe" >"$tmp/$app"
  chmod +x "$tmp/$app"
  printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\n' "$app" "$tmp/$app" >"$tmp/data/applications/$app.desktop"
done
set +e
(
  export XDG_RUNTIME_DIR="$tmp/runtime" HOME="$tmp/home"
  unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
  export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
  export XDG_DATA_HOME="$tmp/data" XDG_DATA_DIRS="$tmp/data"
  export SHADY_ROOT="$ROOT" SHADY_LUA_INIT="$ROOT/tests/headless-overview-search-init.lua"
  export SHADY_SHELL_BIN="$ROOT/$BUILD_DIR/shady-shell"
  export OVERVIEW_IME_PROBE="$ROOT/$BUILD_DIR/ime-probe" OVERVIEW_IME_LOG="$tmp/ime"
  export TIDE_IDLE_LOCK=0 TIDE_IDLE_SCREEN_OFF=0
  export SHADY_SHELL_CONFIG="$ROOT/rices/tide/shell.lua"
  export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
  export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
  timeout 40s "$BUILD_DIR/shady" -c "$ROOT/rices/tide/config.lua"
) >"$log" 2>&1
rc=$?
set -e
if [[ $rc -ne 0 ]] || grep -Eq 'AddressSanitizer|runtime error:' "$log" ||
    ! grep -q 'overview-search: PASS' "$log"; then
  grep -E 'overview-search|shady-shell:|lua:|SHADY LUA|AddressSanitizer|runtime error' "$log" | tail -n 40
  echo "overview-search: FAIL (status $rc)" >&2
  exit 1
fi
echo "overview-search: PASS"
