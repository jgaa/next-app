#!/usr/bin/env groovy

pipeline {
  agent { label 'main' }

  stages {
    stage('Parallel build') {
      parallel {
        stage('Windows Build') {
            when { expression { false } }
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
            agent { label 'linux' }

            environment {
                // Qt settings
                KEY_ALIAS        = "eu.lastviking.app"
                BUILD_DIR        = "${WORKSPACE}/build"
                SDK_PATH         = "${WORKSPACE}/android-sdk"
                QT_INSTALL_DIR   = "${WORKSPACE}/qt-sdk"
            }

            steps {

                withCredentials([
                  file(credentialsId: 'GOOGLE_SERVICES_NEXTAPP_ANDROID', variable: 'GOOGLE_SERVICES_PATH'),
                  file(credentialsId: 'KEYSTORE_PATH',  variable: 'KEYSTORE_PATH'),
                  string(credentialsId: 'KEYSTORE_PASSWORD',      variable: 'KEYSTORE_PASSWORD')
                ]) {
                  sh '''
                    set -e
                    ./building/android/build-nextapp.sh arm64_v8a
                  '''
              }
            }
        } // android arm64
      } //parallel
    } // Build
  } // stages
}
