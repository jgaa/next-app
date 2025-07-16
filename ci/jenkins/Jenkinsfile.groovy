#!/usr/bin/env groovy
pipeline {
  /* no global agent: pick per-stage */
  agent none

  stages {
    stage('Windows Build') {
      /* run on your Windows VM */
      agent { label 'windows' }

      /* set all env vars up front for this stage */
      environment {
        BUILD_DIR            = "${WORKSPACE}\\build"
        QT_TARGET_DIR        = "${WORKSPACE}\\qt-target"
        VCPKG_ROOT           = "C:\\src\\vcpkg"
        VCPKG_DEFAULT_TRIPLET= "x64-windows-release"
        CMAKE_GENERATOR_PLATFORM = "x64"
      }

      steps {
        // 1. Clone including submodules
        checkout scm
        bat 'git submodule update --init'

        // 2. Set up MSVC dev prompt
        bat 'call "%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vsdevcmd.bat" -arch x64'

        // 3. Install NSIS
        bat 'choco install nsis --no-progress -y'
        bat 'refreshenv'

        // 4. Update vcpkg
        dir("%VCPKG_ROOT%") {
          bat 'git fetch --unshallow || echo Already a full clone'
          bat 'git pull'
        }

        // 5. Build Qt statically and then your app
        bat 'building\\static-qt-windows\\build-nextapp.bat'

        // 6. Read NEXTAPP_VERSION from file into an env var
        script {
          def ver = powershell(
            returnStdout: true,
            script: "Get-Content \"${env.BUILD_DIR}\\nextapp\\VERSION.txt\" -Raw"
          ).trim()
          env.NEXTAPP_VERSION = ver
          echo "✅ NEXTAPP_VERSION=${env.NEXTAPP_VERSION}"
        }

        // 7. Archive the .exe installer (will show up under “Build Artifacts”)
        archiveArtifacts artifacts: "${env.BUILD_DIR}\\*.exe", fingerprint: true
      }
    } // Windows
  } // stages
}
