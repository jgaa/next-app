#!/usr/bin/bash

if [ -z ${BUILD_IMAGE+x} ]; then
    BUILD_IMAGE=qt-static
fi

chmod 0777 target

time docker build -t qt-static . &&\
time docker run --rm -v `pwd`/../../:/next-app:ro -v `pwd`/target:/target --name qt-static-build-nextapp -it ${BUILD_IMAGE} bash /next-app/building/static-qt/build-from-local-src.sh &&\
echo "Successfully done!"

