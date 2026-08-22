#ifndef ARCHPAPER_DAEMON_H
#define ARCHPAPER_DAEMON_H

#include "backend.h"

/* Daemon que cambia periódicamente el wallpaper desde un directorio.
 * Si enable_wallust es distinto de cero, ejecuta wallust tras cada cambio.
 * wallust_hook puede ser NULL/vacío para usar el hook por defecto. */
int daemonize_random(const char *dir, int interval, backend_t b, const char *mode,
                      int enable_wallust, const char *wallust_hook);

#endif
