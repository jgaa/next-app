#!/usr/bin/env bash

cd
cd build

set -e

############################
# 1) Parse version from CMakeLists.txt
############################
CMAKEFILE="/next-app/src/NextAppUi/CMakeLists.txt"
VERSION="$(grep -oP 'VERSION \\K[0-9]+\\.[0-9]+\\.[0-9]+' "$CMAKEFILE" || true)"  # If not found, it's empty

############################
# 2) Generate the .desktop file
############################
cat << 'EOF' > eu.lastviking.NextApp.desktop
[Desktop Entry]
Name=NextApp
Comment=Example Flatpak of NextApp
Exec=nextapp
Icon=eu.lastviking.NextApp
Terminal=false
Type=Application
Categories=Utility;
EOF

############################
# 3) Recursive function to find missing libraries
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

############################
# 4) Run the function on your main binary
############################
MAIN_BINARY="bin/nextapp"
if [[ ! -f "$MAIN_BINARY" ]]; then
  echo "Warning: $MAIN_BINARY not found. The script won't find dependencies for your main app."
  echo "Press Ctrl+C to abort or wait to continue..."
  sleep 3
fi

add_missing_libs "$MAIN_BINARY"

############################
# 5) Create "install" commands & "sources" for discovered libs
############################
INSTALL_COMMANDS=()
SOURCE_ENTRIES=()

for LIBNAME in "${MISSING_LIBS[@]}"; do
  HOSTFILE="${FOUND_LIBS["$LIBNAME"]}"
  if [[ -n "$HOSTFILE" ]]; then
    BASENAME="$(basename "$HOSTFILE")"
    INSTALL_COMMANDS+=( "install -Dm755 $BASENAME /app/lib/$BASENAME" )
    SOURCE_ENTRIES+=( "{ \"type\": \"file\", \"path\": \"$HOSTFILE\", \"dest-filename\": \"$BASENAME\" }" )
  fi
done

# Add the new libraries manually
MANUAL_LIBS=(
  "/usr/lib/x86_64-linux-gnu/libicudata.so.70"
  "/usr/lib/x86_64-linux-gnu/libicui18n.so.70"
  "/usr/lib/x86_64-linux-gnu/libicuuc.so.70"
  "/usr/lib/x86_64-linux-gnu/libtiff.so.5"
  "/usr/lib/x86_64-linux-gnu/libjbig.so.0"
  "/usr/lib/x86_64-linux-gnu/libjpeg.so.8"
  "/usr/lib/x86_64-linux-gnu/libdeflate.so.0"
)

for LIB in "${MANUAL_LIBS[@]}"; do
  if [[ -f "$LIB" ]]; then
    BASENAME="$(basename "$LIB")"
    INSTALL_COMMANDS+=( "install -Dm755 $BASENAME /app/lib/$BASENAME" )
    SOURCE_ENTRIES+=( "{ \"type\": \"file\", \"path\": \"$LIB\", \"dest-filename\": \"$BASENAME\" }" )
  else
    echo "Warning: $LIB not found. Skipping."
  fi
done

############################
# 6) Build the JSON snippet for the "libs" module
############################
LIB_MODULE=""
if [ ${#INSTALL_COMMANDS[@]} -gt 0 ]; then
  BUILD_COMMANDS_JSON=$(printf '"%s",\n' "${INSTALL_COMMANDS[@]}")
  BUILD_COMMANDS_JSON="[${BUILD_COMMANDS_JSON%,}]"

  SOURCES_JSON=$(printf '%s,\n' "${SOURCE_ENTRIES[@]}")
  SOURCES_JSON="[${SOURCES_JSON%,}]"

  read -r -d '' LIB_MODULE << EOM || true
,
{
  "name": "libs",
  "buildsystem": "simple",
  "build-commands": $BUILD_COMMANDS_JSON,
  "sources": $SOURCES_JSON
}
EOM
fi

############################
# 7) Write out the final Flatpak manifest
############################
cat << EOF > eu.lastviking.NextApp.json
{
  "app-id": "eu.lastviking.NextApp",
  "runtime": "org.freedesktop.Platform",
  "runtime-version": "23.08",
  "sdk": "org.freedesktop.Sdk",
  "command": "nextapp",
  "version": "$VERSION",

  "finish-args": [
    "--share=network",
    "--socket=x11",
    "--device=dri"
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
          "path": "bin/nextapp"
        }
      ]
    },
    {
      "name": "resources",
      "buildsystem": "simple",
      "build-commands": [
        "install -Dm644 nextapp.svg /app/share/icons/hicolor/scalable/apps/eu.lastviking.NextApp.svg",
        "install -Dm644 eu.lastviking.NextApp.desktop /app/share/applications/eu.lastviking.NextApp.desktop"
      ],
      "sources": [
        {
          "type": "file",
          "path": "/next-app/src/NextAppUi/icons/nextapp.svg"
        },
        {
          "type": "file",
          "path": "eu.lastviking.NextApp.desktop"
        }
      ]
    }$LIB_MODULE
  ]
}
EOF

############################
# 8) Print summary
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

# Build the Flatpak
if [[ -d "flatpak" ]]; then
  sudo rm -rf flatpak/*
fi

sudo flatpak-builder --repo fpnextapp --disable-rofiles-fuse flatpak/ eu.lastviking.NextApp.json
flatpak build-bundle fpnextapp NextApp.flatpak eu.lastviking.NextApp
cp -v NextApp.flatpak /target
