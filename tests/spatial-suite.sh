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
  headless-session-lock-probe \
  math3d-test \
  physics-collision-test \
  libshady-plugin-fps-cube.so \
  libshady-plugin-fps-folded-paper.so \
  libshady-plugin-fps-origami.so \
  libshady-plugin-counter.so \
  libshady-plugin-counter-bad.so \
  libshady-plugin-motion-contract.so \
  libshady-plugin-focus-depth.so \
  libshady-plugin-spatial-overview.so \
  libshady-plugin-water-windows.so \
  libshady-plugin-close-slide-fade.so \
  libshady-plugin-afterglow.so \
  libshady-plugin-obj-loader.so \
  libshady-plugin-counter.so \
  libshady-plugin-black-hole.so \
  libshady-plugin-frozen-window.so \
  shadyctl \
  ipc-json-test \
  shady-shell \
  protocol-probe \
  libshady-plugin-publish-probe.so \
  obj-loader-test

export SHADY_FPS_REPRESENTATION_PLUGIN="$ROOT/$BUILD_DIR/libshady-plugin-fps-cube.so"

nix develop -c meson test -C "$BUILD_DIR" \
  math3d physics-collision motion-plugin obj-loader ipc-json --print-errorlogs

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-spatial-daily.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-spatial-output-recovery.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-spatial-plugin-rollback.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-motion.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-representation-lifetime.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-environment.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-plugin-manager.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-black-hole.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-frozen-window.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-ipc.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-layer-effect.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-standard-protocols.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-layer-transform.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-published-values.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-afterglow-shell.sh

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

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-afterglow.sh

echo "spatial-suite: PASS"
