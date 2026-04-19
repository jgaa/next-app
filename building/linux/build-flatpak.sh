#!/usr/bin/env bash
set -eo pipefail

cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
manifest_dir=$(pwd)

export BUILD_DIR="${BUILD_DIR:-/var/local/build}"
export APP_BUILD_DIR="${APP_BUILD_DIR:-${BUILD_DIR}/next-app-linux}"
export ASSETS_DIR=${ASSETS_DIR:-${APP_BUILD_DIR}/assets}

src_dir="$(realpath "$(pwd)/../../")"
REPO_DIR="${REPO_DIR:-${APP_BUILD_DIR}/flatpak-repo}"
APP_ID="eu.lastviking.NextApp"
MANIFEST="${APP_BUILD_DIR}/${APP_ID}.yml"
FLATPAK_RUNTIME_VERSION=25.08
FP_BUILD_DIR=${APP_BUILD_DIR}/flatpak-build

echo SRC_DIR: ${src_dir}

cd ${APP_BUILD_DIR}

MAIN_BINARY="${ASSETS_DIR}/nextapp"
METAINFO_FILE="${src_dir}/flatpak/${APP_ID}.metainfo.xml"
if [[ ! -f "$MAIN_BINARY" ]]; then
  echo "Error: $MAIN_BINARY not found. The script won't find dependencies for your main app."
  exit 1
fi

if [[ ! -f "$METAINFO_FILE" ]]; then
  echo "Error: $METAINFO_FILE not found."
  exit 1
fi

echo "Nextapp dependencies:"
ldd "${MAIN_BINARY}"

############################
# Parse version from CMakeLists.txt
############################
CMAKEFILE="${src_dir}/CMakeLists.txt"
echo CMAKEFILE: ${CMAKEFILE}
VERSION="$(awk '
  match($0, /^[[:space:]]*set[[:space:]]*\([[:space:]]*NEXTAPP_VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)/, m) {
    print m[1]
    exit
  }
' "$CMAKEFILE")"

echo VERSION: ${VERSION}

############################
# Generate the .desktop file
############################
cat << 'EOF' > eu.lastviking.NextApp.desktop
[Desktop Entry]
Name=NextApp
Comment=Your Personal Organizer
Exec=nextapp
Icon=eu.lastviking.NextApp
Terminal=false
Type=Application
Categories=Utility;
EOF

############################
# Recursive function to find missing libraries
############################
declare -A PROCESSED_ITEMS=()
declare -A FOUND_LIBS=()
MISSING_LIBS=()

add_missing_libs() {
  local file="$1"
  if [[ -n "${PROCESSED_ITEMS["$file"]}" ]]; then
    return
  fi
  PROCESSED_ITEMS["$file"]=1

  local lines
  mapfile -t lines < <(ldd "$file" 2>/dev/null | grep "not found")

  for line in "${lines[@]}"; do
    local LIBNAME
    LIBNAME="$(echo "$line" | awk '{print $1}')"

    if [[ -n "${FOUND_LIBS["$LIBNAME"]}" ]]; then
      continue
    fi

    local HOSTFILE
    HOSTFILE="$(find /usr/lib/x86_64-linux-gnu/ -maxdepth 1 -name "$LIBNAME*" -type f 2>/dev/null | head -n1)"
    if [[ -n "$HOSTFILE" ]]; then
      FOUND_LIBS["$LIBNAME"]="$HOSTFILE"
      MISSING_LIBS+=( "$LIBNAME" )
      add_missing_libs "$HOSTFILE"
    fi
  done
}

add_missing_libs "$MAIN_BINARY"

echo "MISSING_LIBS: ${MISSING_LIBS}"

############################
# Write out the final Flatpak manifest
############################
cat << EOF > ${MANIFEST}
{
  "app-id": "eu.lastviking.NextApp",
  "runtime": "org.freedesktop.Platform",
  "runtime-version": "${FLATPAK_RUNTIME_VERSION}",
  "sdk": "org.freedesktop.Sdk",
  "command": "nextapp",
  "version": "$VERSION",

  "finish-args": [
    "--share=network",
    "--socket=x11",
    "--socket=wayland",
    "--device=dri",
    "--share=ipc",
    "--socket=session-bus",
    "--socket=system-bus",
    "--socket=pulseaudio",
    "--filesystem=home/NextApp:create"
  ],

  "modules": [
    {
      "name": "nextapp",
      "buildsystem": "simple",
      "build-commands": [
        "install -Dm755 nextapp /app/bin/nextapp"
      ],
      "sources": [
        {
          "type": "file",
          "path": "${MAIN_BINARY}"
        }
      ]
    },
    {
      "name": "resources",
      "buildsystem": "simple",
      "build-commands": [
        "install -Dm644 nextapp.svg /app/share/icons/hicolor/scalable/apps/eu.lastviking.NextApp.svg",
        "install -Dm644 eu.lastviking.NextApp.desktop /app/share/applications/eu.lastviking.NextApp.desktop",
        "install -Dm644 eu.lastviking.NextApp.metainfo.xml /app/share/metainfo/eu.lastviking.NextApp.metainfo.xml"
      ],
      "sources": [
        {
          "type": "file",
          "path": "${src_dir}/src/NextAppUi/icons/nextapp.svg"
        },
        {
          "type": "file",
          "path": "${APP_BUILD_DIR}/eu.lastviking.NextApp.desktop"
        },
        {
          "type": "file",
          "path": "${METAINFO_FILE}"
        }
      ]
    }
  ]
}
EOF

############################
# Print summary
############################
echo "-------------------------------------------------------"
echo "Generated eu.lastviking.NextApp.json (version=$VERSION)"
if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
  echo "Bundling missing libs (recursive search):"
  for lib in "${MISSING_LIBS[@]}"; do
    echo "  $lib => ${FOUND_LIBS["$lib"]}"
  done
else
  echo "No missing libraries detected (or none were found on host)."
fi

#  Build the Flatpak
if [[ -d "flatpak" ]]; then
  rm -rf flatpak/*
fi

############################
# Build the flatpak
############################

flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install -y --user flathub org.freedesktop.Sdk//${FLATPAK_RUNTIME_VERSION} org.freedesktop.Platform//${FLATPAK_RUNTIME_VERSION}
flatpak-builder --user --disable-rofiles-fuse --force-clean --default-branch=stable  --repo="${REPO_DIR}" "${FP_BUILD_DIR}" "${MANIFEST}"

FP_NAME=NextApp-${VERSION}-x86_64-stable.flatpak

flatpak build-bundle ${REPO_DIR} \
  ${FP_NAME} \
  eu.lastviking.NextApp stable \
  --arch=x86_64 \
  --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo

cp -v ${FP_NAME} "${ASSETS_DIR}"

echo Flatpak file is: ${ASSETS_DIR}/${FP_NAME}
