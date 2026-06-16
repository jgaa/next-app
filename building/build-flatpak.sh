#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Keep the legacy entrypoint, but always route through the maintained KDE 6.10+
# build so we do not accidentally ship a Flatpak with a bundled QtGrpc module.
exec "${SCRIPT_DIR}/build-flatpak-kde.sh" "$@"
