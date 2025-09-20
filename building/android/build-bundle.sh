#!/usr/bin/env bash
set -eo pipefail
trap 'echo "[ERROR] Line $LINENO: \"$BASH_COMMAND\" exited with code $?. Aborting." >&2' ERR

echo Building NextApp bundle for Android

ABIS=("armv7" "arm64_v8a" "x86" "x86_64")
ANDROID_ABIS=("armeabi-v7a" "arm64-v8a" "x86" "x86_64")

BOOST_INSTALL_DIR="${BOOST_INSTALL_DIR:-/var/local/build/boost_1.88}"

if [ ! -d "${BOOST_INSTALL_DIR}" ]; then
  BOOST_VERSION_UNDERSCORE=1_88_0
  echo ">>> Bootstrapping Boost ${BOOST_VERSION_UNDERSCORE} into ${BOOST_INSTALL_DIR}"
  tmpdir=$(mktemp -d)
  trap 'rm -rf "$tmpdir"' EXIT

  # download & extract
  curl -sSL \
    "https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.bz2" \
    -o "$tmpdir/boost.tar.bz2"
  tar xf "$tmpdir/boost.tar.bz2" -C "$tmpdir"

  # install header-only libs
  mkdir -p "${BOOST_INSTALL_DIR}"
  cp -R "$tmpdir/boost_${BOOST_VERSION_UNDERSCORE}/boost" "${BOOST_INSTALL_DIR}/"

  echo ">>> Boost headers installed."
else
  echo ">>> Found existing Boost at ${BOOST_INSTALL_DIR}"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

QT_VERSION="${QT_VERSION:-6.9.1}"
QT_ARCHIVE="qt-${QT_VERSION}.tar"
QT_DOWNLOAD_URL="${QT_DOWNLOAD_URL:-http://192.168.1.95/ci/${QT_ARCHIVE}}"
QT_INSTALL_DIR="${QT_INSTALL_DIR-/var/local/build/qt-${QT_VERSION}}"

export SOURCE_DIR="${SOURCE_DIR:-${SCRIPT_DIR}/../../}"
export BUILD_DIR="${BUILD_DIR:-/var/local/build/nextapp-android}"
#export NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
export NDK_VERSION="${NDK_VERSION:-28.2.13676358}"
export SDK_PATH_BASE="${SDK_PATH_BASE:-/var/local/build/android-sdk}"
#export SDK_PATH="${SDK_PATH:-/var/local/build/android-sdk-$NDK_VERSION}"
if [ -z "$SDK_PATH" ]; then
  export SDK_PATH="${SDK_PATH_BASE}"
fi
export ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-35}"
ASSETS_PATH="${ASSETS_PATH:-${BUILD_DIR}/assets}"

echo "QT_VERSION is ${QT_VERSION}"
echo "HOST_TRIPLET is: $HOST_TRIPLET"
echo "NDK_VERSION is: $NDK_VERSION"
echo "SDK_PATH is: $SDK_PATH"
echo "SOURCE_DIR is: $SOURCE_DIR"
echo "BUILD_DIR is: $BUILD_DIR"
echo "QT_INSTALL_DIR is ${QT_INSTALL_DIR}"
echo "ASSETS_PATH is: ${ASSETS_PATH}"
echo "GOOGLE_SERVICES_PATH is: ${GOOGLE_SERVICES_PATH}"
echo "KEYSTORE_PATH is: ${KEYSTORE_PATH}"
echo "BOOST_INSTALL_DIR is: ${BOOST_INSTALL_DIR}"

if [ ! -d "${QT_INSTALL_DIR}" ]; then
  echo "==> Fetching Qt ${QT_VERSION}"
  mkdir -p "${QT_INSTALL_DIR}"
  curl -fSL "$QT_DOWNLOAD_URL" | tar -xz --strip-components=1 -C "$QT_INSTALL_DIR"
fi

# Install Android SDK/NDK if SDK_PATH is empty
if [ ! -d "$SDK_PATH/cmdline-tools/latest" ]; then
  echo "SDK_PATH missing or incomplete. Installing Android SDK & NDK into $SDK_PATH..."
  mkdir -p "$SDK_PATH"
  TMPDIR=$(mktemp -d)
  (
    cd "$TMPDIR"
    echo "- Downloading Android command-line tools..."
    curl -sSL \
      "https://dl.google.com/android/repository/commandlinetools-linux-13114758_latest.zip" \
      -o cmdline-tools.zip
    unzip -q cmdline-tools.zip
    mkdir -p "$SDK_PATH/cmdline-tools"
    mv cmdline-tools "$SDK_PATH/cmdline-tools/latest"
  )
  rm -rf "$TMPDIR"
fi

export PATH="$SDK_PATH/cmdline-tools/latest/bin:$PATH"
export ANDROID_SDK_ROOT="$SDK_PATH"

# accept licenses
set +o pipefail
yes | sdkmanager --sdk_root="$SDK_PATH" --licenses #>/dev/null
set -o pipefail
echo "Done with licenses."

# install essential components
echo sdkmanager --sdk_root="$SDK_PATH" \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.0.0" \
  "ndk;$NDK_VERSION"

sdkmanager --sdk_root="$SDK_PATH" \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.0.0" \
  "ndk;$NDK_VERSION"

# point to the just-installed NDK
#export ANDROID_NDK_ROOT=$(ls -d "$SDK_PATH/ndk/"* | tail -n1)
export ANDROID_NDK_ROOT="$SDK_PATH/ndk/$NDK_VERSION"

echo "ANDROID_SDK_ROOT is: $ANDROID_SDK_ROOT"
echo "ANDROID_NDK_ROOT is: $ANDROID_NDK_ROOT"


# —————————————————————————————
# Clean & create build and assets dirs
# —————————————————————————————
if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing existing build directory ${BUILD_DIR}…"
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [[ -d "${ASSETS_PATH}" ]]; then
  echo "Removing existing assets directory ${ASSETS_PATH}…"
  rm -rf "${ASSETS_PATH}"
fi
mkdir -p "${ASSETS_PATH}"

export ANDROID_SDK_ROOT="$SDK_PATH"
export ANDROID_HOME="$SDK_PATH"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
export ANDROID_NDK="$ANDROID_NDK_ROOT"
export ANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
ABI=arm64_v8a
ANDROID_ABI=arm64-v8a

echo "ANDROID_SDK_ROOT is: $ANDROID_SDK_ROOT"
echo "ANDROID_NDK_ROOT is: $ANDROID_NDK_ROOT"
echo "ANDROID_HOME is: $ANDROID_HOME"
echo "ANDROID_NDK_HOME is: $ANDROID_NDK_HOME"
echo "ANDROID_NDK is: $ANDROID_NDK"
echo "ABI is: $ABI"
echo "ANDROID_ABI is: $ANDROID_ABI"

CMAKEBUILD_DIR=${BUILD_DIR}/bld
mkdir -p ${CMAKEBUILD_DIR}
pushd ${CMAKEBUILD_DIR}

echo "Configuring NextApp for Android, apk and aab, all abi's ..."

cmake \
  -S ${SOURCE_DIR} \
  -B ${CMAKEBUILD_DIR} \
  -DQT_QMAKE_EXECUTABLE:FILEPATH=${QT_INSTALL_DIR}/gcc_64/bin/qmake \
  -DQT_HOST_PATH:PATH=${QT_INSTALL_DIR}/gcc_64 \
  -DCMAKE_GENERATOR:STRING=Ninja \
  -DCMAKE_COLOR_DIAGNOSTICS:BOOL=ON \
  -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL:BOOL=ON \
  -DCMAKE_PREFIX_PATH:PATH=${QT_INSTALL_DIR}/android_${ABI} \
  -DQT_USE_TARGET_ANDROID_BUILD_DIR:BOOL=ON \
  -DANDROID_USE_LEGACY_TOOLCHAIN_FILE:BOOL=OFF \
  -DANDROID_SDK_ROOT:PATH=${ANDROID_SDK_ROOT} \
  -DCMAKE_CXX_COMPILER:FILEPATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  -DANDROID_NDK:PATH=${ANDROID_NDK} \
  -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${ANDROID_NDK}/build/cmake/android.toolchain.cmake \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DCMAKE_CXX_FLAGS_INIT:STRING= \
  -DANDROID_PLATFORM:STRING=${ANDROID_PLATFORM} \
  -DANDROID_STL:STRING=c++_shared \
  -DCMAKE_FIND_ROOT_PATH:PATH=${QT_INSTALL_DIR}/android_${ABI} \
  -DCMAKE_C_COMPILER:FILEPATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  -DANDROID_ABI:STRING=${ANDROID_ABI} \
  -DNEXTAPP_BOOST_ROOT=${BOOST_INSTALL_DIR} \
  -DNEXTAPP_WITH_FCM=ON \
  '-DQT_ANDROID_ABIS:STRING=armeabi-v7a;arm64-v8a;x86;x86_64' \
  -DQT_ANDROID_BUILD_ALL_ABIS:BOOL=ON

app_version=$(cat VERSION.txt)
echo "App version: ${app_version}"

echo "Building NextApp for Android, apk and aab, all abi's ..."
cmake --build ${CMAKEBUILD_DIR} --target all

DEPLOY_CONFIG=`find src -name '*-deployment-settings.json'`
echo "Deploy config: ${DEPLOY_CONFIG}"

echo 'Preparing and signing Android App Bundle (.aab) ...'
${QT_INSTALL_DIR}/gcc_64/bin/androiddeployqt \
  --input ${DEPLOY_CONFIG} \
  --output ${CMAKEBUILD_DIR}/src/NextAppUi/android-build-appNextAppUi \
  --android-platform ${ANDROID_PLATFORM} \
  --gradle --aab --jarsigner --release \
  --sign ${KEYSTORE_PATH} eu.lastviking.app --storepass ${KEYSTORE_PASSWORD} --no-gdbserver

AAB_FILE=`find src/NextAppUi/android-build-appNextAppUi  -name '*.aab' | grep output | grep release`
APK_FILE=`find src/NextAppUi/android-build-appNextAppUi  -name '*.apk' | grep output | grep release`

echo "AAB file: ${AAB_FILE}"
ls -l ${AAB_FILE}

echo "APK file: ${APK_FILE}"
ls -l ${APK_FILE}

AAB_DST=${ASSETS_PATH}/nextapp-${app_version}.aab
APK_DST=${ASSETS_PATH}/nextapp-${app_version}_all.apk

echo "Copying AAB to ${AAB_DST}"
cp -v "${AAB_FILE}" "${AAB_DST}"
echo "Copying APK to ${APK_DST}"
cp -v "${APK_FILE}" "${APK_DST}"

echo "✔ AAB built and stored in ${aab_dst}"

