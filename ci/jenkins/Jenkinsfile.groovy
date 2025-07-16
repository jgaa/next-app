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

        // … your VS-vars setup here …

        // ───────────────────────────────────────────────────
        // 4. Update vcpkg safely
        // ───────────────────────────────────────────────────
        dir(env.VCPKG_ROOT) {
          // Use PowerShell for cleaner logic, or you can do this in `bat` if you prefer cmd
          powershell """
            \$shallow = Test-Path -Path .git\\shallow
            if (\$shallow) {
              Write-Host '🔄 Shallow clone detected – fetching full history…'
              git fetch --unshallow
            }
            else {
              Write-Host '✅ Full clone already – skipping unshallow.'
            }
            git pull
          """
        }

        // 5. Build Qt statically + your app
        bat 'building\\static-qt-windows\\build-nextapp.bat'

        // 6. Extract version
        script {
          def ver = powershell(
            returnStdout: true,
            script: "Get-Content \"${env.BUILD_DIR}\\nextapp\\VERSION.txt\" -Raw"
          ).trim()
          env.NEXTAPP_VERSION = ver
          echo "✅ NEXTAPP_VERSION=${ver}"
        }

        // 7. Archive your installer
        archiveArtifacts artifacts: "${env.BUILD_DIR}\\*.exe", fingerprint: true
      }
    } // win
  } // stages
}
