#!/usr/bin/env bash
set -eo pipefail
trap 'echo "[ERROR] Line $LINENO: \"$BASH_COMMAND\" exited with code $?. Aborting." >&2' ERR

echo Building NextApp for Android

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
  echo "Usage: $0 <ABI> [aab]"
  echo "  ABI must be one of: arm64_v8a | armv7 | x86 | x86_64"
  echo "  Optional second argument: 'aab' to also generate an Android App Bundle"
  exit 1
fi

ABI=$1
BUILD_AAB=${2:-false}
case "$ABI" in
  arm64_v8a)   ANDROID_ABI="arm64-v8a" ;;
  armv7) ANDROID_ABI="armeabi-v7a" ;;
  x86)         ANDROID_ABI="x86" ;;
  x86_64)      ANDROID_ABI="x86_64" ;;
  *)
    echo "Unsupported ABI: $ABI"
    exit 1
    ;;
esac

BOOST_INSTALL_DIR="${BOOST_INSTALL_DIR:-/var/local/build/boost_1.88}"

if [ ! -d "${BOOST_INSTALL_DIR}" ]; then
  BOOST_VERSION_UNDERSCORE=1_88_0
  echo ">>> Bootstrapping Boost ${BOOST_VERSION} into ${BOOST_INSTALL_DIR}"
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
APK_DST="${APK_DST:-apk}"

export SOURCE_DIR="${SOURCE_DIR:-${SCRIPT_DIR}/../../}"
export BUILD_DIR="${BUILD_DIR:-/var/local/build/nextapp-android}"
export SDK_PATH="${SDK_PATH:-/var/local/build/android-sdk}"
export NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
export ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-36}"

echo "QT_VERSION is ${QT_VERSION}"
echo "HOST_TRIPLET is: $HOST_TRIPLET"
echo "SDK_PATH is: $SDK_PATH"
echo "SOURCE_DIR is: $SOURCE_DIR"
echo "BUILD_DIR is: $BUILD_DIR"
echo "QT_INSTALL_DIR is ${QT_INSTALL_DIR}"

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
export ANDROID_NDK_ROOT=$(ls -d "$SDK_PATH/ndk/"* | tail -n1)

echo "ANDROID_SDK_ROOT is: $ANDROID_SDK_ROOT"
echo "ANDROID_NDK_ROOT is: $ANDROID_NDK_ROOT"


# —————————————————————————————
# Clean & create build dir
# —————————————————————————————
if [[ -d "${BUILD_DIR}" ]]; then
  echo "Removing existing build directory ${BUILD_DIR}…"
  rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"


export ANDROID_SDK_ROOT="$SDK_PATH"
export ANDROID_HOME="$SDK_PATH"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
export ANDROID_NDK="$ANDROID_NDK_ROOT"
export ANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"


echo "ANDROID_SDK_ROOT is: $ANDROID_SDK_ROOT"
echo "ANDROID_NDK_ROOT is: $ANDROID_NDK_ROOT"
echo "ANDROID_HOME is: $ANDROID_HOME"
echo "ANDROID_NDK_HOME is: $ANDROID_NDK_HOME"
echo "ANDROID_NDK is: $ANDROID_NDK"

cmake -S ${SOURCE_DIR} \
  -B ${BUILD_DIR} \
  -DCMAKE_BUILD_TYPE:STRING=Release \
  -DQT_USE_TARGET_ANDROID_BUILD_DIR:BOOL=ON \
  -DANDROID_STL:STRING=c++_shared \
  -DANDROID_PLATFORM:STRING=android-29 \
  -DQT_QMAKE_EXECUTABLE:FILEPATH=${QT_INSTALL_DIR}/android_${ABI}/bin/qmake \
  -DCMAKE_CXX_COMPILER:FILEPATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  -DCMAKE_C_COMPILER:FILEPATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  -DANDROID_NDK:PATH=${ANDROID_NDK} \
  -DQT_HOST_PATH:PATH=${QT_INSTALL_DIR}/gcc_64 \
  -DANDROID_ABI:STRING=${ANDROID_ABI} \
  -DCMAKE_PREFIX_PATH:PATH=${QT_INSTALL_DIR}/android_${ABI} \
  -DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL:BOOL=OFF \
  -DCMAKE_GENERATOR:STRING=Ninja \
  -DCMAKE_FIND_ROOT_PATH:PATH=${QT_INSTALL_DIR}/android_${ABI} \
  -DCMAKE_COLOR_DIAGNOSTICS:BOOL=ON \
  -DCMAKE_CXX_FLAGS_INIT:STRING= \
  -DANDROID_USE_LEGACY_TOOLCHAIN_FILE:BOOL=OFF \
  -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${ANDROID_NDK}/build/cmake/android.toolchain.cmake \
  -DANDROID_SDK_ROOT:PATH=${ANDROID_SDK_ROOT} \
  -DCMAKE_SYSTEM_NAME=Android \
  -DNEXTAPP_BOOST_ROOT=${BOOST_INSTALL_DIR} \
  -DNEXTAPP_WITH_FCM=ON

# —————————————————————————————
# Build & install
# —————————————————————————————
echo "Building Nextapp for Android..."
cmake --build . --target apk --config Release --parallel

app_version=$(cat VERSION.txt)

# —————————————————————————————
# Locate & sign the APK
# —————————————————————————————
APK=$(find -name "*release-unsigned.apk" | head -n1)
if [ -z "$APK" ]; then
  echo "[ERROR] No APK found in $INSTALL_DIR/$ABI"
  exit 1
fi

NEXTAPP_APK=${APK_DST}/nextapp-${app_version}_${ANDROID_ABI}.apk
mkdir apk
cp -v ${APK} ${NEXTAPP_APK}

echo "Signing ${NEXTAPP_APK} ..."
# echo ${ANDROID_SDK_ROOT}/build-tools/36.0.0/apksigner sign \
#   --ks ${KEYSTORE_PATH} \
#   --ks-key-alias ${KEY_ALIAS} \
#   --ks-pass pass:${KEYSTORE_PASSWORD} \
#   ${NEXTAPP_APK}

${ANDROID_SDK_ROOT}/build-tools/36.0.0/apksigner sign \
  --ks ${KEYSTORE_PATH} \
  --ks-key-alias ${KEY_ALIAS} \
  --ks-pass pass:${KEYSTORE_PASSWORD} \
  ${NEXTAPP_APK}

# echo "✔ Successfully built & signed: ${NEXTAPP_APK}"

# —————————————————————————————
# Optionally: Create .aab bundle for this ABI
# —————————————————————————————
if [ "$BUILD_AAB" = "aab" ]; then
  echo "Building Android App Bundle (.aab) for ${ANDROID_ABI}..."
  PATH="${QT_INSTALL_DIR}/gcc_64/bin:${PATH}"

  mkdir -p aab

  gradle_dir=$(find -name gradlew | grep NextApp | xargs -r dirname)

  if [[ -z "$gradle_dir" ]]; then
    echo "ERROR: no gradlew found under $(pwd)" >&2
    exit 1
  fi

  echo "Gradle found in: ${gradle_dir}"
  pushd "${gradle_dir}"
  ./gradlew bundleRelease
  popd

  aab_dst=aab/nextapp-${app_version}_${ANDROID_ABI}.aab

  cp -v $(find -type f -name '*.aab' | grep outputs) ${aab_dst}

  echo "Signing ${aab_dst} with keystore ${KEYSTORE_PATH}…"
  jarsigner \
  -sigalg SHA256withRSA \
  -digestalg SHA-256 \
  -keystore "${KEYSTORE_PATH}" \
  -storepass "${KEYSTORE_PASSWORD}" \
  -tsa http://timestamp.digicert.com \
  "${aab_dst}" \
  "${KEY_ALIAS}"

  # (Optional) Verify the signature:
  jarsigner -verify -verbose -certs "${aab_dst}"

  echo "✔ AAB built and stored in ${aab_dst}"
fi

