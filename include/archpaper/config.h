/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ARCHPAPER_CONFIG_H
#define ARCHPAPER_CONFIG_H

#include "backend.h"
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FAVORITE_FOLDERS 32
#define MAX_FOLDER_LEN 4096

typedef struct config {
    backend_t backend;
    char mode[32];
    char last_wallpaper[4096];
    char folders[MAX_FAVORITE_FOLDERS][MAX_FOLDER_LEN];
    int folder_count;
    int wallust_enabled;
    char wallust_hook[4096];
    int daemon_interval;
    char cache_quality[16];
    char mpvpaper_profile[16];
    int mpvpaper_hwdec;
} config_t;

/* Load config from ~/.config/archpaper/config and apply defaults. */
int config_load(config_t *cfg);

/* Save the current config. */
int config_save(const config_t *cfg);
int config_validate(const config_t *cfg);
int config_parse_interval(const char *value, int *seconds);
/* Atomically merge a successful wallpaper change with the latest config.
 * options=NULL preserves settings; otherwise copies options but keeps folders. */
int config_record_wallpaper(const char *path, const config_t *options);

/* Fill in default values. */
void config_default(config_t *cfg);

/* Add a favorite folder (returns 0 on success). */
int config_add_folder(config_t *cfg, const char *path);

/* Remove a favorite folder by index. */
int config_remove_folder(config_t *cfg, int index);

#ifdef __cplusplus
}
#endif

#endif
