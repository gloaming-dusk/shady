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

case "${1:-all}" in
  configure) configure ;;
  build) build ;;
  test) test_daily ;;
  stress) test_stress ;;
  all) build; test_daily; test_stress ;;
  *)
    echo "usage: $0 [configure|build|test|stress|all]" >&2
    exit 2
    ;;
esac
