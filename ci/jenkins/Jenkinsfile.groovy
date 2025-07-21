#!/usr/bin/env groovy

pipeline {
  agent { label 'main' }

  stages {
    stage('Parallel build') {
      parallel {
        stage('Windows Build') {
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
                git pull
                popd

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

          stage('Android arm64 Build') {
            agent { label 'debian-bookworm' }

            environment {
                // Qt settings
                QT_VERSION       = '6.9.1'
                QT_ARCHIVE       = "qt-${QT_VERSION}.tar"
                QT_DOWNLOAD_URL  = "http://192.168.1.95/ci/${QT_ARCHIVE}"
                QT_INSTALL_DIR   = "${WORKSPACE}/qt-${QT_VERSION}"

                // Android SDK settings
                ANDROID_SDK_ROOT = '/opt/android-sdk'                        // change if different
                PATH             = "${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:" +
                                    "${ANDROID_SDK_ROOT}/platform-tools:" +
                                    "${ANDROID_SDK_ROOT}/tools/bin:${env.PATH}"
            }

            steps {
                sh '''
                set -e

                echo "==> Fetching Qt ${QT_VERSION}"
                mkdir -p "${QT_INSTALL_DIR}"
                curl -fSL "${QT_DOWNLOAD_URL}" -o qt.tar
                tar xf qt.tar --strip-components=1 -C "${QT_INSTALL_DIR}"

                echo "==> Ensuring Android SDK components"
                # Install cmdline-tools if missing
                if [ ! -d "${ANDROID_SDK_ROOT}/cmdline-tools/latest" ]; then
                    mkdir -p "${ANDROID_SDK_ROOT}/cmdline-tools"
                    # assuming archive already exists locally or pre-installed; adjust as needed
                    sdkmanager --install "cmdline-tools;latest"
                fi

                yes | sdkmanager --licenses

                # Install required SDK components
                sdkmanager \
                    "platform-tools" \
                    "platforms;android-31" \
                    "build-tools;31.0.0" \
                    "ndk;25.2.9519653"

                echo "==> Configuring Qt for Android arm64"
                export QT_ANDROID_CROSS_PATH="${QT_INSTALL_DIR}/android_arm64"
                export ANDROID_NDK_HOME="${ANDROID_SDK_ROOT}/ndk/25.2.9519653"

                echo "==> Running qmake + make"
                mkdir -p build-android-arm64 && cd build-android-arm64
                "${QT_INSTALL_DIR}/6.9.1/bin/qmake" \
                    ../path/to/YourProject.pro \
                    -spec android-clang-arm64 \
                    -android-ndk "${ANDROID_NDK_HOME}"
                make -j"$(nproc)"
                '''
            }
        } // android
      } parallel
    } // Build
  } // stages
}
