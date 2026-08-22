#ifndef ARCHPAPER_WALLUST_H
#define ARCHPAPER_WALLUST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Comprueba si el binario 'wallust' está disponible en PATH. */
int wallust_available(void);

/* Ejecuta 'wallust run <image_path>' en segundo plano.
 * No bloquea al proceso padre. Devuelve 0 si el comando se lanzó. */
int wallust_run(const char *image_path);

/* Ejecuta un script adicional post-wallust solo si hook_path está definido
 * y el archivo existe. Nunca usa un hook por defecto: wallust ya ejecuta
 * los hooks definidos en ~/.config/wallust/wallust.toml.
 * El wallpaper se pasa como primer argumento ($1) y en $WALLPAPER. */
int wallust_hook_run(const char *hook_path, const char *image_path);

#ifdef __cplusplus
}
#endif

#endif
