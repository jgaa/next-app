# Building QT statically

Build the docker-image for static QT.

```sh

./build-nextapp.sh

```

This will build a docker container with QT built statically under
Ununtu 24.04 (Noble), and then build the nextapp QT app from the locally
checked out source code. The container ends up at about ~23GB and takes about 30
minutes to build on my workstation. The statically built `nextapp` Linux *AMD64* binary
is about 180MB.

