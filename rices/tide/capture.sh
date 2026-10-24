#!/usr/bin/env bash
# Regenerate Tide's screenshots: preview.jpg (the strip with a notification),
# control.jpg, launcher.jpg, overview.jpg and lock.jpg.
#
# Needs a build in build/ and a GPU. Run from the repository root:
#   nix develop -c bash rices/tide/capture.sh
# foot, grim and ImageMagick come from nixpkgs. The shell runs on a private
# session bus, with Horizon's stand-ins for nmcli, wpctl and friends and its
# fixed set of applications. (Battery and uptime are the machine's own.)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
RICE="$ROOT/rices/tide"
for bin in shady shady-shell; do
    [[ -e "build/$bin" ]] || { echo "capture: build/$bin is missing; run ./tools/build.sh" >&2; exit 1; }
done
if [[ -z "${TIDE_CAPTURE_BUS:-}" ]]; then
    exec env TIDE_CAPTURE_BUS=1 dbus-run-session -- "$0" "$@"
fi
tools() { env -u LD_LIBRARY_PATH nix shell nixpkgs#foot nixpkgs#grim nixpkgs#imagemagick -c "$@"; }
work="$(mktemp -d /tmp/tide-capture.XXXXXX)"
trap 'rm -rf "$work"' EXIT
mkdir -m 700 "$work/runtime" "$work/home"
(
    export XDG_RUNTIME_DIR="$work/runtime" HOME="$work/home" USER=you
    unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
    export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
    export SHADY_ROOT="$ROOT" SHADY_LUA_INIT="$RICE/capture/init.lua"
    export SHADY_SHELL_BIN="$ROOT/build/shady-shell" CAPTURE_OUT="$work"
    export CAPTURE_FOOT_INI="$ROOT/rices/afterglow/capture/foot.ini"
    export PATH="$ROOT/rices/horizon/capture/bin:$PATH"
    export XDG_DATA_HOME="$work/data" XDG_DATA_DIRS="$ROOT/rices/horizon/capture"
    source "$RICE/theme.sh"
    export SHADY_SHELL_CONFIG="$RICE/shell.lua"
    tools build/shady -c "$RICE/config.lua"
) >"$work/capture.log" 2>&1 || { tail -n 40 "$work/capture.log" >&2; exit 1; }
for name in preview control launcher overview lock; do
    [[ -s "$work/$name.png" ]] || { echo "capture: $name.png is missing" >&2; tail -n 40 "$work/capture.log" >&2; exit 1; }
    tools magick "$work/$name.png" -quality 90 "$RICE/$name.jpg"
done
echo "capture: done"
