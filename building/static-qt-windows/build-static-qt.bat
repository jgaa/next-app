@echo off

echo Building static Qt for Windows

set "ORIGINAL_PATH=%PATH%"

if not exist "build-configs\" (
    echo Error: This script must be run from the proper directory, containing the 'build-configs' folder.
    exit /b 1
)

if not defined QT_VERSION set QT_VERSION=6.10.0

if not defined BUILD_DIR (
    set "BUILD_DIR=C:\build"
)
echo "Build dir is: %BUILD_DIR%"

if not defined QT_TARGET_DIR (
    set "QT_TARGET_DIR=C:\qt-static"
)

if not defined VCPKG_ROOT (
    set "VCPKG_ROOT=C:\src\vcpkg"
)

if not defined VCPKG_DEFAULT_TRIPLET (
    set "VCPKG_DEFAULT_TRIPLET=x64-windows-release"
)

if not defined VCPKG_ACTUAL_TRIPLET (
    set "VCPKG_ACTUAL_TRIPLET=x64-windows"
)

if not defined REBUILD_WINDOWS_DEPS (
    set "REBUILD_WINDOWS_DEPS=OFF"
)

if /I "%REBUILD_WINDOWS_DEPS%"=="TRUE" set "REBUILD_WINDOWS_DEPS=ON"
if /I "%REBUILD_WINDOWS_DEPS%"=="YES" set "REBUILD_WINDOWS_DEPS=ON"
if "%REBUILD_WINDOWS_DEPS%"=="1" set "REBUILD_WINDOWS_DEPS=ON"


echo From build-static-qt.bat
echo VCPKG_ROOT is: %VCPKG_ROOT%
echo VCPKG_DEFAULT_TRIPLET is: %VCPKG_DEFAULT_TRIPLET%
echo QT_VERSION is: %QT_VERSION%
echo QT_TARGET_DIR is: %QT_TARGET_DIR%
echo VCPKG_ROOT is: %VCPKG_ROOT%
echo REBUILD_WINDOWS_DEPS is: %REBUILD_WINDOWS_DEPS%

set QT_BUILD_DIR=%BUILD_DIR%\qt
echo Qt build dir is: %QT_BUILD_DIR%
set "OPENSSL_ROOT_DIR=%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%"
echo qt build OPENSSL_ROOT_DIR is: %OPENSSL_ROOT_DIR%

echo %PATH% | find /I "%VCPKG_ROOT%" >nul
if errorlevel 1 (
    set "PATH=%VCPKG_ROOT%;%PATH%"
)
echo "Path is: %PATH%"

echo "Building static QT for Windows in %QT_BUILD_DIR%"
if exist "%QT_BUILD_DIR%" rmdir /S /Q "%QT_BUILD_DIR%"
if exist "%QT_TARGET_DIR%" rmdir /S /Q "%QT_TARGET_DIR%"
mkdir %QT_TARGET_DIR%
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

git clone --quiet --depth=1 --branch %QT_VERSION% git://code.qt.io/qt/qt5.git "%QT_BUILD_DIR%"
if errorlevel 1 (
    echo Failed clone Qt
    exit /b
)

echo Copying build-configs\qt-static-vcpkg.json to %QT_BUILD_DIR%\vcpkg.json
copy build-configs\qt-static-vcpkg.json "%QT_BUILD_DIR%\vcpkg.json"
if errorlevel 1 (
    echo Failed to copy vcpkg.json
    exit /b
)

pushd "%QT_BUILD_DIR%"
if errorlevel 1 (
    echo Failed to cd to %QT_BUILD_DIR%
    exit /b
)

echo "Ready to install vcpkg dependencies"
dir
if /I "%REBUILD_WINDOWS_DEPS%"=="ON" (
    vcpkg install --triplet "%VCPKG_DEFAULT_TRIPLET%" --no-binarycaching --clean-after-build
) else (
    vcpkg install --triplet "%VCPKG_DEFAULT_TRIPLET%"
)
if errorlevel 1 (
    echo Failed to install Qt vcpkg dependencies
    exit /b
)

set BAD_CMAKE_FILE=%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\share\openssl\OpenSSLConfig.cmake
echo Patching OpenSSLConfig.cmake - removing invalid applink requirement: %BAD_CMAKE_FILE%
powershell -Command "(Get-Content \"%BAD_CMAKE_FILE%\") -replace 'OpenSSL::applink', '' | Set-Content \"%BAD_CMAKE_FILE%\""

echo ---------------------------
echo dumping BAD_CMAKE_FILE: %BAD_CMAKE_FILE%
type "%BAD_CMAKE_FILE%"
echo ---------------------------

call init-repository --module-subset=default,-qtwebengine,-qtmultimedia
if errorlevel 1 (
    echo init-repository failed!
    exit /b
)

call configure.bat ^
  -prefix "%QT_TARGET_DIR%" ^
  -static ^
  -release ^
  -opensource ^
  -confirm-license ^
  -no-pch ^
  -nomake examples ^
  -nomake tests ^
  -opengl desktop ^
  -sql-sqlite ^
  -feature-png ^
  -feature-jpeg ^
  -openssl-linked ^
  -no-sql-psql ^
  -no-sql-odbc ^
  -no-sql-mysql ^
  -skip qtcharts ^
  -skip qtwebengine ^
  -skip qtmultimedia ^
  -skip qtspeech ^
  -skip qtsensors ^
  -skip qtconnectivity ^
  -skip qtnetworkauth ^
  -skip qt5compat ^
  -skip qtquick3dphysics ^
  -skip qtremoteobjects ^
  -skip qthttpserver ^
  -skip qtdoc ^
  -vcpkg ^
  -c++std c++20 ^
  -- ^
  -DFEATURE_system_jpeg=OFF ^
  -DFEATURE_system_zlib=OFF ^
  -DFEATURE_system_doubleconversion=OFF

if errorlevel 1 (
    echo configure Qt failed
    exit /b
)

echo Successfully configured Qt.
echo config.summary:
type config.summary

cmake --build . -j
if errorlevel 1 (
    echo Building Qt failed
    exit /b
)

cmake --install .
if errorlevel 1 (
    echo Installing Qt failed
    exit /b
)

set "QT_HOST_DEPS_COPIED=OFF"
if exist "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin\*.dll" (
    echo Copying Qt host tool dependency DLLs from %QT_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin to %QT_TARGET_DIR%\bin
    copy /Y "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin\*.dll" "%QT_TARGET_DIR%\bin\"
    if errorlevel 1 (
        echo Failed to copy Qt host tool dependency DLLs
        exit /b 1
    )
    set "QT_HOST_DEPS_COPIED=ON"
) else (
    if exist "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin\*.dll" (
        echo Copying Qt host tool dependency DLLs from %QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin to %QT_TARGET_DIR%\bin
        copy /Y "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin\*.dll" "%QT_TARGET_DIR%\bin\"
        if errorlevel 1 (
            echo Failed to copy Qt host tool dependency DLLs
            exit /b 1
        )
        set "QT_HOST_DEPS_COPIED=ON"
    ) else (
        echo Error: no Qt host tool dependency DLLs found under %QT_BUILD_DIR%\vcpkg_installed
        exit /b 1
    )
)

echo Successfully built and installed static Qt to %QT_TARGET_DIR%
if /I "%QT_HOST_DEPS_COPIED%"=="ON" (
    echo Static Qt dependency DLLs prepared for NextApp Windows build. > "%QT_TARGET_DIR%\nextapp-static-qt-deps-ready.txt"
    echo QT_VERSION=%QT_VERSION% >> "%QT_TARGET_DIR%\nextapp-static-qt-deps-ready.txt"
    echo VCPKG_DEFAULT_TRIPLET=%VCPKG_DEFAULT_TRIPLET% >> "%QT_TARGET_DIR%\nextapp-static-qt-deps-ready.txt"
    echo VCPKG_ACTUAL_TRIPLET=%VCPKG_ACTUAL_TRIPLET% >> "%QT_TARGET_DIR%\nextapp-static-qt-deps-ready.txt"
)

set "PATH=%ORIGINAL_PATH%"
popd
