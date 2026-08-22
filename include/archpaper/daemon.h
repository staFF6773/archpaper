#ifndef ARCHPAPER_DAEMON_H
#define ARCHPAPER_DAEMON_H

#include "backend.h"

/* Daemon that periodically changes the wallpaper from a directory.
 * If enable_wallust is non-zero, it runs wallust after each change.
 * wallust_hook can be NULL/empty; no external hook is used by default. */
int daemonize_random(const char *dir, int interval, backend_t b, const char *mode,
                       int enable_wallust, const char *wallust_hook);

#endif
