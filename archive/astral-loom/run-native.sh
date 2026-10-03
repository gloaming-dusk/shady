#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
export SHADY_ASTRAL_NATIVE=1
export SHADY_ASTRAL_BUILD_DIR="$ROOT/build"
exec "$ROOT/examples/rice/astral-loom/run.sh" "$@"
