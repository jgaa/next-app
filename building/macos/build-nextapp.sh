#!/usr/bin/env bash
set -eo pipefail

trap 'echo "[ERROR] Line $LINENO: \"$BASH_COMMAND\" exited with code $?. Aborting." >&2' ERR

if [ -z "${IDENTITY:-}" ]; then
  echo "Error: \$IDENTITY is not set. Please export IDENTITY before running." >&2
  exit 1
fi

echo "Preparing to build NextApp on macOS using vcpkg for all dependencies..."

# —————————————————————————————
# Change into the script’s directory
# —————————————————————————————
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# —————————————————————————————
# Verify we’re in the right place
# —————————————————————————————
if [[ ! -d "build-configs" ]]; then
  echo "Error: This script must be run from the directory containing the 'build-configs' folder." >&2
  exit 1
fi

# —————————————————————————————
# Defaults (only applied if unset)
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
# Install system deps via Homebrew
# —————————————————————————————
echo "Updating brew and installing required packages…"
brew update
brew install automake autoconf libtool pkg-config autoconf-archive ninja

# —————————————————————————————
# Clone or update vcpkg
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
# Copy your manifest into place
# —————————————————————————————
cp -v "${SOURCE_DIR}/building/macos/build-configs/vcpkg-all.json" "${SOURCE_DIR}/vcpkg.json"

# —————————————————————————————
# Clean & create build dir
# —————————————————————————————
if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing existing build directory ${BUILD_DIR}…"
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# —————————————————————————————
# Install all vcpkg packages
# —————————————————————————————
echo "Installing vcpkg packages…"
cp -v "${SOURCE_DIR}/building/macos/build-configs/vcpkg-all.json" vcpkg.json
vcpkg install ${VCPKG_INSTALL_OPTIONS} --triplet "${VCPKG_TRIPLET}"
echo "Done installing vcpkg packages."

# —————————————————————————————
#  Configure
# —————————————————————————————
echo "Configuring CMake…"
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}" \
  -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_MANIFEST_MODE="${VCPKG_MANIFEST_MODE}" \
  "${SOURCE_DIR}"

# —————————————————————————————
#  Build
# —————————————————————————————

echo "Building…"
cmake --build . --config Release

# —————————————————————————————
#  Sign
# —————————————————————————————

codesign --deep --force --verbose \
  --sign "$IDENTITY" \
  "$BUILD_DIR/MyApp.app"

# —————————————————————————————
#  Package
# —————————————————————————————

echo "Packaging…"
cpack

# —————————————————————————————
# 10) Copy DMG(s) back to source
# —————————————————————————————
cp -v *.dmg "${SOURCE_DIR}"

echo "Done."
