# archpaper

Wallpaper manager for **Wayland** on Arch Linux and derivatives, with **Qt6 GUI** and integrated CLI for autostart/scripts.

## Features

- **Qt6 GUI** with a three-panel layout:
  - Favorite folders on the left.
  - Thumbnails of the selected directory in the center.
  - Large preview and controls on the right.
- **Quick filter** by file name.
- **Double click** to apply a wallpaper directly.
- Backends **swaybg** (universal Wayland) and **hyprpaper** (Hyprland).
- Automatic backend detection on Hyprland.
- Daemon mode for automatic wallpaper changes by interval.
- Persistent configuration in `~/.config/archpaper/config`.

## Dependencies

```text
swaybg
qt6-base
```

Optional:

```text
hyprpaper
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
- Select a folder from the list (or add a new one).
- Click a thumbnail to see the preview.
- Double-click or press **Apply** to set the wallpaper.
- Use the search box to filter by name.
- Enable the daemon for automatic changes.

### CLI

```bash
archpaper set <image> [--mode fill|fit|stretch|center|tile] [--backend swaybg|hyprpaper] [--wallust] [--wallust-hook <script>]
archpaper random <directory> [--wallust] [--wallust-hook <script>]
archpaper daemon <directory> --interval <seconds> [--wallust] [--wallust-hook <script>]
archpaper clear
archpaper status
archpaper backend
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

- **Checkbox** *Generate scheme with wallust* enables wallust on every wallpaper change.
- **Hook:** optional extra script to run after wallust. If left empty, only `wallust run` is executed and your `wallust.toml` handles the rest.

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
```

## License

MIT
