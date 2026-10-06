#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"
BUILD_TYPE="${SHADY_SPATIAL_BUILD_TYPE:-debug}"
SANITIZE="${SHADY_SPATIAL_SANITIZE:-address,undefined}"

configure() {
  if [[ -f "$BUILD_DIR/build.ninja" ]]; then
    nix develop -c meson setup --reconfigure "$BUILD_DIR" \
      -Dspatial=enabled \
      -Dlua=enabled \
      -Dphysics=enabled \
      -Dfps=enabled \
      -Dwindow_motion=enabled \
      -Dclose_animation=enabled \
      -Dscene_effects=enabled \
      -Dshell=enabled \
      -Dbuildtype="$BUILD_TYPE" \
      -Db_sanitize="$SANITIZE"
  else
    nix develop -c meson setup "$BUILD_DIR" \
      -Dspatial=enabled \
      -Dlua=enabled \
      -Dphysics=enabled \
      -Dfps=enabled \
      -Dwindow_motion=enabled \
      -Dclose_animation=enabled \
      -Dscene_effects=enabled \
      -Dshell=enabled \
      -Dbuildtype="$BUILD_TYPE" \
      -Db_sanitize="$SANITIZE"
  fi
}

build() {
  configure
  nix develop -c ninja -C "$BUILD_DIR" \
    shady \
    headless-automation-probe \
    libshady-plugin-window-motion.so \
    libshady-plugin-counter.so \
    libshady-plugin-counter-bad.so
}

test_fast() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-spatial-daily.sh
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-spatial-output-recovery.sh
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-spatial-plugin-rollback.sh
}

case "${1:-all}" in
  configure) configure ;;
  build) build ;;
  test) test_fast ;;
  all) build; test_fast ;;
  *)
    echo "usage: $0 [configure|build|test|all]" >&2
    exit 2
    ;;
esac
