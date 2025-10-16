#!/usr/bin/env bash

# *** BROKEN
# As of now, qtgrpc is nor partt of the kde runtile, and I am notg able
# to build it as a module. Even if the libraries build, the cmake support
# for the tools is absent.
#
# Adding it anyway, as this was extremely time consuming to work with.
# Don't want to lose it if/when qtgrps-tools is fixed, or I figure out
# how to enable it.

set -euo pipefail

APP_ID="eu.lastviking.NextApp"
MANIFEST="flatpak/${APP_ID}.yml"
BUILD_DIR="${BUILD_DIR:-flatpak-build}"
REPO_DIR="${REPO_DIR:-flatpak-repo}"

flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install -y --user flathub org.kde.Sdk//6.9 org.kde.Platform//6.9

rm -rf "${BUILD_DIR}" "${REPO_DIR}"

flatpak-builder --user --force-clean --repo="${REPO_DIR}" "${BUILD_DIR}" "${MANIFEST}"

# Lint (nice-to-have; won’t fail the build if missing rules)
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest "${MANIFEST}" || true
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo "${REPO_DIR}" || true

flatpak remote-add --if-not-exists --user nextapp-local "file://$(pwd)/${REPO_DIR}"
flatpak install -y --user nextapp-local "${APP_ID}"

echo "Run with: flatpak run ${APP_ID}"
