@echo off
REM Change to script directory
cd /d "%~dp0"

if not exist "build-configs\" (
    echo Error: This script must be run from the proper directory, containing the 'build-configs' folder.
    exit /b 1
)

set "SOURCE_DIR=%CD%"

if not defined BUILD_DIR (
    set "BUILD_DIR=C:\build"
)

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

echo Preparing to build nextapp...
echo SOURCE_DIR is: %SOURCE_DIR%
echo BUILD_DIR is: %BUILD_DIR%
echo QT_TARGET_DIR is: %QT_TARGET_DIR%
echo VCPKG_ROOT is: %VCPKG_ROOT%
echo VCPKG_DEFAULT_TRIPLET is: %VCPKG_DEFAULT_TRIPLET%

set VCPKG_TRIPLET=%VCPKG_DEFAULT_TRIPLET%
set CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
echo CMAKE_TOOLCHAIN_FILE is: %CMAKE_TOOLCHAIN_FILE%
echo VCPKG_TRIPLET is: %VCPKG_TRIPLET%

echo "Cleaning PATH"
call clean-path.bat
set PATH=%VCPKG_ROOT%;%CLEANED_PATH%
echo Path is now: %PATH%

echo Static Qt target dir is: %QT_TARGET_DIR%
if not exist "%QT_TARGET_DIR%\" (
    echo Qt static build not found. Running build-static-qt.bat...
    call .\build-static-qt.bat
    if errorlevel 1 (
        echo Error: Failed to build static Qt.
        exit /b 1
    )
)

echo
echo -------------------------------------------
echo Building nextapp using statically linked Qt
echo

set "MY_BUILD_DIR=%BUILD_DIR%\nextapp"
set "OPENSSL_ROOT_DIR=%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%"
echo nextapp build OPENSSL_ROOT_DIR is %OPENSSL_ROOT_DIR%

echo MY_BUILD_DIR is: %MY_BUILD_DIR%
echo Building nextapp in %MY_BUILD_DIR%

rmdir /S /Q "%MY_BUILD_DIR%"
mkdir "%MY_BUILD_DIR%"
if errorlevel 1 (
    echo Failed to create %MY_BUILD_DIR%
    exit /b
)

pushd "%MY_BUILD_DIR%"
if errorlevel 1 (
    echo Failed to pushd into %MY_BUILD_DIR%
    exit /b
)

echo %PATH% | find /I "%VCPKG_ROOT%" >nul
if errorlevel 1 (
    set "PATH=%VCPKG_ROOT%;%PATH%"
)
echo Path is: %PATH%

echo copy /Y %SOURCE_DIR%\build-configs\vcpkg-noqt.json %MY_BUILD_DIR%\vcpkg.json
copy /Y "%SOURCE_DIR%\build-configs\vcpkg-noqt.json" vcpkg.json
if errorlevel 1 (
    echo Failed to copy %SOURCE_DIR%\build-configs\vcpkg-noqt.json to vcpkg.json
    exit /b
)

echo Running vcpkg install for nextapp
vcpkg install --triplet "%VCPKG_DEFAULT_TRIPLET%"

echo Listing vcpkg packages
vcpkg list

set "PATH=%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\brotli;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\protobuf;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin;%MY_BUILD_DIR%\bin;%PATH%"

echo PATH is: %PATH%


echo "Calling cmake for nextapp"
cmake -S "%SOURCE_DIR%/../.." -B "%MY_BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN_FILE%" ^
    -DVCPKG_TARGET_TRIPLET="%VCPKG_DEFAULT_TRIPLET%" ^
    -DProtobuf_PROTOC_EXECUTABLE="%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\protobuf\protoc.exe" ^
    -DENABLE_GRPC=ON ^
    -DCMAKE_PREFIX_PATH="%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%;%QT_TARGET_DIR%" ^
    -DOPENSSL_ROOT_DIR="%OPENSSL_ROOT_DIR%" ^
    -G "Ninja" ^
    -DUSE_STATIC_QT=ON ^
    -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo Failed to run cmake
    exit /b
)

cmake --build . --config Release
if errorlevel 1 (
    echo Failed to build the project
    exit /b
)

echo Copying dll's
copy /Y %MY_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin\*.dll %MY_BUILD_DIR%\bin\
copy /Y %MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin\*.dll %MY_BUILD_DIR%\bin\

cpack -G NSIS
if errorlevel 1 (
    echo Failed to make the installer
    echo NSISOutput.log:
    echo --------------------------------
    type "%MY_BUILD_DIR%/_CPack_Packages/win64/NSIS/NSISOutput.log"
    echo --------------------------------
    exit /b
)

copy /Y "%MY_BUILD_DIR%\*.exe" "%BUILD_DIR%\"
if errorlevel 1 (
    echo Failed to copy the executable installer
    exit /b
)

popd
