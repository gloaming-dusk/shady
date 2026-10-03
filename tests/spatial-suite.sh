#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  nix develop -c meson setup "$BUILD_DIR" \
    -Dbuildtype=debug \
    -Db_sanitize=address,undefined
else
  nix develop -c meson setup --reconfigure "$BUILD_DIR"
fi

nix develop -c ninja -C "$BUILD_DIR" \
  shady \
  headless-automation-probe \
  math3d-test \
  physics-collision-test \
  libshady-plugin-fps-cube.so \
  libshady-plugin-fps-folded-paper.so \
  libshady-plugin-fps-origami.so \
  libshady-plugin-motion-contract.so \
  libshady-plugin-focus-depth.so \
  libshady-plugin-spatial-overview.so \
  libshady-plugin-water-windows.so \
  libshady-plugin-close-slide-fade.so

export SHADY_FPS_REPRESENTATION_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-fps-cube.so"

nix develop -c meson test -C "$BUILD_DIR" \
  math3d physics-collision motion-plugin --print-errorlogs

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-motion.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-representation-lifetime.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-fps-toggle.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-focus-depth.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-spatial-overview.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-water-windows.sh

SHADY_CLOSE_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-close-effect-plugin.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-neon-water.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-neon-overview.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-neon-quit.sh

echo "spatial-suite: PASS"
