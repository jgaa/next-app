# Building QT statically

Build the docker-image for static QT.

```sh

docker build -t qt-static .

```

To build NextApp, I use this manual steps at the moment.

```sh

docker run -v {$HOME}/src/next-app/:/next-app:ro --rm --name qt-static -it qt-static

adduser build

su - build
mkdir build
cd build

cmake /next-app/src/NextAppUi  -DCMAKE_PREFIX_PATH=/opt/qt-static   -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake   -DVCPKG_TARGET_TRIPLET=x64-linux-static  -DUSE_STATIC_QT=ON -DQT_QMAKE_EXECUTABLE:FILEPATH=/opt/qt-static/bin/qmake -DVCPKG_TARGET_TRIPLET=x64-linux && VERBOSE=1 cmake --build . -j

```

This build the nextapp app for Linux, but unfortunately, it crashes if I start it
under Ubuntu Noble. I'll fix that when time allows it.

