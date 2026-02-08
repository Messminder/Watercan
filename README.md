# Watercan!
To water and grow **spirit trees!**

Watercan is a dependency tree viewer and editor specialized for Sky: Children Of The Light.
It is currently intended for private server software developers, but anyone can use it to create their own spirit trees that are compatible with ThatModdingCommunity, NightSky Communal Ark and other private server softwares.

Currently, this software is **almost** on point for release, but you're welcome to download it's earlybird on each of it's repo branch in the Standalone folder. It supports all spirits from The Season of Belonging until The Season of the Two Embers Part I. (Sky: Light awaits -> Sky: COTL 0.32.0)

Further knowledge gathering is requiered to support spirits from Season of Migration and onward.

## Nice features:
* Interactive dependency tree viewer and editor. Allowing the user to add, delete, link and unlink nodes
  * Have your own color codes and save user preferences.
  * Nodes even has physics because F U N.
  * Multi-select node support and selection box drawing.
  * Elastic relationship lines. They act like rubberbands that can snap if you pull them too far apart. Creating a fun way to manage dependencies.
  * Context menu for quick commands.
* Easy to use and understandable Node attribute editor:
  * Use the scrollwheel on the Cost (cst) to quickly select numbers.
  * FNV1a32 id and programatic name mismatch detection and automatic fix at the click of a button.
  * The restore name by ID is based of the loaded JSON, and will not reverse the FNV1a32 hash.
* There is a raw JSON editor right under if you like:
  * With colorful text when you do the indicated keyboard shortcuts. This is a mini code editor, not a Multiline edit!
  * When selecting multiple nodes, it reflects your selections in real-time all the while keeping JSON object order.
* Fully custom solution for browsing files in your computer!
* Real-time reflection of edits everywhere, yet still performant.
* Saves the whole file, or isolate a singular spirit in a singular JSON file.
* Automatic Travelling Spirit identification.
  * This is based on several checks.

## Screenshots

![Tree Editor - Overview](media/Screenshot%20From%202026-02-08%2000-49-50.png "Tree Editor - Overview")

*Overview of the tree editor showing nodes in a beautiful imgui interface.*

![How pretty](media/Screenshot%20From%202026-02-08%2000-50-46.png "Restore Flow Animation")

*Seriously, what a beautiful piece of software.*

## What's currently lacking:
* Watercan should be able to process names + ID = this icon! So displayed spirit trees would closely mimick what is reflected ingame



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
| Cut | `CTRL+X` |
| Copy | `CTRL-C`|
| Paste | `CTRL+V` |
| Multiple select | `SHIFT+RightCLick` |

## License

This project is provided as-is under the MIT license.

## Acknowledgments

- Built with [Dear ImGui](https://github.com/ocornut/imgui)
- Uses [GLFW](https://www.glfw.org/) for window management
- JSON parsing via [nlohmann/json](https://github.com/nlohmann/json)
- File dialogs by [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)

## Disclaimer
Watercan can absolutely give you a faulty file that will crash your clients if you misuse it!
