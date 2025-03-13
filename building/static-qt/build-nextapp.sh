#!/usr/bin/bash

chmod 0777 target

time docker build -t qt-static . &&\
time docker run --rm -v `pwd`/../../:/next-app:ro -v `pwd`/target:/target --name qt-static-build-nextapp -it qt-static bash /next-app/building/static-qt/build-from-local-src.sh &&\

echo "Successfully done!"

