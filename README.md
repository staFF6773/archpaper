# archpaper

Wallpaper manager for **Wayland** on Arch Linux and derivatives, with a **C17 core and standalone C CLI**, plus an optional **Qt6 Widgets GUI**.

## Features

- **Compact Qt6 GUI** with a minimal, neutral dark theme:
  - Slim sidebar with Library, Favorites, Recent and Settings.
  - Section title, wallpaper count and quick search in the header.
  - Folder names in the sidebar, with full paths in tooltips.
  - Responsive thumbnail grid with subtle selection and empty states.
  - Resizable, hideable preview with image information.
  - A single action bar for applying wallpapers, favorites and additional actions.
  - Scrollable Settings with backend/mode selectors and advanced options.
- **Quick filter** by file name across the current section.
- **Double click** to apply a wallpaper directly.
- **Favorites** and **Recent** wallpapers tracked automatically.
- Backends **swaybg** (universal Wayland), **hyprpaper** (Hyprland), **awww** (efficient animated/GIF wallpapers) and **mpvpaper** (video wallpapers).
- Automatic backend detection: prefers **awww** when available because it is the most efficient for animated and static wallpapers on Wayland.
- Animated wallpaper support: GIF/WebP/MP4/WebM/MKV/MOV with automatic backend selection.
- **Wallpaper Engine projects**: Steam Workshop folder discovery, local projects, titles and previews; videos through mpvpaper and compatible scenes through linux-wallpaperengine.
- Lightweight static previews for images, animations and videos; frame extraction is handled by the C core.
- Background application/conversion in the GUI, with interruptible external processes.
- Daemon mode for automatic wallpaper changes by interval.
- Atomic configuration and history storage, respecting XDG directories.
- Shared favorites, recent wallpapers and application behavior across GUI, CLI and daemon.

## Dependencies

```text
swaybg
json-c    # Wallpaper Engine metadata and Hyprland monitor discovery
qt6-base   # only for the optional GUI
```

Optional:

```text
hyprpaper
awww      # efficient animated/GIF wallpapers on Wayland
mpvpaper  # video wallpapers on Wayland
linux-wallpaperengine-git  # AUR: Wallpaper Engine scenes; needs official assets
wallust
ffmpeg              # video thumbnails and oversized wallpaper conversions
ffmpegthumbnailer   # faster video thumbnails
```

To build:

```text
cmake
pkgconf
base-devel
```

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### C-only build (no Qt or C++ compiler)

```bash
cmake -B build-cli -S . -DARCHPAPER_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-cli
./build-cli/archpaper --help
```

The default build produces `archpaper` (GUI plus CLI) and `archpaper-cli` (pure C).
With `ARCHPAPER_BUILD_GUI=OFF`, the pure C executable is named `archpaper`.

### Tests

```bash
ctest --test-dir build-cli --output-on-failure
```

The C tests use isolated temporary directories and mock executables. They exercise
configuration validation/atomic writes, process failures/timeouts/cancellation,
library scanning, favorites, history, cache fallback, wallust/hook ordering and
daemon lifecycle, plus Wallpaper Engine project parsing, Steam discovery and
supervised engine startup/shutdown, without Qt or a real Wayland session. Set `BUILD_TESTING=OFF` to
omit test executables.

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
- Use the **sidebar** to switch between Library, Favorites, Recent and Settings.
- Select a folder from the folder panel (or add/remove folders).
- Click a thumbnail to see the preview in the right panel.
- Double-click, press **Enter** in the grid, or click **Apply wallpaper** to set the wallpaper.
- Use the search box to filter by name; **Ctrl+F** focuses it.
- Toggle the preview using **Preview** or **Ctrl+P**, and drag its divider to resize it.
- Open **More** to apply a random wallpaper or clear the current wallpaper.
- Press the star button to add/remove wallpapers from Favorites.
- Open Settings to configure the backend, mode, wallust, video quality and the daemon.
- Use **More → Import Wallpaper Engine from Steam** to add downloaded Workshop folders.

### CLI

```bash
archpaper set <image|video|gif|project-directory|project.json> [--mode fill|fit|stretch|center|tile] [--backend swaybg|hyprpaper|awww|mpvpaper|linux-wallpaperengine] [--wallust] [--wallust-hook <script>]
archpaper random <directory> [--wallust] [--wallust-hook <script>]
archpaper daemon <directory> --interval <seconds> [--wallust] [--wallust-hook <script>]
archpaper clear
archpaper status
archpaper backend
archpaper list <directory>
archpaper steam [--import]
archpaper favorite <image|video|gif>
archpaper favorites
archpaper recent
archpaper daemon status
archpaper daemon stop
```

Use `archpaper-cli` instead in scripts to run the pure C binary from the default
build. `--cache-quality original|monitor|low`,
`--mpvpaper-profile quality|balanced|performance` and `--hwdec` are available for
`set`, `random` and `daemon`. Invalid options are rejected before changing a wallpaper.

## Animated wallpapers

`archpaper` supports GIF, animated WebP, MP4, WebM, MKV, MOV and AVI files.

- **awww** is the preferred backend for animated images (GIF/WebP) and static images on Wayland; it is lightweight and fast.
- **mpvpaper** is used for video files (MP4/WebM/MKV/MOV/AVI).

If you select a backend such as `swaybg` or `hyprpaper` and apply an animated file, `archpaper` automatically selects a compatible backend. The GUI displays a static frame to keep preview resource usage low; video frame extraction is cancelled when switching selections.

To use animated wallpapers with the CLI:

```bash
# Starts awww-daemon if needed
archpaper set ~/Wallpapers/animation.gif --backend awww

# Requires mpvpaper for video
archpaper set ~/Wallpapers/video.mp4 --backend mpvpaper
```

## Wallpaper Engine

Archpaper supports locally downloaded **Wallpaper Engine projects** on Hyprland.
Add a project folder or a folder containing projects using **+**, or choose
**More → Import Wallpaper Engine from Steam**. Steam discovery includes native,
Flatpak and Snap installations, plus additional disks listed in `libraryfolders.vdf`.
It imports existing `steamapps/workshop/content/431960` folders; it does not download
or subscribe to Workshop items.

Each project appears once, using the title and preview from `project.json`.
Internal textures and previews are not listed as separate wallpapers. Favorites,
recent history and daemon rotation retain the project's `project.json` identity.

### Playback

- **Video projects:** played with `mpvpaper`, using the existing video settings.
- **Scene projects:** played with [linux-wallpaperengine](https://github.com/Almamu/linux-wallpaperengine).
  Install `linux-wallpaperengine-git` from the AUR and install the official
  Wallpaper Engine through Steam to provide its assets.
- **Web/application projects and incomplete projects:** displayed as unsupported
  and excluded from random selection. Scene effects depend on the Linux engine's
  compatibility; this is not full compatibility with the Windows application.

In **Settings → Wallpaper Engine scenes**, configure:

- **Monitor:** empty uses all active monitors returned by `hyprctl -j monitors`;
  set a name such as `DP-1` to target one monitor.
- **Assets directory:** detected from Steam, or set the path to the official
  `steamapps/common/wallpaper_engine/assets` directory manually.
- **Frame limit:** 1–240 FPS, default 30.
- **Audio:** muted by default. Audio-reactive effects remain controlled by the engine.

`fill`, `fit` and `stretch` map to the engine's scaling modes; `center` and `tile`
use `fill` for scenes. These scene settings do not change mpvpaper video settings.

```bash
# Show discovered Workshop folders; --import also saves them in the library
archpaper steam --import

# Accepts the project folder or its project.json; selects the engine automatically
archpaper set ~/Wallpapers/123456789
archpaper set ~/Wallpapers/123456789/project.json --engine-fps 30 --engine-output DP-1

# Custom Steam library / audio
archpaper set ~/Wallpapers/123456789 \
  --engine-assets "/mnt/Games/steamapps/common/wallpaper_engine/assets" --engine-audio

# Rotate downloaded projects (the engine options also work with random/daemon)
archpaper daemon ~/.local/share/Steam/steamapps/workshop/content/431960 --interval 300
```

Use `--engine-silent` to mute scenes again; pass an empty string to
`--engine-output` or `--engine-assets` to restore automatic detection.
The engine is supervised independently of the GUI. Switching wallpapers or
running `archpaper clear` stops Archpaper's engine process. Initial startup is
checked for early exits; successful startup does not guarantee that every effect
renders correctly. On exit, the first 8 KiB of engine output are written to
`$XDG_RUNTIME_DIR/archpaper/engine.log` (or `/tmp/archpaper-<uid>/engine.log`).

Wallust uses the project's preview image. A missing preview is reported as a
post-apply theme failure; the optional extra hook receives the project manifest path.

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

The shared C application flow waits for wallust to finish before running the
extra hook. Each has a 60-second timeout; a failed wallust run skips the extra
hook. Post-apply theme/storage failures are reported separately from wallpaper
application failures.

### GUI configuration

The Settings section (gear icon in the sidebar) contains:
- **Backend** and **Display mode** selectors in the Wallpaper group.
- **Checkbox** *Generate scheme with wallust* enables wallust on every wallpaper change.
- **Hook:** optional extra script to run after wallust. If left empty, only `wallust run` is executed and your `wallust.toml` handles the rest.
- **Daemon** controls for automatic wallpaper changes by interval.

Favorites and recent wallpapers are stored in `~/.config/archpaper/favorites` and `~/.config/archpaper/recent`.

`XDG_CONFIG_HOME` and `XDG_CACHE_HOME` are supported. Daemon/apply locks live in
`$XDG_RUNTIME_DIR/archpaper`, with a private `/tmp/archpaper-<uid>` fallback.
Daemon status is obtained from the kernel lock owner, so stale PID files do not
identify a running daemon. A daemon change records the wallpaper without
overwriting settings edited in the GUI.

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
include/archpaper/  # Public C API (also callable from C++)
src/core/           # C17 application logic
  wallpaper.c       # Apply transaction shared by CLI, GUI and daemon
  process.c         # argv execution, output capture, deadlines and cancellation
  library.c         # Directory scanning and constant-memory random selection
  history.c         # Locked, atomic favorites and recent-history updates
  config.c          # Validated settings and atomic configuration merges
  storage.c         # XDG paths, directories and atomic file writing
  cache.c           # Media probing, conversion, pruning and thumbnail extraction
  backend.c         # Wayland backend adapters
  engine.c          # Wallpaper Engine metadata, Steam discovery and supervised playback
  daemon.c          # Single-instance background worker and lifecycle
  wallust.c         # Theme generation and ordered extra hooks
src/cli/            # Standalone C entry point and command parsing
src/gui/            # Optional Qt Widgets presentation layer and worker adapters
  components/       # Reusable UI widgets
  models/           # (future) data models
  delegates/        # (future) item delegates
  services/         # (future) config/thumbnail services
  theme/            # QSS stylesheet and resource file
tests/              # C tests with mock processes and temporary data
```

See [the core API notes](docs/core-api.md) for ownership, errors and threading.

## License

This program is free software: you can redistribute it and/or modify
it under the terms of the **GNU General Public License v3.0** or later.
See the [LICENSE](LICENSE) file for the full text.
