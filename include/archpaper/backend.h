#ifndef ARCHPAPER_BACKEND_H
#define ARCHPAPER_BACKEND_H

typedef enum {
    BACKEND_SWAYBG,
    BACKEND_HYPRPAPER
} backend_t;

const char *backend_to_string(backend_t b);
backend_t backend_from_string(const char *s);

/* Automatically detect the backend (hyprpaper only if running under Hyprland). */
backend_t detect_backend(void);

/* Check whether the backend binary is available in PATH. */
int backend_available(backend_t b);

/* Apply a wallpaper with the selected backend. */
int set_wallpaper(backend_t b, const char *path, const char *mode);

/* Remove existing background processes. */
int clear_wallpaper(void);

#endif
