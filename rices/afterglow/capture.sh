#!/usr/bin/env bash
# Regenerate Afterglow's screenshots and hero video from the live rice:
#   preview.jpg   the afterglow hour, as on the README and website
#   phases.jpg    the four hours, 2x2
#   hero.mp4/webm/jpg  the 17.1 s website loop and its poster
#
# Needs a build in build/ and a GPU. Run from the repository root:
#   nix develop -c bash rices/afterglow/capture.sh
# grim, foot, wf-recorder, ImageMagick and ffmpeg come from nixpkgs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
RICE="$ROOT/rices/afterglow"
for bin in shady shady-shell libshady-plugin-afterglow.so; do
    [[ -e "build/$bin" ]] || { echo "capture: build/$bin is missing; run ./tools/build.sh" >&2; exit 1; }
done

tools() { env -u LD_LIBRARY_PATH nix shell nixpkgs#wf-recorder nixpkgs#imagemagick nixpkgs#ffmpeg -c "$@"; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# run <mode>: start the rice headless in a fresh runtime dir.
run() {
    local mode="$1"
    rm -rf "$work/runtime" && mkdir -m 700 "$work/runtime"
    : >"$work/status"
    (
        export XDG_RUNTIME_DIR="$work/runtime"
        unset WAYLAND_DISPLAY WAYLAND_SOCKET SHADY_SOCKET
        export WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=gles2
        export SHADY_ROOT="$ROOT" SHADY_AFTERGLOW_PHASE="golden hour"
        export SHADY_LUA_INIT="$RICE/capture/init.lua"
        export SHADY_SHELL_BIN="$ROOT/build/shady-shell"
        export CAPTURE_MODE="$mode" CAPTURE_OUT="$work" CAPTURE_STATUS="$work/status"
        export CAPTURE_FOOT_INI="$RICE/capture/foot.ini"
        source "$RICE/theme.sh"
        export SHADY_SHELL_CONFIG="$RICE/shell.lua"
        exec build/shady -c "$RICE/config.lua"
    ) >"$work/$mode.log" 2>&1 &
    echo $!
}
wait_mark() {
    for _ in $(seq 1 1500); do
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
tools magick "$work/hour-afterglow.png" -quality 90 "$RICE/preview.jpg"
tools magick \( "$work/hour-golden-hour.png" "$work/hour-afterglow.png" -resize 640x360 +append \) \
    \( "$work/hour-blue-hour.png" "$work/hour-night.png" -resize 640x360 +append \) \
    -append -quality 90 "$RICE/phases.jpg"

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
