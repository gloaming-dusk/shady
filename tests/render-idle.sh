#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${SHADY_DAILY_BUILD_DIR:-build-daily-driver}"
COMPOSITOR="$BUILD_DIR/shady"
if [[ ! -x "$COMPOSITOR" ]]; then
  echo "render-idle: $COMPOSITOR is missing; run ./daily-driver.sh build first" >&2
  exit 2
fi

tmp="$(mktemp -d)"
pid=""
cleanup() {
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT

runtime="$tmp/runtime"
mkdir -p "$runtime"
chmod 700 "$runtime"
log="$tmp/compositor.log"

export XDG_RUNTIME_DIR="$runtime"
export WLR_BACKENDS=headless
export WLR_HEADLESS_OUTPUTS=1
export WLR_RENDERER=pixman
export SHADY_RENDER_STATS=1
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

"$COMPOSITOR" --safe -c "$ROOT/tests/daily-driver-config.lua" >"$log" 2>&1 &
pid=$!

for _ in $(seq 1 100); do
  if grep -q 'Running Wayland compositor' "$log"; then
    break
  fi
  if ! kill -0 "$pid" 2>/dev/null; then
    cat "$log" >&2
    echo "render-idle: FAIL: compositor exited before becoming ready" >&2
    exit 1
  fi
  sleep 0.02
done

if ! grep -q 'Running Wayland compositor' "$log"; then
  cat "$log" >&2
  echo "render-idle: FAIL: compositor did not become ready" >&2
  exit 1
fi

# Leave the compositor completely idle. A static safe-mode desktop should not
# create a self-sustaining frame loop.
sleep 1

kill -TERM "$pid"
wait "$pid"
pid=""

cat "$log"

if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' "$log"; then
  echo "render-idle: FAIL: sanitizer diagnostic found" >&2
  exit 1
fi

stats="$(grep 'render-stats output=' "$log" | tail -n1 || true)"
if [[ -z "$stats" ]]; then
  echo "render-idle: FAIL: no render statistics were emitted" >&2
  exit 1
fi

requests="$(sed -n 's/.* requests=\([0-9][0-9]*\).*/\1/p' <<<"$stats")"
coalesced="$(sed -n 's/.* coalesced=\([0-9][0-9]*\).*/\1/p' <<<"$stats")"
frames="$(sed -n 's/.* frames=\([0-9][0-9]*\).*/\1/p' <<<"$stats")"

if [[ -z "$requests" || -z "$coalesced" || -z "$frames" ]]; then
  echo "render-idle: FAIL: malformed render statistics: $stats" >&2
  exit 1
fi

# One or two startup frames are acceptable for initial modeset/scene setup.
# A continuous loop would produce dozens of callbacks during the one-second idle.
if (( frames > 2 )); then
  echo "render-idle: FAIL: idle compositor rendered $frames frames" >&2
  exit 1
fi

echo "render-idle: PASS requests=$requests coalesced=$coalesced frames=$frames"
