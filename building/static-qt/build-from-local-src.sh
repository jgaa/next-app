#!/usr/bin/bash

# Set default number of cores to the system's core count (nproc)
NUM_CORES=${1:-$(nproc)}

cd
rm -rf build
mkdir build
cd build

# Generate CMakePresets.json file
cat <<EOF > CMakePresets.json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 19 },
  "configurePresets": [
    {
      "name": "static-build",
      "description": "Default preset to prefer static libraries",
      "hidden": false,
      "default": true,
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_FIND_LIBRARY_SUFFIXES": ".a;.lib;.so;.dll",
        "BUILD_SHARED_LIBS": "OFF"
      }
    }
  ]
}
EOF

# Run CMake and build with the specified or default core count
cmake /next-app/src/NextAppUi \
  -DCMAKE_PREFIX_PATH=/opt/qt-static \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DUSE_STATIC_QT=ON \
  -DQT_QMAKE_EXECUTABLE:FILEPATH=/opt/qt-static/bin/qmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux && cmake --build . -j${NUM_CORES}

# Copy the built executable to the target directory
cp -v bin/nextapp /target/

echo "Done!"
