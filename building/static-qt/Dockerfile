# Use a base image with a compatible Linux distribution
FROM ubuntu:24.04

# Set environment variables
ENV QT_VERSION=6.8
ENV QT_DIR=/opt/qt-build
ENV QT_INSTALL_DIR=/opt/qt-static
ENV VCPKG_ROOT=/opt/vcpkg
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary tools
RUN apt-get update && apt-get install -y \
    autoconf \
    build-essential \
    clang \
    cmake \
    curl \
    flite1-dev \
    git \
    googletest \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-ugly \
    libasound2-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libclang-dev \
    libcups2-dev libcupsfilters-dev \
    libcurl4-openssl-dev \
    libfontconfig1-dev \
    libfreetype-dev \
    libgl1-mesa-dev libglew-dev libglu1-mesa-dev libegl1-mesa-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-dev \
    libharfbuzz-dev \
    libhunspell-dev \
    libjpeg-dev \
    libjpeg-turbo8-dev \
    libpng-dev \
    libpulse-dev \
    libsqlite3-dev \
    libswscale-dev \
    libv4l-dev \
    libva-dev \
    libvdpau-dev \
    libx11-dev \
    libx11-dev libxext-dev libxrandr-dev libxrender-dev libwayland-dev \
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
    libxrender-dev \
    nasm \
    ninja-build \
    openssl \
    perl \
    pkg-config \
    python3 \
    python3-pip \
    wget \
    zip unzip tar wget \
    zlib1g-dev \
    libsm-dev \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} && \
    ${VCPKG_ROOT}/bootstrap-vcpkg.sh

# Install necessary Qt dependencies using vcpkg
WORKDIR ${VCPKG_ROOT}
RUN ./vcpkg install protobuf grpc openssl ffmpeg \
    boost-system boost-context boost-coroutine boost-filesystem \
    boost-asio boost-chrono boost-date-time boost-log boost-regex boost-json \
    boost-url boost-uuid boost-mysql boost-charconv boost-program-options boost-functional

# Create directory for Qt installation
RUN mkdir -p ${QT_DIR}

# Clone Qt source code
RUN git clone --branch ${QT_VERSION} git://code.qt.io/qt/qt5.git ${QT_DIR}/qt

# Initialize the repository and synchronize submodules
WORKDIR ${QT_DIR}/qt

RUN ./init-repository --module-subset=default,-qtwebengine

RUN ./configure -prefix ${QT_INSTALL_DIR} -static -release -opensource -confirm-license -no-pch -nomake examples -nomake tests -openssl-linked -opengl desktop -sql-sqlite -skip qtwebengine -vcpkg --feature-protobufquick --feature-qtprotobufgen -feature-xcb
# Build and install Qt
RUN cmake --build . --parallel
RUN cmake --install .

RUN egrep -i 'grpc|proto' config.summary

# Default command
CMD ["/bin/bash"]
