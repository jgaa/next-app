#!/usr/bin/env groovy

// Android builds assume:
//    apt install protobuf-compiler-grpc unzip cmake ninja-build

pipeline {
  parameters {
    booleanParam(name: 'RUN_ANDROID', defaultValue: true, description: 'Run Android stage')
    booleanParam(name: 'RUN_WINDOWS', defaultValue: true, description: 'Run Windows stage')
    booleanParam(name: 'RUN_LINUX', defaultValue: true, description: 'Run Linux stage')
    booleanParam(name: 'RUN_MACOS', defaultValue: true, description: 'Run macOS stage')
    booleanParam(name: 'REBUILD_WINDOWS_DEPS', defaultValue: false, description: 'Windows only: rebuild static Qt and vcpkg dependencies from source; use once after Qt/vcpkg tool DLL changes')
  }
  agent { label 'main' }

  options {
    // Keep only the last 10 builds and delete older ones…
    // …but also only keep artifacts for those builds for 7 days
    buildDiscarder(
      logRotator(
        daysToKeepStr:        '30',   // delete build records older than 30 days
        numToKeepStr:         '10',   // or when there are more than 10 builds
        artifactDaysToKeepStr:'7',    // delete archived artifacts older than 7 days
        artifactNumToKeepStr: '5'     // keep artifacts only for the last 5 builds
      )
    )
  }

  stages {
    stage('Parallel build') {
      parallel {
        stage('Windows Build') {
            when {
              beforeAgent true
              expression { return params.RUN_WINDOWS }
            }
            // Assumes cmake, nsis, ninja and exists
            //
            //   choco install nsis
            //   choco install ninja
            agent { label 'windows' }
            environment {
              BUILD_DIR               = "${WORKSPACE}\\build"
              VCPKG_ROOT              = "C:\\src\\vcpkg"
              VCPKG_DEFAULT_TRIPLET   = "x64-windows-release"
              CMAKE_GENERATOR_PLATFORM= "x64"
              CMAKE_GENERATOR          = "Ninja"
            }

            steps {
              echo "BRANCH_NAME=${env.BRANCH_NAME}, CHANGE_BRANCH=${env.CHANGE_BRANCH}, GIT_BRANCH=${env.GIT_BRANCH}"
              checkout scm
              bat 'git submodule update --init'

            bat """
                @echo off
                REM path to vswhere.exe
                set "VSWHERE=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe"

                if exist "%VSWHERE%" (
                  REM query latest VS install path into VSINSTALL
                  for /f "delims=" %%I in ('"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do (
                    set "VSINSTALL=%%I"
                  )
                  call "%%VSINSTALL%%\\VC\\Auxiliary\\Build\\vcvars64.bat"
                ) else (
                  REM fallback: direct call to the known path
                  call "%ProgramFiles%\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"
                )

                pushd %VCPKG_ROOT%
                echo 🔄 Pulling latest vcpkg…
                git pull --ff-only
                call bootstrap-vcpkg.bat -disableMetrics
                popd

                set "REBUILD_WINDOWS_DEPS=${params.REBUILD_WINDOWS_DEPS ? 'ON' : 'OFF'}"
                echo "Starting build..."
                building\\static-qt-windows\\build-nextapp.bat
              """

              script {
                def ver = powershell(
                  returnStdout: true,
                  script: "Get-Content \"${env.BUILD_DIR}\\nextapp\\VERSION.txt\" -Raw"
                ).trim()
                env.NEXTAPP_VERSION = ver
                echo "✅ NEXTAPP_VERSION=${ver}"
              }

              archiveArtifacts artifacts: "build/*.exe", fingerprint: true
            }
          } // win

          stage('Android Build') {
            when {
              beforeAgent true
              expression { return params.RUN_ANDROID }
            }
            agent { label 'linux' }

            environment {
                // Qt settings
                KEY_ALIAS         = "eu.lastviking.app"
                BUILD_DIR         = "${WORKSPACE}/build"
                SDK_PATH_BASE     = "${HOME}/android-sdk"
                QT_INSTALL_DIR    = "${HOME}/qt-sdk"
                BOOST_INSTALL_DIR = "${HOME}/boost"
            }

            steps {
                echo "Runner: node=${env.NODE_NAME}, labels=${env.NODE_LABELS}, executor=${env.EXECUTOR_NUMBER}"
                sh 'echo "Host:" $(hostname)'

                checkout scm

                withCredentials([
                  file(credentialsId: 'GOOGLE_SERVICES_NEXTAPP_ANDROID', variable: 'GOOGLE_SERVICES_PATH'),
                  file(credentialsId: 'KEYSTORE_PATH',  variable: 'KEYSTORE_PATH'),
                  string(credentialsId: 'KEYSTORE_PASSWORD', variable: 'KEYSTORE_PASSWORD')
                ]) {
                  sh '''
                    set -e
                    echo "Beginning..."
                    pwd
                    ls -la

                    git submodule update --init
                    chmod +x building/android/build-bundle.sh
                    ./building/android/build-bundle.sh
                  '''

                  archiveArtifacts artifacts: "build/assets/*", fingerprint: true
              }
            }
          } // android

          stage('Linux and flatpak Build') {
            when {
              beforeAgent true
              expression { return params.RUN_LINUX }
            }

            agent { label 'linux' }

            environment {
              BUILD_DIR  = "${WORKSPACE}/build"
              VCPKG_ROOT = "${HOME}/vcpkg"
              ASSETS_DIR = "${WORKSPACE}/build/assets"
              CACHE_DIR  = "${HOME}/cache"
            }

            steps {
                echo "Runner: node=${env.NODE_NAME}, labels=${env.NODE_LABELS}, executor=${env.EXECUTOR_NUMBER}"

                sh 'echo "Host:" $(hostname)'

                checkout scm

                sh 'git submodule update --init'

                sh '''
                #!/bin/bash
                set -Eeuo pipefail

                cd building/linux
                docker buildx build -t nextapp-builder --build-arg UID=$(id -u) --build-arg GID=$(id -g) .

                cd ../../

                mkdir -p ${BUILD_DIR}
                mkdir -p ${CACHE_DIR}
                mkdir -p ${ASSETS_DIR}

                # --- Make sure vcpkg is present and up to date
                if [ ! -d "$VCPKG_ROOT/.git" ]; then
                    echo "Installing vcpkg";
                    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT";
                    ( cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics );
                else
                    echo "Updating vcpkg";
                    ( cd "$VCPKG_ROOT" && git pull --ff-only );
                fi
                ( cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics );

                echo "Building nextapp with static QT"
                docker run --rm -v "$(pwd)":/src:ro  -v "${ASSETS_DIR}":/artifacts -v "${VCPKG_ROOT}":/vcpkg -v "${BUILD_DIR}":/build -v ${CACHE_DIR}:/cache  nextapp-builder

                echo "Building flatpak"
                ./building/linux/build-flatpak.sh

              '''
              }

            post {
              always {
                // Archive whatever the builds produced
                archiveArtifacts artifacts: 'build/assets/**', fingerprint: true
              }
            }
          } //Linux

        stage('macOS Build (x64)') {
          when {
            beforeAgent true
            expression { return params.RUN_MACOS }
          }
          agent { label 'macos' }

          environment {
            BUILD_DIR              = "${WORKSPACE}/build"
            STAGE_DIR              = "${WORKSPACE}/stage-macos-x86_64"
            DMG_DIR                = "${WORKSPACE}/dmg-macos-x86_64"
            QT_SDK_CACHE_DIR       = "${WORKSPACE}/.qt-sdk-cache"
            QT_SDK_DIR             = "${WORKSPACE}/qt-sdk"
            QT_SDK_VERSION         = "6.11.0"
            QT_SDK_BASE_URL        = "https://next-app.org/ci"
            QT_SDK_USERNAME        = "jgaa"
            MACOS_DEPLOYMENT_TARGET = "15.0"
            MACOS_ARCH              = "x86_64"
            NOTARIZATION_ENABLED    = "false"
            SIGN_ID                 = "Developer ID Application: The Last Viking LTD ood (G7GPB64J77)"
          }

          steps {
            echo "Runner: node=${env.NODE_NAME}, labels=${env.NODE_LABELS}, executor=${env.EXECUTOR_NUMBER}"
            sh 'echo "Host:" $(hostname)'

            checkout scm
            sh 'git submodule update --init'

            withCredentials([
              file(credentialsId: 'MACOS_P12_FILE', variable: 'P12_FILE'),
              string(credentialsId: 'MACOS_P12_PASS', variable: 'P12_PASS'),
              string(credentialsId: 'QT_SDK_PASSWORD', variable: 'QT_SDK_PASSWORD')
            ]) {
              sh '''#!/usr/bin/env bash
                set -Eeuo pipefail

                KEYCHAIN_NAME="ci-signing"
                KEYCHAIN_FILE="$KEYCHAIN_NAME.keychain"
                KEYCHAIN_DB="$HOME/Library/Keychains/$KEYCHAIN_NAME.keychain-db"
                KEYCHAIN_PWD="${P12_PASS}"
                cleanup() {
                  security list-keychains -d user -s login.keychain || true
                  security lock-keychain "$KEYCHAIN_FILE" || true
                }
                trap cleanup EXIT

                if [ ! -f "$KEYCHAIN_DB" ] && [ ! -f "$HOME/Library/Keychains/$KEYCHAIN_FILE" ]; then
                  security create-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN_FILE"
                fi

                security unlock-keychain -p "$KEYCHAIN_PWD" "$KEYCHAIN_FILE" || true
                security set-keychain-settings -lut 21600 "$KEYCHAIN_FILE"

                if ! security list-keychains -d user | grep -q "$KEYCHAIN_FILE"; then
                  security list-keychains -d user -s "$KEYCHAIN_FILE" login.keychain
                fi

                if ! security find-identity -p codesigning "$KEYCHAIN_FILE" | grep -Fq "$SIGN_ID"; then
                  security import "$P12_FILE" \
                    -k "$KEYCHAIN_FILE" \
                    -P "$P12_PASS" \
                    -A \
                    -T /usr/bin/codesign \
                    -T /usr/bin/productsign
                fi

                security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PWD" "$KEYCHAIN_FILE"
                security find-identity -v -p codesigning "$KEYCHAIN_FILE"

                brew update
                brew install ninja pkg-config openssl@3 boost protobuf

                QT_SDK_ARCHIVE="$QT_SDK_CACHE_DIR/qt-${QT_SDK_VERSION}-macos.tar.zst"
                if [ ! -f "$QT_SDK_ARCHIVE" ]; then
                  mkdir -p "$QT_SDK_CACHE_DIR"
                  netrc_file="$(mktemp)"
                  chmod 600 "$netrc_file"
                  printf 'machine next-app.org login %s password %s\n' "$QT_SDK_USERNAME" "$QT_SDK_PASSWORD" > "$netrc_file"
                  trap 'rm -f "$netrc_file"; cleanup' EXIT

                  curl --fail --silent --show-error --location \
                    --netrc-file "$netrc_file" \
                    --output "${QT_SDK_ARCHIVE}.tmp" \
                    "$QT_SDK_BASE_URL/qt-${QT_SDK_VERSION}-macos.tar.zst"
                  mv "${QT_SDK_ARCHIVE}.tmp" "$QT_SDK_ARCHIVE"
                fi

                rm -rf "$QT_SDK_DIR" "$BUILD_DIR" "$STAGE_DIR" "$DMG_DIR"
                mkdir -p "$QT_SDK_DIR" "$DMG_DIR"
                tar -xf "$QT_SDK_ARCHIVE" -C "$QT_SDK_DIR"

                QT_ROOT="$QT_SDK_DIR/$QT_SDK_VERSION"
                test -d "$QT_ROOT" || { echo "Qt SDK root $QT_ROOT is missing after extraction" >&2; exit 1; }
                QT_MACDEPLOYQT="$(find "$QT_ROOT" -type f -path '*/bin/macdeployqt' -print -quit)"
                QT_CMAKE="$(find "$QT_ROOT" -type f -path '*/bin/qt-cmake' -print -quit)"
                test -n "$QT_MACDEPLOYQT" || { echo "Unable to locate macdeployqt in the Qt SDK" >&2; exit 1; }
                test -n "$QT_CMAKE" || { echo "Unable to locate qt-cmake in the Qt SDK" >&2; exit 1; }

                OPENSSL_PREFIX="$(brew --prefix openssl@3)"
                BOOST_PREFIX="$(brew --prefix boost)"
                PROTOBUF_PREFIX="$(brew --prefix protobuf)"
                export PATH="$PROTOBUF_PREFIX/bin:$PATH"

                "$QT_CMAKE" -S "$WORKSPACE" -B "$BUILD_DIR" -G Ninja \
                  -DCMAKE_BUILD_TYPE=Release \
                  -DCMAKE_OSX_ARCHITECTURES="$MACOS_ARCH" \
                  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" \
                  -DCMAKE_PREFIX_PATH="$OPENSSL_PREFIX;$BOOST_PREFIX" \
                  -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX" \
                  -DNEXTAPP_WITH_TESTS=OFF \
                  -DNEXTAPP_WITH_BACKEND=OFF \
                  -DNEXTAPP_WITH_SIGNUP=OFF \
                  -DUSE_STATIC_QT=OFF \
                  -DSIGN_ID="$SIGN_ID"

                cmake --build "$BUILD_DIR" --config Release
                cmake --install "$BUILD_DIR" --prefix "$STAGE_DIR" --component Application

                APP_PATH="$(find "$STAGE_DIR" -maxdepth 2 -type d -name 'nextapp.app' -print -quit)"
                test -n "$APP_PATH" || { echo "Unable to locate installed nextapp.app" >&2; exit 1; }

                "$QT_MACDEPLOYQT" "$APP_PATH" \
                  -always-overwrite \
                  -verbose=2 \
                  -qmldir="$WORKSPACE/src/NextAppUi" \
                  -codesign="$SIGN_ID"

                codesign --force --deep --options runtime --sign "$SIGN_ID" "$APP_PATH"
                codesign --verify --deep --strict --verbose=2 "$APP_PATH"

                APP_BIN="$APP_PATH/Contents/MacOS/nextapp"
                file "$APP_BIN"
                lipo -info "$APP_BIN"
                lipo "$APP_BIN" -verify_arch "$MACOS_ARCH"

                ver="$(< "${BUILD_DIR}/VERSION.txt")"
                echo "✅ NEXTAPP_VERSION=$ver"
                dmg_name="nextapp-macos-x86_64-${ver}.dmg"
                ditto "$APP_PATH" "$DMG_DIR/nextapp.app"
                ln -s /Applications "$DMG_DIR/Applications"
                hdiutil create \
                  -volname "NextApp ${ver}" \
                  -srcfolder "$DMG_DIR" \
                  -ov \
                  -format UDZO \
                  "$BUILD_DIR/$dmg_name"
                codesign --force --sign "$SIGN_ID" "$BUILD_DIR/$dmg_name"

                # Set NOTARIZATION_ENABLED=true only after a notarytool keychain profile
                # has been provisioned on the Jenkins macOS node. The profile keeps Apple
                # credentials out of the job environment and build log.
                if [ "$NOTARIZATION_ENABLED" = "true" ]; then
                  : "${NOTARY_KEYCHAIN_PROFILE:?Set NOTARY_KEYCHAIN_PROFILE to enable notarization}"
                  xcrun notarytool submit "$BUILD_DIR/$dmg_name" --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait
                  xcrun stapler staple "$BUILD_DIR/$dmg_name"
                  xcrun stapler validate "$BUILD_DIR/$dmg_name"
                else
                  echo "Apple notarization is disabled."
                fi

                echo "$ver" > "${BUILD_DIR}/.nextapp_version"

              '''
            }

            script {
              env.NEXTAPP_VERSION = readFile("${env.BUILD_DIR}/.nextapp_version").trim()
            }

            archiveArtifacts artifacts: 'build/*.dmg', fingerprint: true
          }
        } // macos

      } //parallel
    } // Build
  } // stages
}
