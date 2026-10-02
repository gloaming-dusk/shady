#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [[ ! -d build ]]; then
  echo "Shady: build/ does not exist. Run ./build.sh first." >&2
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
