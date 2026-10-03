#!/usr/bin/env bash
set -euo pipefail

STATUS_FILE="${SHADY_SPATIAL_OUTPUT_STATUS:?SHADY_SPATIAL_OUTPUT_STATUS is required}"
PROBE="${SHADY_AUTOMATION_PROBE:?SHADY_AUTOMATION_PROBE is required}"

SHADY_AUTOMATION_PROBE_APP_ID=spatial-output-probe "$PROBE" &
probe_pid=$!
trap 'kill "$probe_pid" 2>/dev/null || true' EXIT
sleep 0.35

target_output="$(wlr-randr 2>/dev/null | awk '
  /^[^[:space:]]/ { print $1; exit }
')"
if [[ -z "$target_output" ]]; then
  echo "FAIL=no-output" >"$STATUS_FILE"
  exit 1
fi

wlr-randr --output "$target_output" --off
echo "OUTPUT_DISABLE=PASS($target_output)" >>"$STATUS_FILE"
sleep 0.35

kill "$probe_pid" 2>/dev/null || true
wait "$probe_pid" 2>/dev/null || true
probe_pid=""
echo "PASS" >>"$STATUS_FILE"
