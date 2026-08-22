/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "archpaper/config.h"
#include "archpaper/utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *config_file_path(void) {
    static char path[4096];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(path, sizeof(path), "%s/archpaper/config", xdg);
    } else {
        snprintf(path, sizeof(path), "%s/.config/archpaper/config", get_home());
    }
    return path;
}

static char *config_dir_path(void) {
    static char path[4096];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(path, sizeof(path), "%s/archpaper", xdg);
    } else {
        snprintf(path, sizeof(path), "%s/.config/archpaper", get_home());
    }
    return path;
}

void config_default(config_t *cfg) {
    cfg->backend = detect_backend();
    strncpy(cfg->mode, "fill", sizeof(cfg->mode) - 1);
    cfg->mode[sizeof(cfg->mode) - 1] = '\0';
    cfg->last_wallpaper[0] = '\0';
    cfg->folder_count = 0;
    cfg->wallust_enabled = 0;
    cfg->wallust_hook[0] = '\0';
    cfg->daemon_interval = 300;
    strncpy(cfg->wallhaven_purity, "sfw", sizeof(cfg->wallhaven_purity) - 1);
    cfg->wallhaven_purity[sizeof(cfg->wallhaven_purity) - 1] = '\0';
    cfg->wallhaven_api_key[0] = '\0';
    char *download_dir = expand_path("~/Pictures/Wallpapers");
    if (download_dir) {
        strncpy(cfg->market_download_dir, download_dir, sizeof(cfg->market_download_dir) - 1);
        cfg->market_download_dir[sizeof(cfg->market_download_dir) - 1] = '\0';
        free(download_dir);
    } else {
        cfg->market_download_dir[0] = '\0';
    }
}

static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : 1;

    if (mkdir(path, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;

    if (errno == ENOENT) {
        char parent[4096];
        snprintf(parent, sizeof(parent), "%s", path);
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent) {
            *slash = '\0';
            if (ensure_dir(parent) != 0) return 1;
            if (mkdir(path, 0755) == 0) return 0;
            return (errno == EEXIST) ? 0 : 1;
        }
    }
    return 1;
}

int config_load(config_t *cfg) {
    config_default(cfg);

    FILE *f = fopen(config_file_path(), "r");
    if (!f) return 0; /* No config yet; use defaults. */

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strncmp(line, "backend=", 8) == 0) {
            cfg->backend = backend_from_string(line + 8);
        } else if (strncmp(line, "mode=", 5) == 0) {
            strncpy(cfg->mode, line + 5, sizeof(cfg->mode) - 1);
            cfg->mode[sizeof(cfg->mode) - 1] = '\0';
        } else if (strncmp(line, "last=", 5) == 0) {
            strncpy(cfg->last_wallpaper, line + 5, sizeof(cfg->last_wallpaper) - 1);
            cfg->last_wallpaper[sizeof(cfg->last_wallpaper) - 1] = '\0';
        } else if (strncmp(line, "folder=", 7) == 0) {
            config_add_folder(cfg, line + 7);
        } else if (strncmp(line, "wallust=", 8) == 0) {
            cfg->wallust_enabled = (strcmp(line + 8, "true") == 0 ||
                                    strcmp(line + 8, "1") == 0);
        } else if (strncmp(line, "wallust_hook=", 13) == 0) {
            char *expanded = expand_path(line + 13);
            if (expanded) {
                strncpy(cfg->wallust_hook, expanded, sizeof(cfg->wallust_hook) - 1);
                cfg->wallust_hook[sizeof(cfg->wallust_hook) - 1] = '\0';
                free(expanded);
            }
        } else if (strncmp(line, "daemon_interval=", 16) == 0) {
            cfg->daemon_interval = atoi(line + 16);
            if (cfg->daemon_interval <= 0) cfg->daemon_interval = 300;
        } else if (strncmp(line, "market_download_dir=", 20) == 0) {
            char *expanded = expand_path(line + 20);
            if (expanded) {
                strncpy(cfg->market_download_dir, expanded, sizeof(cfg->market_download_dir) - 1);
                cfg->market_download_dir[sizeof(cfg->market_download_dir) - 1] = '\0';
                free(expanded);
            }
        } else if (strncmp(line, "wallhaven_api_key=", 18) == 0) {
            strncpy(cfg->wallhaven_api_key, line + 18, sizeof(cfg->wallhaven_api_key) - 1);
            cfg->wallhaven_api_key[sizeof(cfg->wallhaven_api_key) - 1] = '\0';
        } else if (strncmp(line, "wallhaven_purity=", 17) == 0) {
            strncpy(cfg->wallhaven_purity, line + 17, sizeof(cfg->wallhaven_purity) - 1);
            cfg->wallhaven_purity[sizeof(cfg->wallhaven_purity) - 1] = '\0';
        }
    }
    fclose(f);
    return 0;
}

int config_save(const config_t *cfg) {
    if (ensure_dir(config_dir_path()) != 0) return 1;

    FILE *f = fopen(config_file_path(), "w");
    if (!f) return 1;

    fprintf(f, "backend=%s\n", backend_to_string(cfg->backend));
    fprintf(f, "mode=%s\n", cfg->mode);
    fprintf(f, "wallust=%s\n", cfg->wallust_enabled ? "true" : "false");
    fprintf(f, "wallust_hook=%s\n", cfg->wallust_hook);
    fprintf(f, "daemon_interval=%d\n", cfg->daemon_interval > 0 ? cfg->daemon_interval : 300);
    fprintf(f, "market_download_dir=%s\n", cfg->market_download_dir);
    fprintf(f, "wallhaven_api_key=%s\n", cfg->wallhaven_api_key);
    fprintf(f, "wallhaven_purity=%s\n", cfg->wallhaven_purity);
    fprintf(f, "last=%s\n", cfg->last_wallpaper);

    for (int i = 0; i < cfg->folder_count; i++) {
        fprintf(f, "folder=%s\n", cfg->folders[i]);
    }

    fclose(f);
    return 0;
}

int config_add_folder(config_t *cfg, const char *path) {
    if (!path || !path[0]) return 1;
    if (cfg->folder_count >= MAX_FAVORITE_FOLDERS) return 1;

    /* Avoid duplicates. */
    for (int i = 0; i < cfg->folder_count; i++) {
        if (strcmp(cfg->folders[i], path) == 0) return 1;
    }

    strncpy(cfg->folders[cfg->folder_count], path, MAX_FOLDER_LEN - 1);
    cfg->folders[cfg->folder_count][MAX_FOLDER_LEN - 1] = '\0';
    cfg->folder_count++;
    return 0;
}

int config_remove_folder(config_t *cfg, int index) {
    if (index < 0 || index >= cfg->folder_count) return 1;
    for (int i = index; i < cfg->folder_count - 1; i++) {
        strcpy(cfg->folders[i], cfg->folders[i + 1]);
    }
    cfg->folder_count--;
    cfg->folders[cfg->folder_count][0] = '\0';
    return 0;
}
