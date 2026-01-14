# Watercan!
---------
To water and grow **spirit trees!**

Watercan is a dependency tree viewer and editor specialized for Sky: COTL.
It is currently intended for private server software developers, but anyone can use it to create their own spirit trees 
that are compatible with ThatModdingCommunity and NightSky Communal Ark private server softwares.

Currently, this software is not on point for release, but you're welcome to download it's earlybird on each of it's repo branch in the Standalone folder.


## Nice features:
* Interactive dependency tree viewer and editor. Allowing the user to add, delete, link and unlink nodes (broken, see under):
  * Have your own color codes and save user preferences.
  * Nodes even has physics because F U N.
* Easy to use and understandable Node attribute editor:
  * Use the scrollwheel on the Cost (cst) to quickly select numbers.
* There is a raw JSON editor right under if you like:
  * With colorful text when you do the indicated keyboard shortcuts.
* Fully custom solution for browsing files in your computer.
* Real-time reflection of edits everywhere.
* Saves the whole file, or isolate a singular spirit in a singular JSON file.
* Automatic Travelling Spirit identification.
  * This is based on several checks. 

## What's currently lacking:
* Watercan should be able to process names + ID = this icon! So displayed spirit trees would closely mimick what is reflected ingame.
* Linking and unlinking nodes is broken.

## Current issues:
### Tree editor
> When unlinking nodes, the cardinal rules seems extremely stubborn. Breaking the tree appart as it conflicts with the fact nodes can be in a "freeFloating state". You can always do File > Reload.
> Watercan cannot process the item and ids with it's in-game icon, but this feature is greatly desired.
> 





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

## License

This project is provided as-is for educational and personal use.

## Acknowledgments

- Built with [Dear ImGui](https://github.com/ocornut/imgui)
- Uses [GLFW](https://www.glfw.org/) for window management
- JSON parsing via [nlohmann/json](https://github.com/nlohmann/json)
- File dialogs by [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)
