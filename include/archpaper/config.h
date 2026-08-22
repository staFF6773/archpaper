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

/* Load config from ~/.config/archpaper/config and apply defaults. */
int config_load(config_t *cfg);

/* Save the current config. */
int config_save(const config_t *cfg);

/* Fill in default values. */
void config_default(config_t *cfg);

/* Add a favorite folder (returns 0 on success). */
int config_add_folder(config_t *cfg, const char *path);

/* Remove a favorite folder by index. */
int config_remove_folder(config_t *cfg, int index);

#endif
