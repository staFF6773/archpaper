/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ARCHPAPER_WALLUST_H
#define ARCHPAPER_WALLUST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Check whether the 'wallust' binary is available in PATH. */
int wallust_available(void);

/* Run 'wallust run <image_path>' in the background.
 * Does not block the parent process. Returns 0 if the command was launched. */
int wallust_run(const char *image_path);

/* Run an additional post-wallust script only if hook_path is set
 * and the file exists. No default hook is used: wallust already runs
 * the hooks defined in ~/.config/wallust/wallust.toml.
 * The wallpaper is passed as the first argument ($1) and in $WALLPAPER. */
int wallust_hook_run(const char *hook_path, const char *image_path);

#ifdef __cplusplus
}
#endif

#endif
