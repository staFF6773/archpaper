#ifndef ARCHPAPER_CONFIG_H
#define ARCHPAPER_CONFIG_H

#include "backend.h"

#define MAX_FAVORITE_FOLDERS 32
#define MAX_FOLDER_LEN 4096

typedef struct {
    backend_t backend;
    char mode[32];
    char last_wallpaper[4096];
    char folders[MAX_FAVORITE_FOLDERS][MAX_FOLDER_LEN];
    int folder_count;
    int wallust_enabled;
    char wallust_hook[4096];
} config_t;

/* Carga la config desde ~/.config/archpaper/config y aplica valores por defecto. */
int config_load(config_t *cfg);

/* Guarda la config actual. */
int config_save(const config_t *cfg);

/* Rellena valores por defecto. */
void config_default(config_t *cfg);

/* Añade una carpeta favorita (devuelve 0 si ok). */
int config_add_folder(config_t *cfg, const char *path);

/* Elimina una carpeta favorita por índice. */
int config_remove_folder(config_t *cfg, int index);

#endif
