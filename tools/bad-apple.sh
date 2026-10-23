#!/usr/bin/env bash
# Turn a video into frames for the bad-apple plugin (Super+A).
#
#   tools/bad-apple.sh <video> [columns] [rows]
#
# Writes frames.bin (1 bit per tile) and audio.wav to
# ${SHADY_BAD_APPLE_DIR:-${XDG_DATA_HOME:-~/.local/share}/shady/bad-apple}.
# The grid defaults to 80x60, the 4:3 of the original video. No video ships
# with Shady; use your own copy. Needs ffmpeg and ffprobe
# (`nix shell nixpkgs#ffmpeg`).
#
# frames.bin is one text line,
#   shady-bad-apple 1 <columns> <rows> <fps numerator> <fps denominator>
# followed by every frame as ffmpeg's monow: rows top to bottom, each padded
# to whole bytes, most significant bit first, 1 for black.
set -euo pipefail

if [[ $# -lt 1 || ! -f "$1" ]]; then
  echo "usage: $0 <video> [columns] [rows]" >&2
  exit 1
fi
video="$1"
cols="${2:-80}"
rows="${3:-60}"
for tool in ffmpeg ffprobe; do
  command -v "$tool" >/dev/null || { echo "bad-apple: needs $tool (nix shell nixpkgs#ffmpeg)" >&2; exit 1; }
done

out="${SHADY_BAD_APPLE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/shady/bad-apple}"
mkdir -p "$out"

rate="$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate \
  -of default=noprint_wrappers=1:nokey=1 "$video")"
num="${rate%/*}"
den="${rate#*/}"
[[ "$num" =~ ^[0-9]+$ && "$den" =~ ^[0-9]+$ && "$num" -gt 0 && "$den" -gt 0 ]] || {
  echo "bad-apple: cannot read the frame rate of $video ($rate)" >&2
  exit 1
}

# Area scaling averages each tile; the threshold then makes it black or
# white, so no dithering noise reaches the tiles.
tmp="$out/frames.bin.tmp"
printf 'shady-bad-apple 1 %d %d %d %d\n' "$cols" "$rows" "$num" "$den" >"$tmp"
ffmpeg -v error -i "$video" -an \
  -vf "scale=${cols}:${rows}:flags=area,format=gray,lut=y='if(gt(val,127),255,0)'" \
  -pix_fmt monow -f rawvideo - >>"$tmp"
mv "$tmp" "$out/frames.bin"

if ffprobe -v error -select_streams a:0 -show_entries stream=index -of csv=p=0 "$video" | grep -q .; then
  ffmpeg -v error -y -i "$video" -vn -ac 2 -ar 48000 -c:a pcm_s16le "$out/audio.wav"
else
  rm -f "$out/audio.wav"
  echo "bad-apple: $video has no audio; the frames will play silently" >&2
fi

bytes=$(( ($(stat -c %s "$out/frames.bin") - $(head -n1 "$out/frames.bin" | wc -c)) ))
per_frame=$(( (cols + 7) / 8 * rows ))
echo "bad-apple: $(( bytes / per_frame )) frames of ${cols}x${rows} at $num/$den fps in $out"
