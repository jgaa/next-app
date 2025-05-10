#!/usr/bin/env bash
set -euo pipefail

trap 'echo "[ERROR] Line $LINENO: \"$BASH_COMMAND\" exited with code $?. Aborting." >&2' ERR


echo "Preparing to build NextApp on macOS using vcpkg for all dependencies..."

@echo off
REM Change to script directory
cd /d "%~dp0"

if [[ ! -d "build-configs" ]]; then
  echo "Error: This script must be run from the proper directory, containing the 'build-configs' folder." >&2
  exit 1
fi

## Note: I run this locally on a macbook pro with insufficient
## disk space. So I have attached a 1TB external SSD
## via USB-C. That's why I use `/Volumes/devel/` as my default
## build location. You can set each of these variables before
## running the script.

export SOURCE_DIR="${SOURCE_DIR:${PWD}/../../}"
export BUILD_DIR="${BUILD_DIR:-/Volumes/devel/build/nextapp}"
export VCPKG_ROOT="${VCPKG_ROOT:-/Volumes/devel/src/vcpkg}"
export VCPKG_TRIPLET="${VCPKG_TRIPLET:-x64-osx-release}"
export VCPKG_INSTALL_OPTIONS="${VCPKG_INSTALL_OPTIONS:---clean-after-build}"
export VCPKG_MANIFEST_MODE="${VCPKG_MANIFEST_MODE:-ON}"
export CMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}\scripts\buildsystems\vcpkg.cmake

echo SOURCE_DIR is: %SOURCE_DIR%
echo BUILD_DIR is: %BUILD_DIR%
echo VCPKG_ROOT is: %VCPKG_ROOT%
echo CMAKE_TOOLCHAIN_FILE is: %CMAKE_TOOLCHAIN_FILE%
echo VCPKG_TRIPLET is: %VCPKG_TRIPLET%

echo Updatig brew and installing reqired packages
rew update
brew install automake autoconf libtool pkg-config autoconf-archive ninja

if [[ -d "${VCPKG_ROOT}" ]]; then
  echo "Updating existing vcpkg in ${VCPKG_ROOT}…"
  git -C "${VCPKG_ROOT}" pull --ff-only
else
  echo "Cloning vcpkg into ${VCPKG_ROOT}…"
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
  echo "Bootstrapping vcpkg…"
  (
    cd "${VCPKG_ROOT}"
    ./bootstrap-vcpkg.sh
  )
fi

set PATH=%VCPKG_ROOT%;%CLEANED_PATH%
echo Path is now: %PATH%

cp ${SRC_DIR}/building/macos/build-configs/vcpkg-all.json ${SRC_DIR}/vcpkg.json

if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing existing build directory ${BUILD_DIR}…"
  rm -rf "${BUILD_DIR}"
fi

mkdir -p $BUILD_DIR && cd $BUILD_DIR

echo installing vcpkg packages
vcpkg install $VCPKG_INSTALL_OPTIONS --triplet $VCPKG_TRIPLET
echo installing vcpkg packages done
echo configuring cmake
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=$VCPKG_TRIPLET \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_MANIFEST_MODE=ON \
    -DENABLE_GRPC=ON \
    -DUSE_STATIC_QT=ON \
    $SRC_DIR

cd $BUILD_DIR
cmake --build . --config Release

cpack

cp -v *.dmg "${SOURCE_DIR}"

echo done
