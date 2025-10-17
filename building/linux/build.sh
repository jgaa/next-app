#!/bin/bash
set -eo pipefail

unset ANDROID ANDROID_NDK_ROOT ANDROID_HOME ANDROID_SDK_ROOT
export VCPKG_CMAKE_OPTIONS="-DQT_AUTODETECT_ANDROID=OFF -DANDROID=OFF"

# -------------------------
# Args
# -------------------------
CLEAN_BEFORE_BUILD=1

usage() {
  cat <<'EOF'
Build NextApp with vcpkg (manifest mode)

Usage:
  build.sh [options]

Options:
  -h, --help                 Show this help and exit
  --no-clean-before-build    Do not delete the existing build directory before configuring

Env vars you can set:
  BUILD_DIR                  Default: /var/local/build
  APP_BUILD_DIR              Default: ${BUILD_DIR}/next-app-linux
  VCPKG_ROOT                 Default: /var/local/src/vcpkg
  TRIPLET                    Default: x64-linux-release
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --no-clean-before-build)
      CLEAN_BEFORE_BUILD=0
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      echo "Try: $0 --help" >&2
      exit 2
      ;;
  esac
done


# Move to the directory containing this script
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
manifest_dir=$(pwd)

export BUILD_DIR="${BUILD_DIR:-/var/local/build}"
export APP_BUILD_DIR="${APP_BUILD_DIR:-${BUILD_DIR}/next-app-linux}"
export ASSETS_DIR=${ASSETS_DIR:-${APP_BUILD_DIR}/assets}

echo Building NextApp from sources using vcpkg.
echo BUILD_DIR: ${BUILD_DIR}
echo APP_BUILD_DIR: ${APP_BUILD_DIR}

src_dir="$(realpath "$(pwd)/../../")"
echo SRC_DIR: ${src_dir}

if [[ "$CLEAN_BEFORE_BUILD" == 1 ]]; then
  if [ -d "${APP_BUILD_DIR}" ]; then
      echo "Removing existing build directory: ${APP_BUILD_DIR}"
      rm -rf "${APP_BUILD_DIR}"
  fi
fi

mkdir -p ${APP_BUILD_DIR}
cd ${APP_BUILD_DIR}
cp -v ${manifest_dir}/vcpkg.json .
mkdir -p ${ASSETS_DIR}

# --- vcpkg setup ---
VCPKG_ROOT="${VCPKG_ROOT:-/var/local/src/vcpkg}"
vcpkg="${VCPKG:-$VCPKG_ROOT/vcpkg}"
TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
export VCPKG_INSTALLED_DIR="${APP_BUILD_DIR}/vcpkg_installed"
echo TOOLCHAIN_FILE: ${TOOLCHAIN_FILE}
export VCPKG_BUILD_TYPE=release

# Ensure manifest mode (usually auto when vcpkg.json exists)
export VCPKG_FEATURE_FLAGS=manifests

# Use release triplet to avoid building debug versions of everything
TRIPLET="${TRIPLET:-x64-linux-release}"

echo checking for android envvars.
env | egrep -i '^(ANDROID|ANDROID_HOME|ANDROID_SDK_ROOT|ANDROID_NDK_ROOT)='

echo Running vcpkg install
${vcpkg} install --clean-buildtrees-after-build --clean-downloads-after-build --triplet ${TRIPLET} --vcpkg-root ${VCPKG_ROOT}

echo Running cmake for nextapp...

cmake -S "$src_dir" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET=${TRIPLET} \
  -DVCPKG_MANIFEST_DIR=${APP_BUILD_DIR} \
  -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR} \
  -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE} \
  -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \
  -DCMAKE_PREFIX_PATH=${APP_BUILD_DIR}/vcpkg_installed \
  -DNEXTAPP_WITH_TESTS=OFF \
  -DNEXTAPP_WITH_BACKEND=OFF \
  -DNEXTAPP_WITH_SIGNUP=OFF

export PATH="${VCPKG_ROOT}:${PATH}:${APP_BUILD_DIR}/vcpkg_installed/x64-linux/tools/Qt6/bin/:${APP_BUILD_DIR}/vcpkg_installed/x64-linux-release/tools/Qt6/bin"

echo "Building NextApp..."

cmake --build . -j

cp -c bin/nextapp "${ASSETS_DIR}"

echo Done. Executable is in ${ASSETS_DIR}

