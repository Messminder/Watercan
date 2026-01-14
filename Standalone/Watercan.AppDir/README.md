# Watercan

**Sky: Children of the Light Spirit Tree Viewer and Editor**

Watercan is a cross-platform desktop application for viewing and editing spirit trees from Sky: Children of the Light. It reads JSON data files containing spirit tree information and visualizes them as interactive dependency trees that closely mimic in-game spirit tree designs that were used since Sky version 0.4.X until 0.31.X.

## Features

- **Spirit List**: Browse all spirits from the loaded JSON file, with search filtering
- **Tree Visualization**: View spirit trees with proper dependency layout
- **Interactive Viewport**: Pan (right-click drag) and zoom (scroll wheel) the tree view. It even has collision detection!
- **Node Information**: See node types, costs, and Adventure Pass status at a glance and edit them quickly and conveniently.
- **Cross-Platform**: Works on Windows, macOS, and Linux

## Node Types & Colors

- **Blue (O)**: Outfit items
- **Purple (E)**: Emote/Spirit upgrades  
- **Gold (M)**: Music sheets
- **Red (L)**: Lootbox items/Spells
- **Golden border**: Root nodes
- **Gold star indicator**: Adventure Pass items

## Building

### Prerequisites

- CMake 3.16 or higher
- C++17 compatible compiler
- OpenGL development libraries

**Linux (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install build-essential cmake libgl1-mesa-dev libgtk-3-dev
```

**Linux (Fedora):**
```bash
sudo dnf install cmake gcc-c++ mesa-libGL-devel gtk3-devel
```

**Linux (Arch Linux):**
```bash
# Update packages and install development tools and OpenGL/GTK dependencies
sudo pacman -Syu --needed base-devel cmake pkgconf glfw gtk3 mesa
```

> Tip: On Arch the package `glfw` provides the GLFW development files; `mesa` provides the OpenGL drivers. If you use Wayland/other toolkits you may need additional packages (e.g., `libx11`, `libxrandr`).

**macOS:**
```bash
xcode-select --install
brew install cmake
```

**Windows:**
- Install Visual Studio 2019 or later with C++ workload
- Install CMake (https://cmake.org/download/)

### Build Steps

1. Clone or download the repository

2. Create build directory and configure:
```bash
cd Watercan
mkdir build
cd build
cmake ..
```

3. Build:
```bash
# Linux/macOS
cmake --build . --config Release

# Windows (Visual Studio)
cmake --build . --config Release
# Or open the generated .sln file in Visual Studio
```

4. Run:
```bash
# Linux/macOS
./Watercan

# Windows
# Note: Windows packaging guidance has been removed from the docs while we work on a single-exe plan.
```

> Note: Sample JSON data files (for example `seasonal_spiritshop.json`) are runtime assets and are **not** required to configure or build the project. The application will open JSON files at runtime via **File → Open** or by placing them alongside the executable.

## Usage

1. Launch Watercan
2. Go to **File > Open** or press `Ctrl+O`
3. Select a spirit shop JSON file (e.g., `seasonal_spiritshop.json`)
4. Browse spirits in the left panel
5. Click a spirit to view its tree
6. Use scroll wheel to zoom, right-click drag to pan
7. Press `R` or click "Reset View" to reset the viewport

## JSON Format

Watercan expects JSON files with the following structure:
```json
[
  {
    "id": 1234567890,
    "dep": 0,
    "nm": "item_name",
    "spirit": "spirit_name",
    "typ": "outfit|spirit_upgrade|music|lootbox",
    "ctyp": "season_candle",
    "cst": 10,
    "ap": false
  }
]
```

### Field Descriptions

| Field | Description |
|-------|-------------|
| `id` | Unique node identifier |
| `dep` | Parent node ID (0 = root node) |
| `nm` | Item name |
| `spirit` | Spirit this node belongs to |
| `typ` | Item type |
| `ctyp` | Cost type (currency) |
| `cst` | Cost amount |
| `ap` | Adventure Pass exclusive (true/false) |

## Tree Layout Rules

- Root node (dep=0) is positioned at the bottom (south)
- Child nodes branch upward (north)
- Up to 3 children per node:
  - 1 child: Directly above (straight trunk)
  - 2 children: Split left and right
  - 3 children: Left, center, right (full branch)

## Controls

| Action | Control |
|--------|---------|
| Zoom | Scroll wheel |
| Pan | Right-click + drag |
| Reset view | Press `R` or click "Reset View" |
| Open file | `Ctrl+O` |

## License

This project is provided as-is for educational and personal use.

## Acknowledgments

- Built with [Dear ImGui](https://github.com/ocornut/imgui)
- Uses [GLFW](https://www.glfw.org/) for window management
- JSON parsing via [nlohmann/json](https://github.com/nlohmann/json)
- File dialogs by [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)
