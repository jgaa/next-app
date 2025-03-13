#!/usr/bin/bash
set -o pipefail  # Ensure errors in pipelines cause failure

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
fi

# Ensure target directory exists and set permissions
chmod 0777 target || error_exit "Failed to set permissions on 'target' directory."

# Run the Docker container
time docker run --rm \
    -v "$(pwd)"/../../:/next-app:ro \
    -v "$(pwd)"/target:/target \
    --name qt-static-build-nextapp \
    -it "${BUILD_IMAGE}" \
    bash /next-app/building/static-qt/build-from-local-src.sh || error_exit "Docker run failed."

echo "✅ Done!"
