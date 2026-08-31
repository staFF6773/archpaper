/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ARCHPAPER_BACKEND_H
#define ARCHPAPER_BACKEND_H

typedef enum {
    BACKEND_SWAYBG,
    BACKEND_HYPRPAPER,
    BACKEND_MPVPPAPER,
    BACKEND_SWWW
} backend_t;

const char *backend_to_string(backend_t b);
backend_t backend_from_string(const char *s);

/* Automatically detect the backend (hyprpaper only if running under Hyprland). */
backend_t detect_backend(void);

/* Check whether the backend binary is available in PATH. */
int backend_available(backend_t b);

/* Pick the best backend for a file, falling back from the user's preference
 * when the file type is unsupported by that backend. */
backend_t select_backend_for_path(const char *path, backend_t preferred);

/* Apply a wallpaper with the selected backend. */
int set_wallpaper(backend_t b, const char *path, const char *mode);

/* Return the best path to use for previews/GUI thumbnails. For oversized
 * videos or animated images this is the cached, resized copy instead of the
 * original file. The returned pointer points to internal static storage and
 * is only valid until the next call. */
const char *backend_optimized_path(const char *path);

/* Remove existing background processes. */
int clear_wallpaper(void);

#endif
