#!/usr/bin/env groovy
pipeline {
  /* no global agent: pick per-stage */
  agent none

  stages {
    stage('Windows Build') {
      agent { label 'windows' }
      environment {
        BUILD_DIR              = "${WORKSPACE}\\build"
        QT_TARGET_DIR          = "${WORKSPACE}\\qt-target"
        VCPKG_ROOT             = "C:\\src\\vcpkg"
        VCPKG_DEFAULT_TRIPLET  = "x64-windows-release"
        CMAKE_GENERATOR_PLATFORM = "x64"
      }
      steps {
        checkout scm
        bat 'git submodule update --init'

        // Use vswhere to find the real VS install path, then invoke VsDevCmd.bat
        bat """
          @echo off
          for /f "usebackq delims=" %%I in (
            \"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\"
            -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath
          ) do (
            set "VSINSTALL=%%I"
          )
          if not defined VSINSTALL (
            echo ERROR: vswhere failed to locate Visual Studio!
            exit /b 1
          )
          call "%%VSINSTALL%%\\Common7\\Tools\\VsDevCmd.bat" -arch x64
        """

        bat 'choco install nsis --no-progress -y'
        bat 'refreshenv'

        dir("%VCPKG_ROOT%") {
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
    } // windows
  } // stages
}
