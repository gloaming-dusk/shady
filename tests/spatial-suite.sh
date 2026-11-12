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
  ime-probe \
  math3d-test \
  physics-collision-test \
  libshady-plugin-fps-cube.so \
  libshady-plugin-fps-folded-paper.so \
  libshady-plugin-fps-origami.so \
  libshady-plugin-counter.so \
  libshady-plugin-counter-bad.so \
  libshady-plugin-motion-contract.so \
  libshady-plugin-focus-depth.so \
  libshady-plugin-border-accent.so \
  libshady-plugin-spatial-overview.so \
  libshady-plugin-rewind.so \
  libshady-plugin-close-slide-fade.so \
  libshady-plugin-afterglow.so \
  libshady-plugin-obj-loader.so \
  libshady-plugin-counter.so \
  shadyctl \
  ipc-json-test \
  protocol-probe \
  libshady-plugin-publish-probe.so \
  obj-loader-test

# Tests whose layer-shell client is shady-shell (its own project): skipped,
# not failed, when none is on PATH or in SHADY_SHELL_BIN.
with_shell() {
  local rc=0
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" nix develop -c bash "$1" || rc=$?
  if [[ $rc -eq 77 ]]; then echo "spatial-suite: skipped $1"; return 0; fi
  return $rc
}

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
  nix develop -c bash ./tests/headless-ipc.sh

with_shell ./tests/headless-layer-effect.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-standard-protocols.sh

with_shell ./tests/headless-layer-transform.sh

with_shell ./tests/headless-layer-below.sh

with_shell ./tests/headless-published-values.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-fps-toggle.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-focus-depth.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-spatial-overview.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-overview.sh

SHADY_CLOSE_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-close-effect-plugin.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-neon-overview.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-neon-quit.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-rewind.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-screenshot.sh

SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
  nix develop -c bash ./tests/headless-damage.sh

echo "spatial-suite: PASS"
