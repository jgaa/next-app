set QT_VERSION="6.8.3"
set BUILD_DIR=C:\build\qt
set TARGET_DIR=C:\qt-static
set VCPKG_ROOT=C:\src\vcpkg
set VCPKG_DEFAULT_TRIPLET=x64-windows

call clean-path.bat
set "PATH=%VCPKG_ROOT%;%PATH%"  :: Applies the cleaned PATH
echo "Path is: %PATH%"

echo "Building static QT for Windows"
mkdir C:\build
rmdir /S /Q  %BUILD_DIR%
rmdir /S /Q  %TARGET_DIR%
mkdir %TARGET_DIR%

git clone --depth=1 --branch %QT_VERSION% git://code.qt.io/qt/qt5.git %BUILD_DIR%

copy vcpkg.json %BUILD_DIR%
cd %BUILD_DIR%
rem vcpkg install
vcpkg install --triplet %VCPKG_DEFAULT_TRIPLET%

set BAD_CMAKE_FILE=C:\build\qt\vcpkg_installed\x64-windows\share\openssl\OpenSSLConfig.cmake
powershell -Command "(Get-Content \"%BAD_CMAKE_FILE%\") -replace 'OpenSSL::applink', '' | Set-Content \"%BAD_CMAKE_FILE%\""

call init-repository --module-subset=default,-qtwebengine,-qtmultimedia

rem -platform win32-msvc

call configure.bat ^
  -prefix %TARGET_DIR% ^
  -static ^
  -release ^
  -opensource ^
  -confirm-license ^
  -no-pch ^
  -nomake examples ^
  -nomake tests ^
  -opengl desktop ^
  -openssl-linked ^
  -sql-sqlite ^
  -feature-png ^
  -feature-jpeg ^
  -skip qtwebengine ^
  -skip qtmultimedia ^
  -skip qtsensors ^
  -skip qtconnectivity ^
  -skip qtnetworkauth ^
  -skip qtspeech ^
  -skip qt5compat ^
  -skip qtquick3dphysics ^
  -skip qtremoteobjects ^
  -skip qthttpserver ^
  -skip qtdoc ^
  -vcpkg ^
  -nomake examples -nomake tests -- -DFEATURE_system_zlib=OFF  -DFEATURE_system_jpeg=OFF



cmake --build . -j
cmake --install .

echo "Potentially done..."

