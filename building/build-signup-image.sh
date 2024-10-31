#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export BUILD_PROJECT=signupd
${SCRIPT_DIR}/build-project-image.sh $@

