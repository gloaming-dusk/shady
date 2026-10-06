#!/usr/bin/env bash
set -euo pipefail

STATUS="${SHADY_ASTRAL_OUTPUT_STATUS:?SHADY_ASTRAL_OUTPUT_STATUS is required}"

listing=""
target=""
command_path="${SHADY_WLR_RANDR:-$(command -v wlr-randr || true)}"
if [[ -z "$command_path" ]]; then
  echo "OUTPUT_DISABLE=FAIL(no-wlr-randr)" >>"$STATUS"
  exit 1
fi
last_error=""
for _ in $(seq 1 20); do
  errfile="$(mktemp)"
  if listing="$("$command_path" 2>"$errfile")"; then
    last_error=""
  else
    last_error="$(cat "$errfile")"
  fi
  rm -f "$errfile"
  if [[ -n "$listing" ]]; then
    target="$(printf '%s\n' "$listing" | awk '
      /^[^[:space:]]/ { output=$1; if (first == "") first=output }
      /^[[:space:]]+Position: 0,0/ { print output; found=1; exit }
      END { if (!found && first != "") print first }
    ')"
    [[ -n "$target" ]] && break
  fi
  sleep 0.05
done

if [[ -z "$target" ]]; then
  echo "OUTPUT_DISABLE=FAIL(no-output)" >>"$STATUS"
  printf 'WLR_RANDR_PATH=%s\n' "$command_path" >>"$STATUS"
  printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY-}" >>"$STATUS"
  printf 'LAST_LISTING=%q\n' "$listing" >>"$STATUS"
  printf 'WLR_RANDR_ERROR=%q\n' "$last_error" >>"$STATUS"
  exit 1
fi

if ! "$command_path" --output "$target" --off; then
  echo "OUTPUT_DISABLE=FAIL(apply:$target)" >>"$STATUS"
  exit 1
fi

echo "OUTPUT_DISABLE=PASS($target)" >>"$STATUS"
