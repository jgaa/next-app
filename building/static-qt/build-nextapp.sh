#!/usr/bin/bash

if [ -z ${BUILD_IMAGE+x} ]; then
    BUILD_IMAGE=qt-static
    time docker build -t ${BUILD_IMAGE} . &&\
fi

chmod 0777 target

time docker run --rm -v `pwd`/../../:/next-app:ro -v `pwd`/target:/target --name qt-static-build-nextapp -it ${BUILD_IMAGE} bash /next-app/building/static-qt/build-from-local-src.sh &&\
echo "Successfully done!"

