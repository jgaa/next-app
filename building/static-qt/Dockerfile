# Use a base image with a compatible Linux distribution
FROM ubuntu:22.04

# Set environment variables
ENV QT_VERSION=6.8.2
ENV QT_DIR=/opt/qt-build
ENV QT_INSTALL_DIR=/opt/qt-static
ENV VCPKG_ROOT=/opt/vcpkg
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary tools (excluding multimedia-related dependencies)
RUN apt-get update && apt-get install -y \
    autoconf \
    build-essential \
    clang \
    libclang-dev \
    cmake \
    curl \
    git \
    libicu-dev \
    file \
    dpkg-dev \
    libasound2-dev \
    libpulse-dev \
    libsndfile1-dev \
    libbz2-dev \
    libcups2-dev \
    libcupsfilters-dev \
    libcurl4-openssl-dev \
    libegl1-mesa-dev \
    libfontconfig1-dev \
    libfreetype-dev \
    libgl1-mesa-dev \
    libglew-dev \
    libglu1-mesa-dev \
    libharfbuzz-dev \
    libhunspell-dev \
    libjpeg-dev \
    libpng-dev \
    libsm-dev \
    libsqlite3-dev \
    libwayland-dev \
    libx11-dev \
    libx11-xcb-dev \
    libxcb-cursor-dev \
    libxcb-glx0-dev \
    libxcb-icccm4-dev \
    libxcb-image0-dev \
    libxcb-keysyms1-dev \
    libxcb-randr0-dev \
    libxcb-render-util0-dev \
    libxcb-render0-dev \
    libxcb-shape0-dev \
    libxcb-shm0-dev \
    libxcb-sync-dev \
    libxcb-util-dev \
    libxcb-util0-dev \
    libxcb-xfixes0-dev \
    libxcb-xinerama0-dev \
    libxcb-xinput-dev \
    libxcb-xkb-dev \
    libxcb1-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    libxrandr-dev \
    libxrender-dev \
    nasm \
    ninja-build \
    openssl \
    perl \
    pkg-config \
    python3 \
    python3-pip \
    zip \
    unzip \
    tar \
    zlib1g-dev \
    lsb-release \
    flatpak \
    flatpak-builder \
    sudo && \
    rm -rf /var/lib/apt/lists/*

# Set up Flatpak
RUN flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo && \
    flatpak --noninteractive install flathub org.freedesktop.Platform//23.08 && \
    flatpak --noninteractive install flathub org.freedesktop.Sdk//23.08

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} && \
    ${VCPKG_ROOT}/bootstrap-vcpkg.sh && \
    cd ${VCPKG_ROOT} && \
    ./vcpkg install protobuf grpc openssl && \
    rm -rf $VCPKG_ROOT/buildtrees $VCPKG_ROOT/downloads

# Install Boost
RUN mkdir -p /tmp/boost && \
    curl -sL https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.bz2 | tar -xjC /tmp/boost --strip-components 1 && \
    cd /tmp/boost && \
    ./bootstrap.sh && \
    ./b2 -d0 link=static threading=multi --with-program_options --with-system install && \
    cd / && \
    rm -rf /tmp/boost

# Build and install Qt
RUN mkdir -p ${QT_DIR} && \
    git clone --depth=1 --branch ${QT_VERSION} git://code.qt.io/qt/qt5.git ${QT_DIR}/qt && \
    cd ${QT_DIR}/qt && \
    ./init-repository --module-subset=default,-qtwebengine && \
    ./configure -prefix ${QT_INSTALL_DIR} -static -release -opensource -confirm-license -no-pch \
    -nomake examples -nomake tests -openssl-linked -opengl desktop -sql-sqlite \
    -skip qtwebengine -skip qtmultimedia -skip qtsensors -skip qtconnectivity -skip qtnetworkauth \
    -vcpkg --feature-protobufquick --feature-qtprotobufgen -feature-xcb -qt-libpng -qt-libjpeg \
    -skip qtspeech -skip qt5compat -skip qtquick3dphysics -skip qtremoteobjects -skip qthttpserver && \
    cmake --build . --parallel && \
    cmake --install . && \
    cd / && \
    rm -rf ${QT_DIR}

# RUN mkdir -p ${QT_DIR} && \
#     git clone --depth=1 --branch ${QT_VERSION} git://code.qt.io/qt/qt5.git ${QT_DIR}/qt && \
#     cd ${QT_DIR}/qt && \
#     ./init-repository --module-subset=default,-qtwebengine && \
#     ./configure -prefix ${QT_INSTALL_DIR} -static -release -opensource -confirm-license -no-pch \
#     -nomake examples -nomake tests -openssl-linked -opengl desktop -sql-sqlite \
#     -skip qtwebengine -skip qtmultimedia -skip qtsensors -skip qtconnectivity -skip qtnetworkauth \
#     -vcpkg --feature-protobufquick --feature-qtprotobufgen -feature-xcb -qt-libpng -qt-libjpeg \
#     -skip qtspeech -skip qt5compat -skip qtquick3dphysics -skip qtremoteobjects -skip qthttpserver \
#     && cmake --build . --parallel \
#     && cmake --install . \
#     && cd / && rm -rf ${QT_DIR}/qt

# Create a non-root user
RUN useradd -m -s /bin/bash build && \
    echo "build ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER build
WORKDIR /home/build

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Default command
CMD ["/bin/bash"]
