# syntax=docker/dockerfile:1.7
FROM debian:13-slim
# broken with vcpkg?
# FROM ubuntu:25.04

ARG UID=1000
ARG GID=1000
ARG USER=builder

ENV DEBIAN_FRONTEND=noninteractive

# libfontconfig1-dev \
# libexpat1-dev

# --- Base system and build tools ---
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl pkg-config \
    python3 python3-pip python3-setuptools \
    ca-certificates sudo unzip tar xz-utils file jq patchelf \
    bison flex gperf zip tar gzip sudo \
    autoconf automake libtool m4 perl autoconf-archive \
    libtool-bin libltdl-dev python3-jinja2 \
    libx11-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev \
    libxcb1-dev libx11-xcb-dev libxcb-glx0-dev libxcb-keysyms1-dev \
    libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev \
    libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev \
    libxcb-xinerama0-dev libxcb-xinput-dev \
    libxkbcommon-dev libxkbcommon-x11-dev \
    libfreetype6-dev libpng-dev libjpeg-dev libbrotli-dev libharfbuzz-dev \
    libdbus-1-dev libssl-dev libicu-dev libglvnd-dev \
    libgl1-mesa-dev libegl1-mesa-dev libgbm-dev libdrm-dev mesa-common-dev libglvnd-dev \
    libsm-dev libice-dev libxau-dev libxdmcp-dev libudev-dev \
    libxcb-cursor-dev libxcb-xkb-dev libgtk-3-dev libatspi2.0-dev \
    flatpak flatpak-builder ostree bubblewrap \
    libxcursor-dev libxinerama-dev libxxf86vm-dev \
    && rm -rf /var/lib/apt/lists/*

ENV ACLOCAL_PATH=/usr/share/aclocal

ARG USER=builder
ARG UID=1000
ARG GID=1000

RUN set -eux; \
    if getent group "${GID}" >/dev/null; then \
        grp="$(getent group "${GID}" | cut -d: -f1)"; \
    else \
        grp="${USER}"; \
        groupadd -g "${GID}" "${grp}"; \
    fi; \
    if getent passwd "${UID}" >/dev/null; then \
        usr="$(getent passwd "${UID}" | cut -d: -f1)"; \
        if [ "${usr}" != "${USER}" ]; then \
            usermod -l "${USER}" -d "/home/${USER}" -m "${usr}"; \
        fi; \
        usermod -g "${grp}" -s /bin/bash "${USER}"; \
        [ -d "/home/${USER}" ] || mkdir -p "/home/${USER}"; \
        chown -R "${UID}:${GID}" "/home/${USER}"; \
    else \
        useradd -m -u "${UID}" -g "${grp}" -s /bin/bash "${USER}"; \
    fi; \
    apt-get update && apt-get install -y --no-install-recommends sudo && rm -rf /var/lib/apt/lists/*; \
    echo "${USER} ALL=(ALL) NOPASSWD: ALL" > "/etc/sudoers.d/${USER}"; \
    chmod 0440 "/etc/sudoers.d/${USER}"

USER ${USER}
WORKDIR /home/${USER}

# --- Define mount points ---
VOLUME ["/src", "/artifacts", "/vcpkg", "/build", "/cache"]

# --- Environment for vcpkg ---
ENV VCPKG_ROOT=/vcpkg
ENV PATH=$VCPKG_ROOT:$PATH
ENV VCPKG_DEFAULT_BINARY_CACHE=/cache/vcpkg

# --- Env for the build scripts
ENV BUILD_DIR=/build
ENV ASSETS_DIR=/artifacts


# # --- Bootstrap vcpkg if not mounted ---
# RUN if [ ! -d "/vcpkg/.git" ]; then \
#         git clone https://github.com/microsoft/vcpkg.git /vcpkg && \
#         /vcpkg/bootstrap-vcpkg.sh -disableMetrics; \
#     fi

# --- Default build command ---
CMD ["/bin/bash", "-c", "echo $UID && cd /src && ./building/linux/build.sh && ./building/linux/build-flatpak.sh"]

