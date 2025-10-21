#!/usr/bin/env groovy

// Android builds assume:
//    apt install protobuf-compiler-grpc unzip cmake ninja-build

pipeline {
  parameters {
    booleanParam(name: 'RUN_ANDROID', defaultValue: true, description: 'Run Android stage')
    booleanParam(name: 'RUN_WINDOWS', defaultValue: true, description: 'Run Windows stage')
    booleanParam(name: 'RUN_LINUX', defaultValue: true, description: 'Run Linux stage')
    booleanParam(name: 'RUN_MACOS', defaultValue: true, description: 'Run macOS stage')
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

                sh '''
                #!/bin/bash
                set -Eeuo pipefail

                cd building/linux
                docker buildx build -t nextapp-builder --build-arg UID=$(id -u) --build-arg GID=$(id -g) .

                cd ../../

                mkdir -p ${BUILD_DIR}
                mkdir -p ${CACHE_DIR}

                docker run --rm -it -v "$(pwd)":/src:ro  -v "${ASSETS_DIR}":/artifacts -v "${VCPKG_ROOT}":/vcpkg -v "${BUILD_DIR}":/build -v ${CACHE_DIR}:/cache  nextapp-builder

              '''
              }

            post {
              always {
                // Archive whatever the builds produced
                archiveArtifacts artifacts: 'build/assets/**', fingerprint: true
              }
            }
          } //Linux

        // Add this new stage inside your existing `parallel { ... }` block
        stage('macOS Build (arm64)') {
          when {
            beforeAgent true
            expression { return params.RUN_MACOS }
          }
          // Your mac runner label; ensure Xcode + codesign tools installed
          agent { label 'macos' }

          environment {
            SRC_DIR        = "${WORKSPACE}"
            BUILD_DIR      = "${WORKSPACE}/build"
            VCPKG_ROOT     = "/Volumes/devel/src/vcpkg"
            VCPKG_MANIFEST_MODE = "ON"
            VCPKG_INSTALL_OPTIONS = "--clean-after-build"
            SIGN_ID        = "Developer ID Application: The Last Viking LTD ood (G7GPB64J77)"           }

          steps {
            echo "Runner: node=${env.NODE_NAME}, labels=${env.NODE_LABELS}, executor=${env.EXECUTOR_NUMBER}"
            sh 'echo "Host:" $(hostname)'

            checkout scm
            sh 'git submodule update --init'

            withCredentials([
              file(credentialsId: 'MACOS_P12_FILE', variable: 'P12_FILE'),    // create in Jenkins
              string(credentialsId: 'MACOS_P12_PASS', variable: 'P12_PASS')
            ]) {
              sh '''#!/usr/bin/env bash
                set -Eeuo pipefail

                # 2) Ensure vcpkg "cache" dir exists
                mkdir -p "$VCPKG_ROOT"
                if [ ! -d "$VCPKG_ROOT/.git" ]; then
                  echo "Installing vcpkg into $VCPKG_ROOT"
                  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
                  (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
                else
                  echo "Updating vcpkg in $VCPKG_ROOT"
                  (cd "$VCPKG_ROOT" && git pull --ff-only)
                fi

                # 3) Run your macOS build script
                chmod +x ./building/macos/build-nextapp.sh
                ./building/macos/build-nextapp.sh

                # 4) Read version
                ver="$(< "${BUILD_DIR}/VERSION.txt")"
                echo "✅ NEXTAPP_VERSION=$ver"
                echo "$ver" > "${BUILD_DIR}/.nextapp_version"
              '''
            }

            script {
              // lift the version into env so we can name the artifact nicely, like your Windows stage does :contentReference[oaicite:7]{index=7}
              env.NEXTAPP_VERSION = readFile("${env.BUILD_DIR}/.nextapp_version").trim()
            }

            // 5) Archive DMG (GA uploads '*.dmg' with retention) :contentReference[oaicite:8]{index=8}
            archiveArtifacts artifacts: 'build/*.dmg', fingerprint: true
          }
        } // macos

      } //parallel
    } // Build
  } // stages
}
