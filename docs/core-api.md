# C core API

`archpaper_core` is a C17 static library using Linux/POSIX facilities. It has no
Qt or C++ dependency. Public headers live in `include/archpaper/` and provide C
linkage guards. CMake enables the C++ language and searches for Qt only when
`ARCHPAPER_BUILD_GUI=ON`.
The core links to `json-c` for project metadata and monitor discovery.

## Responsibilities

- **Frontends:** parse commands or display widgets, convert UI values to C data,
  schedule work, and display results.
- **Core:** validate settings/media, scan directories, choose backends, execute
  processes, convert media, record history, and coordinate wallust and hooks.
- **Backends:** use explicit configuration snapshots; they do not read widgets
  or reload configuration while building video options.

## Applying a wallpaper

```c
#include <archpaper/wallpaper.h>

config_t options;
int rc = config_load(&options);
if (rc == AP_OK) {
    ap_apply_result result;
    rc = ap_wallpaper_apply("/absolute/path/wallpaper.png", &options,
                            AP_APPLY_SAVE_OPTIONS, &result);
    if (rc == AP_OK) {
        /* Applied successfully. result.persistence and result.theme describe
         * any post-apply storage or theme failure. */
    }
}
```

`flags=0` preserves current preferences while recording the new wallpaper;
the GUI and daemon use this form. `AP_APPLY_SAVE_OPTIONS` also saves the passed
preferences, for explicit CLI overrides. Both preserve the latest folder list.
Configuration and recent history are updated only after backend success.
Detached backend startup confirms a successful `exec`; long-running backend
health is not implied. Short-lived awww commands are checked for successful exit.

## Ownership and errors

- `ap_result` describes invalid input, I/O, allocation, missing executable/file,
  process failure, timeout, cancellation, or an operation already running.
  Wallpaper Engine adds unsupported/incomplete project, missing engine/assets,
  and missing output errors with actionable messages.
- Legacy `int` APIs also return these values; use `ap_error_string()` to present
  them. Availability/type predicates return booleans.
- Initialize `ap_path_list` with `{0}`. It owns its strings; release it with
  `ap_path_list_free()`. Scan/history load replace it only on success.
- `ap_library_random()` and `expand_path()` return malloc-owned strings.
- Storage/cache paths are copied into caller-owned buffers. Oversized paths
  are rejected, not silently truncated.
- `backend_optimized_path()` now takes an output buffer and capacity rather
  than returning a pointer to shared static storage.
- `config_load()` leaves defaults and returns an error when the file is invalid;
  a nonexistent file is a successful load of defaults. Save refuses invalid
  values. Invalid files are not silently overwritten by GUI startup.

## Processes and workers

`ap_process_start()` uses an argument vector, with no shell interpretation.
Use `ap_process_poll()` until it stops returning `AP_BUSY`, or use
`ap_process_wait()` with a deadline. Output buffers are borrowed until completion;
excess output is drained/discarded so a full pipe cannot deadlock the child.
Every started process must be waited for or cancelled to release its descriptor
and reap the child. A handle belongs to one caller and must not be reused while
running. A timeout of zero means no deadline.
`ap_process_start_logged()` has the same lifecycle but merges stderr into the
captured output. `ap_process_cancel_requested()` checks the owning thread's
cancellation callback during higher-level startup waits.

`ap_process_cancel()` terminates the process group. A worker can register a
per-thread cancellation callback with `ap_process_set_cancel_check()`, then clear
it before its context expires. The GUI adapter uses Qt's thread interruption;
the daemon uses its signal flag. Callbacks must be fast and their context must
be safe for the owning thread to read. Do not mutate process handles from a
different thread.

The application flow is synchronous in C. The GUI invokes it on a Qt worker and
marshals the result back to the UI thread; closing the window requests
cancellation and joins the worker. Thumbnail extraction uses the same C process
API. Image decoding/rendering stays in Qt Widgets; Qt Multimedia is not required.

The daemon starts a fresh executable using its private `__daemon-run` CLI entry,
rather than continuing inside a forked Qt process. `daemon_start()` therefore
expects the host executable to dispatch that command through `archpaper_cli()`,
as both shipped launchers do. Locks use the per-user runtime directory. Stop is
cooperative and cancels an active external operation before releasing the lock.

## Wallpaper Engine projects

`ap_engine_read()` accepts a project directory or `project.json` and fills a
caller-owned `ap_engine_project`. No returned fields require freeing. Metadata
reads are bounded to 4 MiB and restricted to regular files. Preview and media
references resolve inside the project directory, including after symlink
resolution. Scene projects may refer to `scene.json` packed in `scene.pkg`.
Unknown types return metadata with `AP_ENGINE_UNSUPPORTED` for display.

`ap_library_scan()` lists immediate project subdirectories as manifest paths;
scanning a project itself returns only that project. Malformed manifests are
skipped in a collection. Random selection excludes unsupported/incomplete
projects. `ap_engine_discover()` replaces an initialized path list on success
with deduplicated existing Steam Workshop directories. `ap_engine_outputs()`
appends configured/Hyprland monitor names to an initialized list.

`ap_wallpaper_apply()` normalizes project input to its manifest for persistence,
selects mpvpaper for videos and linux-wallpaperengine for scenes, and uses the
preview for Wallust. Extra hooks still receive the original project identity.
Scene executable, assets and output checks run before stopping existing backends.

The scene engine uses a fresh executable via `__engine-run`, dispatched through
`archpaper_cli()`. The supervisor owns `engine.lock`, starts the renderer in a
process group and publishes `engine.ready` after 250 ms without an early exit.
Startup waits at most about three seconds, and checks cancellation. Stop signals
the kernel lock owner; the supervisor terminates/reaps its renderer before
releasing the lock. It captures bounded stdout/stderr diagnostics in `engine.log`
on exit. This detects early startup failures, not rendering correctness or later
health failures. As with daemon startup, embedders must dispatch the private
command in their host executable.

## Storage and cache

Config/history use sibling temporary files, checked flush/fsync/close, and
atomic rename. Read-modify-write operations are serialized by lock files.
Unavailable favorite paths remain stored and reappear when mounted again.
Recent entries are unique and limited to 50. The current line-based formats
cannot represent paths containing CR/LF, so those are rejected on write.

Cache conversion uses a temporary output and publishes it only on success.
Missing tools, a busy conversion lock, or conversion failure fall back to the
original media. Conversion has a 120-second limit. Completed cached conversions
are pruned to 50 entries / 2 GiB per media directory, retaining the current entry.
One oversized current entry may exceed the byte limit. Cached filenames account
for source path, modification time, size and target resolution.

## Verification

```sh
cmake -S . -B build-cli -DARCHPAPER_BUILD_GUI=OFF
cmake --build build-cli
ctest --test-dir build-cli --output-on-failure
```

For memory/undefined-behavior checks, configure a separate build with
`-DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` and
`-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`. The tests use temporary
XDG directories and fake backend/tool executables, including daemon subprocesses.
