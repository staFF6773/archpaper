#ifndef ARCHPAPER_UTILS_H
#define ARCHPAPER_UTILS_H

/* Expande una ruta que empieza con ~ al valor de $HOME. */
char *expand_path(const char *path);

/* Comprueba si un archivo o directorio existe. */
int file_exists(const char *path);

/* Comprueba si la ruta es un directorio. */
int is_dir(const char *path);

/* Indica si la ruta tiene extensión de imagen soportada. */
int is_image(const char *path);

/* Devuelve una imagen aleatoria de un directorio (debe liberarse con free). */
char *random_image(const char *dir);

/* Devuelve el directorio home del usuario. */
const char *get_home(void);

#endif
