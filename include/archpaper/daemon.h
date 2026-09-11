/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ARCHPAPER_DAEMON_H
#define ARCHPAPER_DAEMON_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

int daemon_start(const char *dir, const config_t *cfg);
int daemon_stop(void);
/* pid=0 when stopped. Uses the kernel lock owner, not a stored PID. */
int daemon_status(int *pid);
/* Foreground loop for the dedicated internal CLI entry point. */
int daemon_run(const char *dir, const config_t *cfg);

/* Daemon that periodically changes the wallpaper from a directory.
 * If enable_wallust is non-zero, it runs wallust after each change.
 * wallust_hook can be NULL/empty; no external hook is used by default.
 * cache_quality controls downscaling of oversized videos/animations
 * (see backend.h for accepted values). */
int daemonize_random(const char *dir, int interval, backend_t b, const char *mode,
                        int enable_wallust, const char *wallust_hook,
                        const char *cache_quality);

#ifdef __cplusplus
}
#endif

#endif
