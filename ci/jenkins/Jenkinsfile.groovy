#!/usr/bin/env groovy

// Android builds assume:
//    apt install protobuf-compiler-grpc unzip cmake ninja-build

pipeline {
  parameters {
    booleanParam(name: 'RUN_ANDROID', defaultValue: true, description: 'Run Android stage')
    booleanParam(name: 'RUN_WINDOWS', defaultValue: true, description: 'Run Windows stage')
    booleanParam(name: 'RUN_LINUX', defaultValue: true, description: 'Run Linux stage')
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
            }

            steps {
                checkout scm

                sh '''#!/usr/bin/env bash
              set -Eeuo pipefail

              mkdir -p "$BUILD_DIR" "$ASSETS_DIR"

              if [ ! -d "$VCPKG_ROOT/.git" ]; then
                git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
              else
                git -C "$VCPKG_ROOT" fetch --depth 1 origin
                git -C "$VCPKG_ROOT" reset --hard origin/master
              fi

              chmod +x "$VCPKG_ROOT/bootstrap-vcpkg.sh"
              "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

              chmod +x ./building/linux/build.sh ./building/linux/build-flatpak.sh
              ./building/linux/build.sh
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
      } //parallel
    } // Build
  } // stages
}
