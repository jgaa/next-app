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

        // … your VS env-setup here …

        // 4. Update vcpkg safely
        dir(env.VCPKG_ROOT) {
          bat """
            @echo off
            echo 🔄 Stashing any local vcpkg changes…
            git stash push --include-untracked -m "ci-auto-stash" || echo No local changes
            echo 🔄 Pulling latest vcpkg…
            git pull
            echo 🗑️ Clearing stash…
            git stash clear
          """
        }

        // 5. Build Qt + your app
        bat 'building\\static-qt-windows\\build-nextapp.bat'

        // 6. Read NEXTAPP_VERSION
        script {
          def ver = powershell(
            returnStdout: true,
            script: "Get-Content \"${env.BUILD_DIR}\\nextapp\\VERSION.txt\" -Raw"
          ).trim()
          env.NEXTAPP_VERSION = ver
          echo "✅ NEXTAPP_VERSION=${ver}"
        }

        // 7. Archive installer
        archiveArtifacts artifacts: "${env.BUILD_DIR}\\*.exe", fingerprint: true
      }
    } // win

  } // stages
}
