#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

setup_args=()
if [[ -n "${PKG_CONFIG_PATH:-}" ]]; then
  setup_args+=("-Dpkg_config_path=$PKG_CONFIG_PATH")
fi

if [[ -f build/build.ninja ]]; then
  meson setup --reconfigure --clearcache build "${setup_args[@]}"
else
  meson setup build "${setup_args[@]}"
fi
# Set after the reconfigure: a build directory made before an option
# existed rejects it on the reconfigure itself.
if [[ -n "${SHADY_CURSOR_PATH:-}" ]]; then
  meson configure build "-Dcursor_path=$SHADY_CURSOR_PATH"
fi
ninja -C build
