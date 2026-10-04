#!/bin/sh
# Configure, build and test CloudScope on Linux (Debian 13 / Raspberry Pi OS).
#   tools/build/build.sh [config] [extra cmake configure arguments]
#   config: Debug (default), RelWithDebInfo or Release
set -eu

config="${1:-Debug}"
[ "$#" -gt 0 ] && shift

case "$(uname -m)" in
    x86_64) preset="linux-x64" ;;
    aarch64) preset="linux-aarch64" ;;
    *) echo "Unsupported machine: $(uname -m)" >&2; exit 1 ;;
esac

cd "$(dirname "$0")/../.."
cmake --preset "$preset" "$@"
cmake --build --preset "$preset" --config "$config"
ctest --preset "$preset" -C "$config"
