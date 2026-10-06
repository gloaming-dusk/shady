#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -x build/shady ]]; then
  echo "Shady: build/shady is missing. Run ./tools/build.sh first." >&2
  exit 1
fi

if ! usage="$("$ROOT/build/shady" -h 2>&1)"; then
  echo "$usage" >&2
  echo "Shady native: could not inspect build/shady. Rebuild with ./tools/build.sh." >&2
  exit 1
fi
if [[ "$usage" != *"--native"* ]]; then
  echo "Shady native: build/shady is outdated and does not support --native." >&2
  echo "Run ./tools/build.sh in the development shell, then launch this script again." >&2
  exit 1
fi

if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
  echo "Shady native: XDG_RUNTIME_DIR is not set." >&2
  echo "Start from a normal local TTY login session (PAM/systemd-logind usually sets it)." >&2
  exit 1
fi

TTY_PATH="$(tty 2>/dev/null || true)"
if [[ ! "$TTY_PATH" =~ ^/dev/tty[0-9]+$ ]]; then
  echo "Shady native: warning: current terminal is '$TTY_PATH', not a Linux VT (/dev/ttyN)." >&2
  echo "For DRM/libinput testing, switch to a real TTY (for example Ctrl+Alt+F3) and log in there." >&2
fi

unset WAYLAND_DISPLAY WAYLAND_SOCKET DISPLAY
export WLR_BACKENDS="drm,libinput"

exec "$ROOT/build/shady" --native "$@"
