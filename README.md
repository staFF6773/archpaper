# archpaper

Wallpaper manager for **Wayland** on Arch Linux and derivatives, with **Qt6 GUI** and integrated CLI for autostart/scripts.

## Features

- **Qt6 GUI** inspired by Wallpaper Engine:
  - Dark sidebar with quick sections: Home, Favorites, Recent, Market and Settings.
  - Top bar with title, search and backend/mode selectors.
  - Folder panel with your favorite wallpaper directories.
  - Responsive thumbnail grid with rounded cards and file names.
  - Hero preview panel with image info, favorite toggle and apply controls.
- **Built-in Market** to browse and download wallpapers directly from **Wallhaven** (static images) and **MoeWalls** (live/animated wallpapers) without opening a browser.
- **Quick filter** by file name across the current section.
- **Double click** to apply a wallpaper directly.
- **Favorites** and **Recent** wallpapers tracked automatically.
- Backends **swaybg** (universal Wayland), **hyprpaper** (Hyprland), **awww** (efficient animated/GIF wallpapers) and **mpvpaper** (video wallpapers).
- Automatic backend detection: prefers **awww** when available because it is the most efficient for animated and static wallpapers on Wayland.
- Animated wallpaper support: GIF/WebP/MP4/WebM/MKV/MOV with automatic backend selection.
- Optimized preview: only the selected wallpaper plays animation; it stops when switching to another item.
- Daemon mode for automatic wallpaper changes by interval.
- Persistent configuration in `~/.config/archpaper/config`.

## Dependencies

```text
swaybg
qt6-base
qt6-multimedia
qt6-network
```

Optional:

```text
hyprpaper
awww      # efficient animated/GIF wallpapers on Wayland
mpvpaper  # video wallpapers on Wayland
wallust
```

To build:

```text
cmake
base-devel
```

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install

```bash
sudo cmake --install build --prefix /usr
```

Or from PKGBUILD:

```bash
makepkg -si
```

## Usage

### GUI

```bash
archpaper
```

In the window:
- Use the **sidebar** to switch between Home, Favorites, Recent, Market and Settings.
- Select a folder from the folder panel (or add/remove folders).
- Click a thumbnail to see the preview in the right panel.
- Double-click or press **Apply** to set the wallpaper.
- Use the search box to filter by name.
- Press the star button to add/remove wallpapers from Favorites.
- Open **Market** to browse wallpapers from Wallhaven and MoeWalls, download them, or download and apply them directly.
- Open Settings to configure the backend, mode, wallust, the daemon and Market options (download folder, Wallhaven API key/purity).

### Market

The **Market** section lets you search online wallpaper sources without opening a browser.

- **Source:** search Wallhaven (static images), MoeWalls (live/animated videos) or both.
- **Purity:** filter Wallhaven results by SFW, Sketchy, NSFW or All (NSFW/All require a Wallhaven API key).
- **Download folder:** choose where downloaded wallpapers are saved (defaults to `~/Pictures/Wallpapers`).
- **Download** saves the wallpaper to your folder; **Download & Apply** also sets it immediately.

### CLI

```bash
archpaper set <image|video|gif> [--mode fill|fit|stretch|center|tile] [--backend swaybg|hyprpaper|awww|mpvpaper] [--wallust] [--wallust-hook <script>]
archpaper random <directory> [--wallust] [--wallust-hook <script>]
archpaper daemon <directory> --interval <seconds> [--wallust] [--wallust-hook <script>]
archpaper clear
archpaper status
archpaper backend
```

## Animated wallpapers

`archpaper` supports GIF, animated WebP, MP4, WebM, MKV, MOV and AVI files.

- **awww** is the preferred backend for animated images (GIF/WebP) and static images on Wayland; it is lightweight and fast.
- **mpvpaper** is used for video files (MP4/WebM/MKV/MOV/AVI).

If you select a backend such as `swaybg` or `hyprpaper` and apply an animated file, `archpaper` automatically switches to a compatible backend when available. The GUI previews the animation only for the selected item and stops it when you select another wallpaper, keeping CPU/GPU usage low.

To use animated wallpapers with the CLI:

```bash
# Requires awww-daemon running for awww
archpaper set ~/Wallpapers/animation.gif --backend awww

# Requires mpvpaper for video
archpaper set ~/Wallpapers/video.mp4 --backend mpvpaper
```

## Wallust integration

If `wallust` is installed, you can automatically generate a color scheme from the wallpaper:

```bash
archpaper set ~/Pictures/wallpaper.jpg --wallust
```

You can also enable it in the GUI by checking *Generate scheme with wallust*. The setting is saved in `~/.config/archpaper/config` under `wallust=true|false`.

`archpaper` runs `wallust run <image>`, which makes **wallust use your own configuration** from `~/.config/wallust/wallust.toml` to write its templates and execute its hooks. So if your `wallust.toml` has a `[hooks.reload]` section that reloads waybar, kitty, etc., it already works without adding anything else.

### Optional additional hook

If you need to run something outside of `wallust.toml`, use the *Hook* field in the GUI or `--wallust-hook <script>` in the CLI:

```bash
archpaper set ~/Pictures/wallpaper.jpg --wallust --wallust-hook ~/.config/archpaper/extra_hook.sh
```

The script receives the wallpaper as `$1` and in the `$WALLPAPER` environment variable:

```bash
#!/bin/bash
# Extra post-wallust commands
killall -SIGUSR2 waybar 2>/dev/null
killall -USR1 kitty 2>/dev/null
makoctl reload 2>/dev/null
```

### GUI configuration

The Settings section (gear icon in the sidebar) contains:
- **Backend** and **Mode** selectors in the top bar.
- **Checkbox** *Generate scheme with wallust* enables wallust on every wallpaper change.
- **Hook:** optional extra script to run after wallust. If left empty, only `wallust run` is executed and your `wallust.toml` handles the rest.
- **Daemon** controls for automatic wallpaper changes by interval.

Favorites and recent wallpapers are stored in `~/.config/archpaper/favorites` and `~/.config/archpaper/recent`.

## Composer integration

### Hyprland

```ini
exec-once = archpaper set ~/Pictures/wallpaper.jpg
```

### Sway

```sway
exec archpaper set ~/Pictures/wallpaper.jpg
```

## Project structure

```text
include/archpaper/  # Public core API in C
src/core/           # Core: backend, config, daemon, utils, wallust
src/cli/            # CLI entry point
src/gui/            # Qt6 GUI
  components/       # Reusable UI widgets
  models/           # (future) data models
  delegates/        # (future) item delegates
  services/         # (future) config/thumbnail services
  theme/            # QSS stylesheet and resource file
```

## License

This program is free software: you can redistribute it and/or modify
it under the terms of the **GNU General Public License v3.0** or later.
See the [LICENSE](LICENSE) file for the full text.
