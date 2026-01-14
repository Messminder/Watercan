# Building a single Linux executable with embedded assets

This document explains how to build a Linux binary that embeds the About image (`TheBrokenMind.png`) into the executable so it can display the About dialog without requiring the image file on disk.

## Notes about static linking on Linux

- Fully static binaries on Linux (linking glibc statically) can be fragile and are not recommended in many cases. This guide will produce a binary that embeds assets, but some system libraries (drivers, graphics stack) will remain dynamic.
- If you want the smallest and most portable result, consider packaging with an AppImage or using containerization rather than fully static linking.

## Prerequisites

- CMake and build essentials
  - Debian/Ubuntu: sudo apt install build-essential cmake python3 xxd
  - Optionally, to attempt more static result: install libgl1-mesa-dev and other libs, but still dynamic drivers required.

## Build steps

1. Configure with the build option:

```bash
mkdir -p build-linux && cd build-linux
cmake -DBUILD_SINGLE_EXE=ON -DCMAKE_BUILD_TYPE=Release ..
```

2. Build:

```bash
cmake --build . --config Release -- -j$(nproc)
```

3. The produced `Watercan` binary will embed the About image. Sample JSON files like `seasonal_spiritshop.json` are **not** embedded or copied at configure/build time — provide them alongside the binary or open them with **File → Open** in the application at runtime.

## Running the binary

- Run `./Watercan` locally in a desktop session to verify GUI starts. If running headless, the app may fail to create a window.

## Packaging recommendations

- For a single distributable artifact on Linux, consider using AppImage or Snap for easier distribution across different distributions and glibc versions.
- AppImage wraps the bin + dynamic libs into a single file without requiring static glibc.

If you want, I can produce an AppImage build script next or try building and running the `build-linux/Watercan` binary here (note: requires an X display for GUI verification).

## Packaging a tarball release (quick)

A simple helper script is provided to create a release tarball containing the built binary and runtime assets (res/, README, LICENSE, docs). It also writes a small `*.ldd.txt` file that lists the binary's dynamic library dependencies.

From the repository root, run:

```bash
# create a tar.gz in ./dist (default) containing the built release
./scripts/package_release.sh build dist
```

After completion you will find `dist/Watercan-<version>-linux.tar.gz` and a `dist/Watercan-<version>-linux.ldd.txt` file listing runtime dependencies.

This is a quick distribution bundle useful for testing on other machines; for wider compatibility consider AppImage or containerized packaging as described above.