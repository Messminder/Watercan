#!/usr/bin/env bash
set -euo pipefail

# Best-effort single binary build (option B):
# - Builds with -DBUILD_SINGLE_BINARY=ON which attempts to link libstdc++/libgcc statically
# - Keeps libGL (OpenGL) dynamic for compatibility
# Usage: ./scripts/build_single.sh /path/to/build-dir

BUILD_DIR=${1:-build-single}
SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring with BUILD_SINGLE_BINARY=ON (Release)"
cmake "$SRC_DIR" -DBUILD_SINGLE_BINARY=ON -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build . -j $(nproc)

echo "Build complete. Binary is in: $(pwd)/Watercan (or bin/Watercan)"

# Note: this is a best-effort build. If the executable still depends on system GL or other dynamic
# libs, consider running on a target test machine or using the provided packaging script to bundle
# dynamic libs. Static linking of OpenGL drivers and system glibc is generally not portable.
