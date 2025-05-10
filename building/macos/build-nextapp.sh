#!/usr/bin/env bash
set -euo pipefail

trap 'echo "[ERROR] Line $LINENO: \"$BASH_COMMAND\" exited with code $?. Aborting." >&2' ERR

echo "Preparing to build NextApp on macOS using vcpkg for all dependencies..."

# —————————————————————————————
#  1) Change into the script’s directory
# —————————————————————————————
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# —————————————————————————————
#  2) Verify we’re in the right place
# —————————————————————————————
if [[ ! -d "build-configs" ]]; then
  echo "Error: This script must be run from the directory containing the 'build-configs' folder." >&2
  exit 1
fi

# —————————————————————————————
#  3) Defaults (only applied if unset)
# —————————————————————————————
export SOURCE_DIR="${SOURCE_DIR:-${SCRIPT_DIR}/../../}"
export BUILD_DIR="${BUILD_DIR:-/Volumes/devel/build/nextapp}"
export VCPKG_ROOT="${VCPKG_ROOT:-/Volumes/devel/src/vcpkg}"
export VCPKG_TRIPLET="${VCPKG_TRIPLET:-x64-osx-release}"
export VCPKG_INSTALL_OPTIONS="${VCPKG_INSTALL_OPTIONS:---clean-after-build}"
export VCPKG_MANIFEST_MODE="${VCPKG_MANIFEST_MODE:-ON}"
export CMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

echo "SOURCE_DIR is: ${SOURCE_DIR}"
echo "BUILD_DIR is: ${BUILD_DIR}"
echo "VCPKG_ROOT is: ${VCPKG_ROOT}"
echo "CMAKE_TOOLCHAIN_FILE is: ${CMAKE_TOOLCHAIN_FILE}"
echo "VCPKG_TRIPLET is: ${VCPKG_TRIPLET}"

# —————————————————————————————
#  4) Install system deps via Homebrew
# —————————————————————————————
echo "Updating brew and installing required packages…"
brew update
brew install automake autoconf libtool pkg-config autoconf-archive ninja

# —————————————————————————————
#  5) Clone or update vcpkg
# —————————————————————————————
if [[ -d "${VCPKG_ROOT}" ]]; then
  echo "Updating existing vcpkg in ${VCPKG_ROOT}…"
  git -C "${VCPKG_ROOT}" pull --ff-only
else
  echo "Cloning vcpkg into ${VCPKG_ROOT}…"
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
  echo "Bootstrapping vcpkg…"
  ( cd "${VCPKG_ROOT}" && ./bootstrap-vcpkg.sh )
fi

# Prepend vcpkg CLI to PATH
export PATH="${VCPKG_ROOT}:${PATH}"
echo "PATH is now: ${PATH}"

# —————————————————————————————
#  6) Copy your manifest into place
# —————————————————————————————
cp "${SOURCE_DIR}/building/macos/build-configs/vcpkg-all.json" "${SOURCE_DIR}/vcpkg.json"

# —————————————————————————————
#  7) Clean & create build dir
# —————————————————————————————
if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing existing build directory ${BUILD_DIR}…"
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# —————————————————————————————
#  8) Install all vcpkg packages
# —————————————————————————————
echo "Installing vcpkg packages…"
vcpkg install ${VCPKG_INSTALL_OPTIONS} --triplet "${VCPKG_TRIPLET}"
echo "Done installing vcpkg packages."

# —————————————————————————————
#  9) Configure, build, pack
# —————————————————————————————
echo "Configuring CMake…"
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}" \
  -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_MANIFEST_MODE="${VCPKG_MANIFEST_MODE}" \
  "${SOURCE_DIR}"

echo "Building…"
cmake --build . --config Release

echo "Packaging…"
cpack

# —————————————————————————————
# 10) Copy DMG(s) back to source
# —————————————————————————————
cp -v *.dmg "${SOURCE_DIR}"

echo "Done."
