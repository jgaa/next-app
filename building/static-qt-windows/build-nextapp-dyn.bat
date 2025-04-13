echo "Building nextapp"
mkdir C:\build
set BUILD_DIR=C:\build\nextapp
set SRC_DIR=C:\Users\jgaa\source\repos\next-app
set VCPKG_ROOT=C:\src\vcpkg
set VCPKG_DEFAULT_TRIPLET=x64-windows
set TOOLCHAIN_FILE=C:\src\vcpkg\scripts\buildsystems\vcpkg.cmake

echo "Removing very annoying vcpkg alias..."
doskey vcpkg=

call clean-path.bat
set "PATH=%VCPKG_ROOT%;%CLEANED_PATH%;%BUILD_DIR%\vcpkg_installed\x64-windows\bin"
echo "Path is: %PATH%"

rmdir /S /Q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

cmake "%SRC_DIR%\src\NextAppUi" ^
  -DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN_FILE% ^
  -DVCPKG_TARGET_TRIPLET=%VCPKG_DEFAULT_TRIPLET%

rem echo cmake --build . -j

