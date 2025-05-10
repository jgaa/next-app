#!/usr/bin/env bash

## On mac: brew install librsvg

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <path-to-svg>"
  exit 1
fi
SVG="$1"
[[ -f "$SVG" ]] || { echo "File not found: $SVG"; exit 1; }
BASE="$(basename "$SVG" .svg)"
ICONSET="${BASE}.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"

sizes=(16 32 128 256 512)
for s in "${sizes[@]}"; do
  # normal
  rsvg-convert -w $s -h $s "$SVG" -o "$ICONSET/icon_${s}x${s}.png"
  # retina
  r=$((s*2))
  rsvg-convert -w $r -h $r "$SVG" -o "$ICONSET/icon_${s}x${s}@2x.png"
done

echo "✅ iconset created: $ICONSET"
echo "iconutil -c icns \"$ICONSET\"  →  ${BASE}.icns"
