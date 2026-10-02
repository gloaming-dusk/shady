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
}

test_daily() {
  SHADY_DAILY_BUILD_DIR="$BUILD_DIR" ./tests/daily-driver-smoke.sh
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
