#!/usr/bin/env bash
# Damage tracking draws the same pixels as drawing everything: the same
# scenario (tests/headless-damage-init.lua: a ticking wallpaper behind opaque
# windows, ticking windows, a frosted bar, focus changes, a fractional scale)
# is run with damage tracking and with SHADY_DAMAGE=0, and the saved frames
# must match. The tracked run must also have drawn partial frames.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_SPATIAL_TEST_BUILD_DIR:-build}"
COMPOSITOR="$BUILD_DIR/shady"
PROBE="$BUILD_DIR/headless-automation-probe"
for bin in "$COMPOSITOR" "$PROBE"; do
  [[ -x "$bin" ]] || { echo "damage: missing $bin" >&2; exit 2; }
done

tmp="$(mktemp -d)"
trap '[[ -n "${SHADY_DAMAGE_LOGS:-}" ]] && cp "$tmp"/*.log "$SHADY_DAMAGE_LOGS"/; rm -rf "$tmp"' EXIT

export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2 LIBGL_ALWAYS_SOFTWARE=1
export SHADY_ROOT="$ROOT" SHADY_AUTOMATION_PROBE="$ROOT/$PROBE" SHADY_RENDER_STATS=1
export SHADY_LUA_INIT="$ROOT/tests/headless-damage-init.lua"
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

run() {
  local mode=$1; shift
  mkdir -m 700 "$tmp/rt-$mode"
  set +e
  env "$@" XDG_RUNTIME_DIR="$tmp/rt-$mode" SHOT_PATH="$tmp/$mode.png" timeout 30s \
    "$COMPOSITOR" -c "$ROOT/tests/headless-damage-config.lua" >"$tmp/$mode.log" 2>&1
  local rc=$?
  set -e
  fail() { tail -n 40 "$tmp/$mode.log" >&2; echo "damage[$mode]: FAIL $1" >&2; exit 1; }
  [[ $rc -eq 0 ]] || fail "status=$rc"
  grep -Eq 'AddressSanitizer|runtime error:|automation timer:' "$tmp/$mode.log" && fail "sanitizer or Lua error"
  grep -q 'damage-test: DONE' "$tmp/$mode.log" || fail "scenario did not complete"
  [[ -s "$tmp/$mode.png" ]] || fail "no capture"
}

run tracked
run whole SHADY_DAMAGE=0

stats="$(grep -o 'render-damage .*' "$tmp/tracked.log" | head -n1)"
echo "damage: $stats"
partial="$(sed -n 's/.* partial=\([0-9]*\) .*/\1/p' <<<"$stats")"
[[ -n "$partial" && "$partial" -gt 20 ]] || { echo "damage: FAIL too few partial frames ($stats)" >&2; exit 1; }
grep -q 'render-damage .* partial=0 ' "$tmp/whole.log" || { echo "damage: FAIL SHADY_DAMAGE=0 drew partial frames" >&2; exit 1; }

python3 "$ROOT/tests/png-compare.py" "$tmp/whole.png" "$tmp/tracked.png" || {
  [[ -n "${SHADY_DAMAGE_KEEP:-}" ]] && cp "$tmp/whole.png" "$tmp/tracked.png" "$SHADY_DAMAGE_KEEP"/
  echo "damage: FAIL the tracked frame differs from the whole one" >&2
  exit 1
}
echo "damage: PASS"
