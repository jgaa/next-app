#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 SOURCE_FOLDER VOLUME_NAME OUTPUT.dmg" >&2
  exit 2
fi

source_folder="$(cd "$1" && pwd)"
volume_name="$2"
output_dir="$(cd "$(dirname "$3")" && pwd)"
output_path="${output_dir}/$(basename "$3")"

# hdiutil create -srcfolder mounts a temporary filesystem while copying files.
# On CI this can fail with EBUSY when another process touches the volume.
# Build HFS+ directly in userspace instead, then compress it without mounting.
# This is also the approach used by create-dmg's --sandbox-safe mode:
# https://github.com/create-dmg/create-dmg/blob/master/create-dmg
# Keep intermediates outside the source folder and on the output filesystem so
# the final rename is atomic. A failed attempt must not publish a partial DMG.
work_dir="$(mktemp -d "${output_dir}/.nextapp-dmg.XXXXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

hdiutil makehybrid -quiet -hfs \
  -hfs-volume-name "$volume_name" \
  -o "${work_dir}/contents.dmg" "$source_folder"
hdiutil convert -quiet "${work_dir}/contents.dmg" \
  -format UDZO -o "${work_dir}/package.dmg"
hdiutil verify -quiet "${work_dir}/package.dmg"
mv -f "${work_dir}/package.dmg" "$output_path"
