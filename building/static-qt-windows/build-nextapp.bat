echo "Building nextapp"
mkdir C:\build
set BUILD_DIR=C:\build\nextapp
set SRC_DIR=C:\Users\jgaa\source\repos\next-app
set QT_DIR=C:/qt-static
set VCPKG_ROOT=C:\src\vcpkg
set VCPKG_DEFAULT_TRIPLET=x64-windows
set TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake
set QT_INSTALL_PREFIX=%QT_DIR%
rem PATH=%VCPKG_ROOT%;%PATH%

echo "Removing very annoying vcpkg alias..."
doskey vcpkg=

call clean-path.bat
set "PATH=%VCPKG_ROOT%;%CLEANED_PATH%;%BUILD_DIR%\vcpkg_installed\x64-windows\bin"
echo "Path is: %PATH%"

rmdir /S /Q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

cmake "%SRC_DIR%\src\NextAppUi" ^
  -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
  -DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN_FILE% ^
  -DUSE_STATIC_QT=ON ^
  -DQT_QMAKE_EXECUTABLE:FILEPATH="%QT_DIR%\bin\qmake" ^
  -DVCPKG_TARGET_TRIPLET=%VCPKG_DEFAULT_TRIPLET%

echo cmake --build . -j

