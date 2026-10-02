#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"

configure() {
  if [[ -f "$BUILD_DIR/build.ninja" ]]; then
    meson setup --reconfigure "$BUILD_DIR" \
      -Dspatial=disabled \
      -Dlua=enabled \
      -Dphysics=disabled \
      -Dfps=disabled \
      -Dwindow_motion=disabled \
      -Dclose_animation=disabled \
      -Dscene_effects=disabled \
      -Dshell=enabled \
      -Dbuildtype=debug \
      -Db_sanitize=address,undefined
  else
    meson setup "$BUILD_DIR" \
      -Dspatial=disabled \
      -Dlua=enabled \
      -Dphysics=disabled \
      -Dfps=disabled \
      -Dwindow_motion=disabled \
      -Dclose_animation=disabled \
      -Dscene_effects=disabled \
      -Dshell=enabled \
      -Dbuildtype=debug \
      -Db_sanitize=address,undefined
  fi
}

build() {
  configure
  ninja -C "$BUILD_DIR"
  ninja -C "$BUILD_DIR" libshady-plugin-counter.so libshady-plugin-counter-bad.so
}

test_daily() {
  meson test -C "$BUILD_DIR" module-resolver math3d physics-collision --print-errorlogs
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" ./tests/daily-driver-smoke.sh
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" ./tests/render-idle.sh
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" bash ./tests/startup-cleanup.sh
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" bash ./tests/plugin-reload.sh
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" bash ./tests/plugin-rollback.sh
}

case "${1:-all}" in
  configure) configure ;;
  build) build ;;
  test) test_daily ;;
  all) build; test_daily ;;
  *)
    echo "usage: $0 [configure|build|test|all]" >&2
    exit 2
    ;;
esac
