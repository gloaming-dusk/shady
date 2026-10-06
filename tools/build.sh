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
ninja -C build
