#!/usr/bin/env bash
set -euo pipefail

STATUS_FILE="${SHADY_TEST_STATUS:?SHADY_TEST_STATUS is required}"
COMPOSITOR_PID="$PPID"

finish() {
  kill -TERM "$COMPOSITOR_PID" 2>/dev/null || true
}
trap finish EXIT

if ! command -v foot >/dev/null 2>&1; then
  echo "FAIL: foot is required for the window lifecycle test" >"$STATUS_FILE"
  exit 1
fi

# Keep one real xdg-toplevel alive while changing output state.
foot --app-id=shady-daily-driver-probe sh -c 'sleep 4' &
probe=$!
sleep 0.5

# New toplevels are placed on server->outputs.next. With the headless backend,
# outputs are inserted at the front while wlr_output_layout_add_auto() lays each
# newly managed output to the right. Pick the rightmost output so the probe is
# guaranteed to exercise recovery instead of merely disabling an unused output.
# The test remains useful without wlr-randr, but records that transition as skipped.
if command -v wlr-randr >/dev/null 2>&1; then
  origin_output="$(
    wlr-randr 2>/dev/null | awk '
      /^[^[:space:]]/ { output=$1 }
      /^[[:space:]]+Position:/ {
        split($2, pos, ",")
        x = pos[1] + 0
        if (!found || x > max_x) {
          found = 1
          max_x = x
          chosen = output
        }
      }
      END { if (found) print chosen }
    '
  )"
  if [[ -n "$origin_output" ]]; then
    wlr-randr --output "$origin_output" --off
    echo "OUTPUT_DISABLE=PASS($origin_output)" >>"$STATUS_FILE"
    sleep 0.25
  else
    echo "OUTPUT_DISABLE=SKIP(no output at 0,0)" >>"$STATUS_FILE"
  fi
else
  echo "OUTPUT_DISABLE=SKIP(no wlr-randr)" >>"$STATUS_FILE"
fi

kill "$probe" 2>/dev/null || true
wait "$probe" 2>/dev/null || true

# Repeatedly create/map/unmap/destroy xdg-toplevels in small batches. This is
# intentionally modest so it is useful as a fast pre-commit sanitizer test.
for batch in 1 2 3 4 5; do
  pids=()
  for n in 1 2 3 4; do
    foot --app-id="shady-churn-${batch}-${n}" sh -c 'sleep 0.08' &
    pids+=("$!")
  done
  for pid in "${pids[@]}"; do
    wait "$pid"
  done
done

echo "WINDOW_CHURN=PASS" >>"$STATUS_FILE"
echo "PASS" >>"$STATUS_FILE"
