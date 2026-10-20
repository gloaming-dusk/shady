#!/usr/bin/env bash
# Regenerate Horizon's screenshots and hero video from the live rice:
#   preview.jpg         the control center at the afterglow hour, as on the
#                       README and website
#   hour-*.jpg          the four hours
#   notification.jpg, launcher.jpg, control.jpg
#   hero.mp4/webm/jpg   the 17.1 s website loop and its poster
#
# Needs a build in build/ and a GPU. Run from the repository root:
#   nix develop -c bash rices/horizon/capture.sh
# wf-recorder, ImageMagick and ffmpeg come from nixpkgs. The shell runs on a
# private session bus, with stand-ins for nmcli, wpctl and friends and a
# fixed set of applications (capture/), so nothing of this machine shows.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
RICE="$ROOT/rices/horizon"
for bin in shady shady-shell libshady-plugin-afterglow.so; do
    [[ -e "build/$bin" ]] || { echo "capture: build/$bin is missing; run ./tools/build.sh" >&2; exit 1; }
done
for tool in dbus-run-session busctl foot; do
    command -v "$tool" >/dev/null || { echo "capture: needs $tool" >&2; exit 1; }
done
if [[ -z "${HORIZON_CAPTURE_BUS:-}" ]]; then
    exec env HORIZON_CAPTURE_BUS=1 dbus-run-session -- "$0" "$@"
fi

tools() { env -u LD_LIBRARY_PATH nix shell nixpkgs#wf-recorder nixpkgs#imagemagick nixpkgs#ffmpeg -c "$@"; }
# Short: the Wayland socket path must fit in 108 bytes.
work="$(mktemp -d /tmp/horizon-capture.XXXXXX)"
trap 'rm -rf "$work"' EXIT

# run <mode>: start the rice headless in a fresh runtime dir.
run() {
    local mode="$1"
    rm -rf "$work/runtime" "$work/data" && mkdir -m 700 "$work/runtime" && mkdir "$work/data"
    : >"$work/status"
    (
        export XDG_RUNTIME_DIR="$work/runtime"
        unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
        export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
        export SHADY_ROOT="$ROOT" SHADY_AFTERGLOW_PHASE="golden hour"
        export SHADY_LUA_INIT="$RICE/capture/init.lua"
        export SHADY_SHELL_BIN="$ROOT/build/shady-shell"
        export CAPTURE_MODE="$mode" CAPTURE_OUT="$work" CAPTURE_STATUS="$work/status"
        export CAPTURE_FOOT_INI="$ROOT/rices/afterglow/capture/foot.ini"
        export PATH="$RICE/capture/bin:$PATH"
        export XDG_DATA_HOME="$work/data" XDG_DATA_DIRS="$RICE/capture"
        source "$ROOT/rices/afterglow/theme.sh"
        export SHADY_SHELL_CONFIG="$RICE/shell.lua"
        exec build/shady -c "$RICE/config.lua"
    ) >"$work/$mode.log" 2>&1 &
    echo $!
}
wait_mark() {
    for _ in $(seq 1 3000); do
        grep -q "^$1$" "$work/status" && return 0
        sleep 0.02
    done
    echo "capture: timed out waiting for $1" >&2
    tail -n 40 "$work"/*.log >&2
    exit 1
}

echo "capture: stills"
pid="$(run stills)"
wait_mark DONE
wait "$pid" 2>/dev/null || true
for name in hour-golden-hour hour-afterglow hour-blue-hour hour-night notification launcher control; do
    tools magick "$work/$name.png" -quality 90 "$RICE/$name.jpg"
done
cp "$RICE/control.jpg" "$RICE/preview.jpg"

echo "capture: hero loop"
pid="$(run video)"
wait_mark READY
XDG_RUNTIME_DIR="$work/runtime" WAYLAND_DISPLAY=wayland-0 \
    tools wf-recorder -y -c libx264 -p preset=ultrafast -p crf=8 -m matroska \
        -f "$work/raw.mkv" >"$work/recorder.log" 2>&1 &
recorder=$!
sleep 0.5
echo RECORDING >>"$work/status"
wait_mark DONE
kill -INT "$recorder"
wait "$recorder" || true
wait "$pid" 2>/dev/null || true
# Recording started 0.5 s before the first key press was scheduled; keep
# the loop from that moment, at a constant 30 fps.
tools ffmpeg -v error -y -ss 0.5 -i "$work/raw.mkv" -t 17.1 -vf fps=30,format=yuv420p \
    -c:v libx264 -preset slow -crf 23 -movflags +faststart -an "$RICE/hero.mp4"
tools ffmpeg -v error -y -ss 0.5 -i "$work/raw.mkv" -t 17.1 -vf fps=30,format=yuv420p \
    -c:v libvpx-vp9 -b:v 0 -crf 36 -row-mt 1 -an "$RICE/hero.webm"
tools ffmpeg -v error -y -i "$RICE/hero.mp4" -frames:v 1 -q:v 3 "$RICE/hero.jpg"
echo "capture: done"
