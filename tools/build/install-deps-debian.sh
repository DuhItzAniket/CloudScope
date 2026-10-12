#!/bin/sh
# Installs everything needed to build CloudScope on Debian 13 (Trixie), Raspberry Pi OS based on it,
# and Ubuntu 26.04. Older releases (Debian 12, Ubuntu 24.04) ship Qt 6.4, which is too old (ADR-001).
# This list is the single source of truth for Linux dependencies: CI and the Docker image use this script.
#
#   sh tools/build/install-deps-debian.sh          (uses sudo when not run as root)
set -eu

if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi
export DEBIAN_FRONTEND=noninteractive

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    catch2 \
    cmake \
    git \
    libcfitsio-dev \
    libexpected-dev \
    libfmt-dev \
    libjpeg-dev \
    libopencv-dev \
    libspdlog-dev \
    libtomlplusplus-dev \
    libturbojpeg0-dev \
    ninja-build \
    nlohmann-json3-dev \
    pkgconf \
    qt6-base-dev \
    qt6-base-private-dev \
    libqt6sql6-sqlite \
    libgl-dev \
    libxcb1-dev \
    libsdl2-dev \
    fonts-dejavu-core
# qt6-base-private-dev and libxcb1-dev: the Qt Advanced Docking System (built from source, ADR-002) uses Qt's
# platform-native interface on Linux. libgl-dev: Qt OpenGL headers. libsdl2-dev: gamepad input (P043).
# fonts-dejavu-core: the user-interface tests render text on the offscreen platform.
