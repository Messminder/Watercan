#!/usr/bin/env bash
set -euo pipefail

# Build an AppImage for Watercan
# Usage: scripts/build_appimage.sh [build-dir] [out-dir]
# Defaults: build-dir=build, out-dir=Standalone

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}/.."
BUILD_DIR="${1:-${ROOT_DIR}/build}"
OUT_DIR="${2:-${ROOT_DIR}/Standalone}"

echo "Using BUILD_DIR=${BUILD_DIR} OUT_DIR=${OUT_DIR}"

# Check binary
if [[ ! -x "${BUILD_DIR}/Watercan" ]]; then
  echo "Error: Watercan binary not found or not executable in ${BUILD_DIR}. Build first." >&2
  exit 2
fi

APPDIR="${OUT_DIR}/Watercan.AppDir"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/icons/hicolor/256x256/apps" "${APPDIR}/res"

# Copy files
cp "${BUILD_DIR}/Watercan" "${APPDIR}/usr/bin/Watercan"
cp -r "${ROOT_DIR}/res" "${APPDIR}/res" || true
cp "${ROOT_DIR}/README.md" "${APPDIR}/" || true
cp "${ROOT_DIR}/LICENSE" "${APPDIR}/" || true
cp "${ROOT_DIR}/DOCS_LINUX_BUILD.md" "${APPDIR}/" || true

# Ensure the main executable is executable
chmod +x "${APPDIR}/usr/bin/Watercan"

# Desktop file
cat > "${APPDIR}/usr/share/applications/Watercan.desktop" <<'EOF'
[Desktop Entry]
Name=Watercan
Exec=Watercan
Icon=watercan
Type=Application
Categories=Graphics;Utility;
EOF
# Also place a copy of the .desktop file at the AppDir root (required by appimagetool)
cp "${APPDIR}/usr/share/applications/Watercan.desktop" "${APPDIR}/Watercan.desktop" || true

# Icon: use TheBrokenClip.png as the application icon (resizing not performed)
ICON_SRC="${ROOT_DIR}/res/TheBrokenClip.png"
if [[ -f "${ICON_SRC}" ]]; then
  # Copy to the common size used by AppImage desktop integration
  cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/watercan.png" || true
  # Place a top-level copy for appimagetool
  cp "${ICON_SRC}" "${APPDIR}/watercan.png" || true
fi

# AppRun: small launcher that sets working dir and execs binary
cat > "${APPDIR}/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
cd "${HERE}"
exec "$HERE/usr/bin/Watercan" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# Try to use linuxdeploy to bundle dependencies and create AppImage via appimagetool
# We'll prefer linuxdeploy (with appimage plugin) if available, otherwise fall back to using appimagetool after copying libs.

# Helper to download tools to a temp dir
dl_tool() {
  local url="$1"; local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -L -o "${out}" "${url}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${out}" "${url}"
  else
    echo "Error: curl or wget required to download AppImage toolchain" >&2
    return 1
  fi
  chmod +x "${out}"
}

TMPDIR=$(mktemp -d)
trap 'rm -rf "${TMPDIR}"' EXIT

LINUXDEPLOY="${TMPDIR}/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL="${TMPDIR}/appimagetool-x86_64.AppImage"

# Download linuxdeploy if not present locally
if command -v linuxdeploy >/dev/null 2>&1; then
  LINUXDEPLOY_CMD="linuxdeploy"
else
  echo "Downloading linuxdeploy..."
  dl_tool "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "${LINUXDEPLOY}"
  LINUXDEPLOY_CMD="${LINUXDEPLOY}"
fi

# Download appimagetool if needed
if ! command -v appimagetool >/dev/null 2>&1; then
  echo "Downloading appimagetool..."
  dl_tool "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" "${APPIMAGETOOL}"
  APPIMAGETOOL_CMD="${APPIMAGETOOL}"
else
  APPIMAGETOOL_CMD="appimagetool"
fi

# Try to run linuxdeploy with appimage output
echo "Running linuxdeploy to bundle runtime dependencies..."
# linuxdeploy supports --output appimage which produces the final AppImage if appimagetool is available
# We pass icon and desktop file locations
set -x
"${LINUXDEPLOY_CMD}" --appdir "${APPDIR}" --desktop-file "${APPDIR}/usr/share/applications/Watercan.desktop" --executable "${APPDIR}/usr/bin/Watercan" --icon-file "${APPDIR}/usr/share/icons/hicolor/256x256/apps/watercan.png" --output appimage || true
set +x

# If linuxdeploy produced an AppImage in the current dir, move it to OUT_DIR
APPIMAGE_OUTPUT="$(ls -1t ./*.AppImage 2>/dev/null | head -n1 || true)"
if [[ -n "${APPIMAGE_OUTPUT}" ]]; then
  mkdir -p "${OUT_DIR}"
  mv "${APPIMAGE_OUTPUT}" "${OUT_DIR}/Watercan.AppImage"
  echo "Created AppImage: ${OUT_DIR}/Watercan.AppImage"
  exit 0
fi

# Otherwise fall back to using appimagetool on the AppDir
echo "linuxdeploy did not produce an AppImage; using appimagetool on the AppDir..."
"${APPIMAGETOOL_CMD}" "${APPDIR}" "${OUT_DIR}/Watercan.AppImage"

echo "Created AppImage: ${OUT_DIR}/Watercan.AppImage"

exit 0
