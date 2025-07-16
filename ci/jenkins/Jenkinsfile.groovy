#!/usr/bin/env groovy
pipeline {
  /* no global agent: pick per-stage */
  agent none

  stages {
   stage('Windows Build') {
      agent { label 'windows' }
      environment {
        BUILD_DIR               = "${WORKSPACE}\\build"
        QT_TARGET_DIR           = "${WORKSPACE}\\qt-target"
        VCPKG_ROOT              = "C:\\src\\vcpkg"
        VCPKG_DEFAULT_TRIPLET   = "x64-windows-release"
        CMAKE_GENERATOR_PLATFORM= "x64"
      }
      steps {
        checkout scm
        bat 'git submodule update --init'

        // ────────────────────────────────────────────────────────
        // 2. Set up MSVC developer environment (with vswhere + fallback)
        //    Assumes nsis is installed
        //      >> choco install nsis --no-progress -y
        // ────────────────────────────────────────────────────────
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
        """

        dir(env.VCPKG_ROOT) {
          bat 'git fetch --unshallow || echo Already a full clone'
          bat 'git pull'
        }

        bat 'building\\static-qt-windows\\build-nextapp.bat'

        script {
          def ver = powershell(
            returnStdout: true,
            script: "Get-Content \"${env.BUILD_DIR}\\nextapp\\VERSION.txt\" -Raw"
          ).trim()
          env.NEXTAPP_VERSION = ver
          echo "✅ NEXTAPP_VERSION=${env.NEXTAPP_VERSION}"
        }

        archiveArtifacts artifacts: "${env.BUILD_DIR}\\*.exe", fingerprint: true
      }
    } // win

  } // stages
}
