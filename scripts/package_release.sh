#!/usr/bin/env bash
set -euo pipefail

# Package a Linux release tarball for Watercan.
# Usage: scripts/package_release.sh [build-dir] [out-dir]
# Defaults: build-dir=build, out-dir=dist

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${1:-${ROOT_DIR}/build}"
OUT_DIR="${2:-${ROOT_DIR}/dist}"

mkdir -p "${OUT_DIR}"
# Ensure OUT_DIR is absolute so tar invocation works when run from a temp directory
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

# Locate binary
if [[ -x "${BUILD_DIR}/Watercan" ]]; then
  BINARY_PATH="${BUILD_DIR}/Watercan"
elif [[ -x "${BUILD_DIR}/Release/Watercan" ]]; then
  BINARY_PATH="${BUILD_DIR}/Release/Watercan"
else
  echo "Error: Watercan binary not found in ${BUILD_DIR}. Build the project first." >&2
  exit 2
fi

# Determine version from CMakeLists.txt or fallback
VERSION="1.5.7"
if grep -Po "project\(.*VERSION\s+\K[0-9]+(\.[0-9]+)*" "${ROOT_DIR}/CMakeLists.txt" >/dev/null 2>&1; then
  VERSION=$(grep -Po "project\(.*VERSION\s+\K[0-9]+(\.[0-9]+)*" "${ROOT_DIR}/CMakeLists.txt" | head -n1)
fi

PACKAGE_NAME="Watercan-${VERSION}-linux"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "${TMP_DIR}"' EXIT

mkdir -p "${TMP_DIR}/${PACKAGE_NAME}/usr/bin"
# Copy binary
cp "${BINARY_PATH}" "${TMP_DIR}/${PACKAGE_NAME}/usr/bin/Watercan"
# Copy runtime assets
cp -r "${ROOT_DIR}/res" "${TMP_DIR}/${PACKAGE_NAME}/res" 2>/dev/null || true
cp "${ROOT_DIR}/README.md" "${TMP_DIR}/${PACKAGE_NAME}/" 2>/dev/null || true
cp "${ROOT_DIR}/LICENSE" "${TMP_DIR}/${PACKAGE_NAME}/" 2>/dev/null || true
cp "${ROOT_DIR}/DOCS_LINUX_BUILD.md" "${TMP_DIR}/${PACKAGE_NAME}/" 2>/dev/null || true
cp "${ROOT_DIR}/DOCS_WINDOWS_BUILD.md" "${TMP_DIR}/${PACKAGE_NAME}/" 2>/dev/null || true

# Save ldd output (runtime dependencies) next to the tarball for reference
ldd "${TMP_DIR}/${PACKAGE_NAME}/usr/bin/Watercan" > "${OUT_DIR}/${PACKAGE_NAME}.ldd.txt" 2>/dev/null || true

# Strip binary to reduce size if strip is available
if command -v strip >/dev/null 2>&1; then
  strip --strip-all "${TMP_DIR}/${PACKAGE_NAME}/usr/bin/Watercan" || true
fi

# Create tarball
pushd "${TMP_DIR}" >/dev/null
  tar -czf "${OUT_DIR}/${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
popd >/dev/null

echo "Created ${OUT_DIR}/${PACKAGE_NAME}.tar.gz"
if [[ -f "${OUT_DIR}/${PACKAGE_NAME}.ldd.txt" ]]; then
  echo "Runtime dependency list saved as ${OUT_DIR}/${PACKAGE_NAME}.ldd.txt"
fi

exit 0
