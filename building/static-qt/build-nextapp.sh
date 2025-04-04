#!/usr/bin/bash
set -o pipefail  # Ensure errors in pipelines cause failure

TARGET_DIR="${TARGET_DIR:-$(pwd)/target}"

echo "TARGET_DIR: ${TARGET_DIR}"

# Set default number of cores to the system's core count (nproc)
NUM_CORES=${1:-$(nproc)}

# Function to print error message and exit
error_exit() {
    echo "❌ ERROR: $1" >&2
    exit 1
}

# BUILD_IMAGE whould be set when running as CI job
# If unset, assume it's run manually and build the image locally
if [ -z "${BUILD_IMAGE+x}" ]; then
    BUILD_IMAGE="qt-static"
    time docker build -t "${BUILD_IMAGE}" . || error_exit "Docker build failed."
else
    echo "Using pre-built image: ${BUILD_IMAGE}"
    docker pull "${BUILD_IMAGE}" || error_exit "Failed to pull Docker image."
fi

# Ensure target directory exists and set permissions
mkdir -p "${TARGET_DIR}"
chmod 0777 "${TARGET_DIR}" || error_exit "Failed to set permissions on 'target' directory."

# Run the Docker container
time docker run --rm \
    --cap-add SYS_ADMIN --cap-add=NET_ADMIN --privileged \
    --security-opt apparmor=unconfined  --device /dev/fuse \
    -v "$(pwd)"/../../:/next-app:ro \
    -v "${TARGET_DIR}":/target \
    --name qt-static-build-nextapp \
    -i "${BUILD_IMAGE}" \
    bash -c "bash /next-app/building/static-qt/build-from-local-src.sh ${NUM_CORES} && /next-app/building/static-qt/create_flatpak.sh" || error_exit "Docker run failed."

echo "✅ Done!"
