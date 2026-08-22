#ifndef ARCHPAPER_BACKEND_H
#define ARCHPAPER_BACKEND_H

typedef enum {
    BACKEND_SWAYBG,
    BACKEND_HYPRPAPER
} backend_t;

const char *backend_to_string(backend_t b);
backend_t backend_from_string(const char *s);

/* Detecta automáticamente el backend (hyprpaper solo si estamos en Hyprland). */
backend_t detect_backend(void);

/* Comprueba si el binario del backend está en PATH. */
int backend_available(backend_t b);

/* Aplica un wallpaper con el backend indicado. */
int set_wallpaper(backend_t b, const char *path, const char *mode);

/* Elimina los procesos de fondo existentes. */
int clear_wallpaper(void);

#endif
