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

if not defined REBUILD_WINDOWS_DEPS (
    set "REBUILD_WINDOWS_DEPS=OFF"
)

if /I "%REBUILD_WINDOWS_DEPS%"=="TRUE" set "REBUILD_WINDOWS_DEPS=ON"
if /I "%REBUILD_WINDOWS_DEPS%"=="YES" set "REBUILD_WINDOWS_DEPS=ON"
if "%REBUILD_WINDOWS_DEPS%"=="1" set "REBUILD_WINDOWS_DEPS=ON"

echo Preparing to build nextapp...
echo SOURCE_DIR is: %SOURCE_DIR%
echo BUILD_DIR is: %BUILD_DIR%
echo QT_TARGET_DIR is: %QT_TARGET_DIR%
echo VCPKG_ROOT is: %VCPKG_ROOT%
echo VCPKG_DEFAULT_TRIPLET is: %VCPKG_DEFAULT_TRIPLET%
echo REBUILD_WINDOWS_DEPS is: %REBUILD_WINDOWS_DEPS%

set VCPKG_TRIPLET=%VCPKG_DEFAULT_TRIPLET%
set CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
set "QT_BUILD_DIR=%BUILD_DIR%\qt"
echo CMAKE_TOOLCHAIN_FILE is: %CMAKE_TOOLCHAIN_FILE%
echo VCPKG_TRIPLET is: %VCPKG_TRIPLET%
echo QT_BUILD_DIR is: %QT_BUILD_DIR%

echo "Cleaning PATH"
call clean-path.bat
set PATH=%VCPKG_ROOT%;%CLEANED_PATH%
echo Path is now: %PATH%

echo Static Qt target dir is: %QT_TARGET_DIR%
set "QT_DEPS_READY_MARKER=%QT_TARGET_DIR%\nextapp-static-qt-deps-ready.txt"
if /I "%REBUILD_WINDOWS_DEPS%"=="ON" (
    echo REBUILD_WINDOWS_DEPS is ON. Removing cached static Qt build and install directories.
    if exist "%QT_BUILD_DIR%" rmdir /S /Q "%QT_BUILD_DIR%"
    if exist "%QT_TARGET_DIR%" rmdir /S /Q "%QT_TARGET_DIR%"
)

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
if /I "%REBUILD_WINDOWS_DEPS%"=="ON" (
    vcpkg install --triplet "%VCPKG_DEFAULT_TRIPLET%" --no-binarycaching --clean-after-build
) else (
    vcpkg install --triplet "%VCPKG_DEFAULT_TRIPLET%"
)
if errorlevel 1 (
    echo Failed to install nextapp vcpkg dependencies
    exit /b
)

echo Listing vcpkg packages
vcpkg list

if not defined QT_DEPS_BIN (
    if exist "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin\" (
        set "QT_DEPS_BIN=%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin"
    ) else (
        if exist "%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin\" (
            set "QT_DEPS_BIN=%QT_BUILD_DIR%\vcpkg_installed\%VCPKG_ACTUAL_TRIPLET%\bin"
        )
    )
)

if defined QT_DEPS_BIN (
    set "PATH=%QT_TARGET_DIR%\bin;%QT_DEPS_BIN%;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\brotli;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\protobuf;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin;%MY_BUILD_DIR%\bin;%PATH%"
) else (
    if exist "%QT_DEPS_READY_MARKER%" (
        echo Qt host tool dependency marker found: %QT_DEPS_READY_MARKER%
        echo The Qt generator tools will use dependency DLLs from %QT_TARGET_DIR%\bin.
        set "PATH=%QT_TARGET_DIR%\bin;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\brotli;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\tools\protobuf;%MY_BUILD_DIR%\vcpkg_installed\%VCPKG_DEFAULT_TRIPLET%\bin;%MY_BUILD_DIR%\bin;%PATH%"
    ) else (
        echo Error: Qt vcpkg dependency bin directory was not found under %QT_BUILD_DIR%.
        echo Error: %QT_DEPS_READY_MARKER% is also missing, so this C:\qt-static install predates the dependency DLL fix.
        echo Error: Re-run this Jenkins job with REBUILD_WINDOWS_DEPS checked, or set QT_DEPS_BIN to the vcpkg bin directory used to build static Qt.
        exit /b 1
    )
)

echo PATH is: %PATH%


echo "Calling cmake for nextapp"
cmake -S "%SOURCE_DIR%/../.." -B "%MY_BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%CMAKE_TOOLCHAIN_FILE%" ^
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

cpack --verbose -G NSIS
if errorlevel 1 (
    echo Failed to make the installer
    echo NSISOutput.log:
    echo type "D:/a/next-app/next-app/build/nextapp/_CPack_Packages/win64/NSIS/NSISOutput.log"
    echo --------------------------------
    type "D:/a/next-app/next-app/build/nextapp/_CPack_Packages/win64/NSIS/NSISOutput.log"
    echo --------------------------------
    exit /b
)

copy /Y "%MY_BUILD_DIR%\*.exe" "%BUILD_DIR%\"
if errorlevel 1 (
    echo Failed to copy the executable installer
    exit /b
)

popd
