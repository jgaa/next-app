#!/usr/bin/bash

# Start the build-container as
#  docker run --rm -v {$HOME}/src/next-app/:/next-app:ro -v `pwd`/target:/target --name qt-static-build-nextapp -it qt-static bash /next-app/building/static-qt/build-from-local-src.sh

cd
rm -rf build
mkdir build
cd build

RUN echo '{ \
  "version": 3, \
  "cmakeMinimumRequired": { "major": 3, "minor": 19 }, \
  "configurePresets": [ \
    { \
      "name": "static-build", \
      "description": "Default preset to prefer static libraries", \
      "hidden": false, \
      "default": true, \
      "generator": "Ninja", \
      "cacheVariables": { \
        "CMAKE_FIND_LIBRARY_SUFFIXES": ".a;.lib;.so;.dll", \
        "BUILD_SHARED_LIBS": "OFF" \
      } \
    } \
  ] \
}' > CMakePresets.json

cmake /next-app/src/NextAppUi  -DCMAKE_PREFIX_PATH=/opt/qt-static   -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake -DUSE_STATIC_QT=ON -DQT_QMAKE_EXECUTABLE:FILEPATH=/opt/qt-static/bin/qmake -DVCPKG_TARGET_TRIPLET=x64-linux && cmake --build . -j

cp -v bin/nextapp /target/

echo "Done"
