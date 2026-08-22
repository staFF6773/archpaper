/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ARCHPAPER_UTILS_H
#define ARCHPAPER_UTILS_H

/* Expand a path that starts with ~ to the value of $HOME. */
char *expand_path(const char *path);

/* Check whether a file or directory exists. */
int file_exists(const char *path);

/* Check whether the path is a directory. */
int is_dir(const char *path);

/* Indicate whether the path has a supported image/video extension. */
int is_image(const char *path);
int is_video(const char *path);
int is_animated_image(const char *path);

/* Return the extension of a path (lowercase, without the dot). */
const char *file_extension(const char *path);

/* Return a random image/video from a directory (must be freed with free). */
char *random_image(const char *dir);

/* Return the user's home directory. */
const char *get_home(void);

#endif
