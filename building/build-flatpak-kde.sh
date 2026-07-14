#!/usr/bin/env bash

set -euo pipefail
#set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_ID="${APP_ID:-eu.lastviking.NextApp}"
WORK_ROOT="${WORK_ROOT:-${ROOT_DIR}/.flatpak-kde}"
SOURCE_STAGE_DIR="${SOURCE_STAGE_DIR:-${WORK_ROOT}/source}"
MANIFEST_PATH="${MANIFEST_PATH:-${WORK_ROOT}/${APP_ID}.yml}"
DESKTOP_FILE="${DESKTOP_FILE:-${ROOT_DIR}/flatpak/${APP_ID}.desktop}"
METAINFO_TEMPLATE="${METAINFO_TEMPLATE:-${ROOT_DIR}/flatpak/${APP_ID}.metainfo.xml.in}"
METAINFO_FILE="${METAINFO_FILE:-${WORK_ROOT}/${APP_ID}.metainfo.xml}"
BUILD_DIR="${BUILD_DIR:-${WORK_ROOT}/build}"
REPO_DIR="${REPO_DIR:-${WORK_ROOT}/repo}"
DIST_DIR="${DIST_DIR:-${WORK_ROOT}/dist}"
BUNDLE_BRANCH="${BUNDLE_BRANCH:-stable}"
KDE_RUNTIME_VERSION="${KDE_RUNTIME_VERSION:-6.10}"

PROTOBUF_VERSION="${PROTOBUF_VERSION:-32.1}"
PROTOBUF_SHA256="${PROTOBUF_SHA256:-3feeabd077a112b56af52519bc4ece90e28b4583f4fc2549c95d765985e0fd3c}"
BOOST_VERSION="${BOOST_VERSION:-1.90.0}"
BOOST_VERSION_UNDERSCORE="${BOOST_VERSION_UNDERSCORE:-1_90_0}"
BOOST_SHA256="${BOOST_SHA256:-49551aff3b22cbc5c5a9ed3dbc92f0e23ea50a0f7325b0d198b705e8ee3fc305}"

require_cmd() {
    local cmd="$1"
    command -v "${cmd}" >/dev/null 2>&1 || {
        echo "Missing required command: ${cmd}" >&2
        exit 1
    }
}

print_tool_versions() {
    echo "Tool versions:"
    flatpak --version || true
    flatpak-builder --version || true
    appstreamcli --version || true
}

detect_qt_sdk_version() {
    local sdk_ref="$1"
    local sdk_location version_file version

    sdk_location="$(
        env -u G_MESSAGES_DEBUG -u GIO_DEBUG -u RUST_LOG -u RUST_BACKTRACE \
            flatpak info --show-location --user "${sdk_ref}" 2>/dev/null
    )"
    version_file="${sdk_location}/files/lib/x86_64-linux-gnu/cmake/Qt6/Qt6ConfigVersionImpl.cmake"

    if [[ ! -f "${version_file}" ]]; then
        echo "Unable to locate Qt6ConfigVersion.cmake in ${sdk_location}" >&2
        exit 1
    fi

    version="$(
        awk -F'"' '/^set\(PACKAGE_VERSION "/ { print $2; exit }' "${version_file}"
    )"

    if [[ -z "${version}" ]]; then
        echo "Unable to determine Qt SDK version from ${version_file}" >&2
        exit 1
    fi

    printf '%s\n' "${version}"
}

stage_sources() {
    rm -rf "${SOURCE_STAGE_DIR}"
    mkdir -p "${SOURCE_STAGE_DIR}"

    rsync -a --delete \
        --exclude '.git' \
        --exclude '.git/' \
        --exclude '.codex/' \
        --exclude '.flatpak-builder/' \
        --exclude '.flatpak-kde/' \
        --exclude '.qtcreator/' \
        --exclude 'build/' \
        --exclude 'build-*/' \
        --exclude 'devops/' \
        --exclude '.idea/' \
        --exclude '.vscode/' \
        "${ROOT_DIR}/" "${SOURCE_STAGE_DIR}/"
}

write_metainfo_file() {
    cat "${METAINFO_TEMPLATE}" \
        | sed "s/@VERSION@/${APP_VERSION}/g" \
        | sed "s/@RELEASE_DATE@/${RELEASE_DATE}/g" \
        | sed "s|<id>.*</id>|<id>${APP_ID}</id>|" \
        > "${METAINFO_FILE}"
}

write_manifest() {
    cat > "${MANIFEST_PATH}" <<EOF
id: ${APP_ID}
runtime: org.kde.Platform
runtime-version: '${KDE_RUNTIME_VERSION}'
sdk: org.kde.Sdk
command: nextapp

finish-args:
  - --share=network
  - --socket=wayland
  - --socket=fallback-x11
  - --device=dri
  - --share=ipc
  - --socket=session-bus
  - --socket=system-bus
  - --socket=pulseaudio
  - --persist=.local/share
  - --persist=.config
  - --filesystem=home/NextApp:create

build-options:
  cflags: -O2
  cxxflags: -O2
  strip: true
  no-debuginfo: true
  env:
    CMAKE_PREFIX_PATH: /app;/usr
    CMAKE_SYSTEM_PREFIX_PATH: /app;/usr
    CMAKE_FIND_USE_PACKAGE_REGISTRY: 'OFF'
    CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY: 'OFF'

modules:
  - name: boost-headers
    buildsystem: simple
    build-commands:
      - install -d /app/include
      - cp -a boost /app/include/
    sources:
      - type: archive
        url: https://archives.boost.io/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2
        sha256: ${BOOST_SHA256}

  - name: nextapp
    buildsystem: simple
    build-commands:
      - |
        set -euo pipefail
        find_cmake_pkg_dir() {
          local pkg="\$1"
          local config
          config="\$(find /app /usr -type f -name "\${pkg}Config.cmake" 2>/dev/null | head -n1 || true)"
          if [[ -n "\${config}" ]]; then
            dirname "\${config}"
          fi
        }

        cmake -S . -B build -G Ninja \\
          -DCMAKE_BUILD_TYPE=Release \\
          "-DCMAKE_PREFIX_PATH=/usr;/app" \\
          -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \\
          -DCMAKE_INSTALL_PREFIX=/app \\
          -DNEXTAPP_FLATPAK_BUILD:BOOL=ON \\
          "-Dprotobuf_ROOT=\$PWD/protobuf-src/src" \\
          "-DProtobuf_ROOT=\$PWD/protobuf-src/src" \\
          -DBoost_NO_BOOST_CMAKE=ON \\
          -DBoost_NO_SYSTEM_PATHS=OFF \\
          -DNEXTAPP_BOOST_ROOT=/app/include \\
          -DFETCHCONTENT_SOURCE_DIR_MINIAUDIO="\$PWD/miniaudio" \\
          -DBUILD_SHARED_LIBS=OFF \\
          -DNEXTAPP_WITH_UI:BOOL=ON \\
          -DNEXTAPP_WITH_BACKEND:BOOL=OFF \\
          -DNEXTAPP_WITH_SIGNUP:BOOL=OFF \\
          -DNEXTAPP_WITH_TESTS:BOOL=OFF \\
          -DUSE_STATIC_QT:BOOL=OFF \\

        cmake --build build

        install -Dm755 build/bin/nextapp /app/bin/nextapp
        install -Dm644 src/NextAppUi/icons/nextapp.svg /app/share/icons/hicolor/scalable/apps/${APP_ID}.svg
        install -Dm644 ${APP_ID}.desktop /app/share/applications/${APP_ID}.desktop
        install -Dm644 ${APP_ID}.metainfo.xml /app/share/metainfo/${APP_ID}.metainfo.xml

        test -r /app/share/icons/hicolor/scalable/apps/${APP_ID}.svg || { echo "App icon is not readable"; exit 1; }
        test -r /app/share/applications/${APP_ID}.desktop || { echo "Desktop file is not readable"; exit 1; }
        test -r /app/share/metainfo/${APP_ID}.metainfo.xml || {
          echo "Metainfo is not readable in /app/share/metainfo"
          ls -la /app/share/metainfo || true
          exit 1
        }
        if [[ -d build/lib ]]; then
          find build/lib -maxdepth 1 -type f \\( -name '*.so' -o -name '*.so.*' \\) -print0 \\
            | while IFS= read -r -d '' lib; do
                install -Dm755 "\${lib}" "/app/lib/\$(basename "\${lib}")"
              done
        fi

        # KDE 6.10 already ships QtGrpc/QtProtobuf in the runtime. Bundling a
        # second copy from the app causes private Qt ABI mismatches at runtime.
        if [[ -d /app/lib ]] && find /app/lib -maxdepth 1 \\( -type f -o -type l \\) \\
          \\( -name 'libQt6Grpc.so*' -o -name 'libQt6Protobuf.so*' \\) | grep -q .; then
          echo "Unexpected bundled QtGrpc/QtProtobuf libraries in /app/lib; use the KDE runtime copies instead."
          find /app/lib -maxdepth 1 \\( -type f -o -type l \\) \\
            \\( -name 'libQt6Grpc.so*' -o -name 'libQt6Protobuf.so*' \\) -print || true
          exit 1
        fi
        if [[ -d /app/lib/x86_64-linux-gnu ]] && find /app/lib/x86_64-linux-gnu -maxdepth 1 \\( -type f -o -type l \\) \\
          \\( -name 'libQt6Grpc.so*' -o -name 'libQt6Protobuf.so*' \\) | grep -q .; then
          echo "Unexpected bundled QtGrpc/QtProtobuf libraries in /app/lib/x86_64-linux-gnu; use the KDE runtime copies instead."
          find /app/lib/x86_64-linux-gnu -maxdepth 1 \\( -type f -o -type l \\) \\
            \\( -name 'libQt6Grpc.so*' -o -name 'libQt6Protobuf.so*' \\) -print || true
          exit 1
        fi

        # Prune SDK/dev payload from the final app export. The runtime only
        # needs the app binary, shared libs, QML plugins, and desktop metadata.
        rm -rf /app/include
        rm -rf /app/lib/cmake /app/lib/pkgconfig /app/lib/metatypes /app/lib/modules /app/lib/sbom
        rm -rf /app/mkspecs
        rm -rf /app/share/man /app/share/pkgconfig

        rm -f /app/bin/adig /app/bin/ahost /app/bin/grpc_* /app/bin/protoc*

        if [[ -d /app/lib ]]; then
          find /app/lib -maxdepth 1 \\( -type f -o -type l \\) \\
            \\( -name '*.a' -o -name '*.la' -o -name 'libprotoc.so*' -o -name 'libprotobuf*.so*' -o -name 'libutf8_*.so*' \\) \\
            -delete
        fi

        if [[ -d /app/lib/x86_64-linux-gnu ]]; then
          find /app/lib/x86_64-linux-gnu -maxdepth 1 \\( -type f -o -type l \\) -name '*.prl' -delete
        fi
    sources:
      - type: dir
        path: ${SOURCE_STAGE_DIR}
      - type: file
        path: ${DESKTOP_FILE}
        dest-filename: ${APP_ID}.desktop
      - type: file
        path: ${METAINFO_FILE}
        dest-filename: ${APP_ID}.metainfo.xml
      - type: archive
        url: https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION}/protobuf-${PROTOBUF_VERSION}.tar.gz
        sha256: ${PROTOBUF_SHA256}
        dest: protobuf-src
        strip-components: 1
      - type: git
        url: https://github.com/mackron/miniaudio.git
        branch: master
        dest: miniaudio
EOF
}

main() {
    require_cmd flatpak
    require_cmd flatpak-builder
    require_cmd rsync
    require_cmd awk
    require_cmd appstreamcli

    print_tool_versions

    [[ -f "${METAINFO_TEMPLATE}" ]] || {
        echo "Missing AppStream metainfo template: ${METAINFO_TEMPLATE}" >&2
        exit 1
    }
    [[ -f "${DESKTOP_FILE}" ]] || {
        echo "Missing desktop file: ${DESKTOP_FILE}" >&2
        exit 1
    }

    mkdir -p "${WORK_ROOT}" "${DIST_DIR}"

    flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo

    flatpak install -y --noninteractive --user flathub \
        "org.kde.Sdk//${KDE_RUNTIME_VERSION}" \
        "org.kde.Platform//${KDE_RUNTIME_VERSION}"

    QT_SDK_VERSION="${QT_SDK_VERSION:-$(detect_qt_sdk_version "org.kde.Sdk//${KDE_RUNTIME_VERSION}")}"
    QT_MODULE_REF="${QT_MODULE_REF:-v${QT_SDK_VERSION}}"
    ARCH="$(flatpak --default-arch)"
    APP_VERSION="$(
        sed -nE 's/^[[:space:]]*set[[:space:]]*\([[:space:]]*NEXTAPP_VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "${ROOT_DIR}/CMakeLists.txt" \
        | head -n1
    )"
    [[ -n "${APP_VERSION}" ]] || {
        echo "Unable to determine NEXTAPP_VERSION from ${ROOT_DIR}/CMakeLists.txt" >&2
        exit 1
    }
    RELEASE_DATE="$(date -u +%F)"

    echo "Using KDE runtime branch: ${KDE_RUNTIME_VERSION}"
    echo "Using Qt SDK version: ${QT_SDK_VERSION}"
    echo "Using Qt module ref: ${QT_MODULE_REF}"

    stage_sources
    write_metainfo_file
    appstreamcli validate --no-net --verbose "${METAINFO_FILE}"
    write_manifest

    flatpak-builder --verbose \
        --user \
        --disable-rofiles-fuse \
        --force-clean \
        --default-branch="${BUNDLE_BRANCH}" \
        --repo="${REPO_DIR}" \
        "${BUILD_DIR}" \
        "${MANIFEST_PATH}"

    BUNDLE_NAME="NextApp-${APP_VERSION}-${ARCH}-${BUNDLE_BRANCH}.flatpak"
    flatpak build-bundle --verbose \
        "${REPO_DIR}" \
        "${DIST_DIR}/${BUNDLE_NAME}" \
        "${APP_ID}" \
        "${BUNDLE_BRANCH}" \
        --arch="${ARCH}" \
        --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo

    echo "Manifest: ${MANIFEST_PATH}"
    echo "Repo: ${REPO_DIR}"
    echo "Bundle: ${DIST_DIR}/${BUNDLE_NAME}"
    echo "Run with:"
    echo "  flatpak --user install -y \"${DIST_DIR}/${BUNDLE_NAME}\""
    echo "  flatpak run ${APP_ID}"
}

main "$@"
