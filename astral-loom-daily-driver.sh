#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build-spatial-asan}"

configure() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" bash ./spatial-daily-driver.sh configure
}

build() {
  configure
  nix develop -c ninja -C "$BUILD_DIR" \
    shady \
    headless-automation-probe \
    headless-session-lock-probe \
    libshady-plugin-astral-loom.so
}

test_daily() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-astral-loom-daily.sh
}

test_stress() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-astral-loom-stress.sh
}

test_input() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-astral-input-focus-stress.sh
}

test_reload() {
  SHADY_SPATIAL_TEST_BUILD_DIR="$BUILD_DIR" \
    nix develop -c bash ./tests/headless-astral-reload-stress.sh
}

case "${1:-all}" in
  configure) configure ;;
  build) build ;;
  test) test_daily ;;
  stress) test_stress ;;
  input) test_input ;;
  reload) test_reload ;;
  all) build; test_daily; test_stress; test_input; test_reload ;;
  *)
    echo "usage: $0 [configure|build|test|stress|input|reload|all]" >&2
    exit 2
    ;;
esac
