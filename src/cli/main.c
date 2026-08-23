/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "archpaper/backend.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"
#include "archpaper/cli.h"

static void print_usage(const char *name) {
    printf("Usage: %s <command> [options]\n\n", name);
    printf("Commands:\n");
    printf("  set <media> [--mode MODE]       Set a wallpaper (image/video/GIF).\n");
    printf("  clear                           Remove the current wallpaper.\n");
    printf("  random <directory> [--mode MODE]  Pick a random image/video.\n");
    printf("  daemon <directory> --interval <s> [--mode MODE]\n");
    printf("                                  Change the wallpaper periodically.\n");
    printf("  status                          Show the current status.\n");
    printf("  backend                         Show detected and active backend.\n");
    printf("\nModes (swaybg): fill, fit, stretch, center, tile\n");
    printf("Global options:\n");
    printf("  --backend <swaybg|hyprpaper|awww|mpvpaper>  Force a specific backend.\n");
    printf("  --mode <MODE>                   Image scaling mode.\n");
    printf("  --wallust                       Run 'wallust run' after applying.\n");
    printf("  --wallust-hook <script>         Script to run after wallust.\n");
}

static int check_wayland(void) {
    if (getenv("WAYLAND_DISPLAY")) return 1;
    const char *stype = getenv("XDG_SESSION_TYPE");
    return stype && strcmp(stype, "wayland") == 0;
}

static void parse_global_args(int argc, char *argv[], backend_t *backend, const char **mode,
                              int *wallust_enabled, const char **wallust_hook) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            *backend = backend_from_string(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            *mode = argv[++i];
        } else if (strcmp(argv[i], "--wallust") == 0) {
            *wallust_enabled = 1;
        } else if (strcmp(argv[i], "--wallust-hook") == 0 && i + 1 < argc) {
            *wallust_hook = argv[++i];
        }
    }
}

static void run_wallust_theme(int enabled, const char *image_path, const char *wallust_hook) {
    if (!enabled) return;
    if (wallust_available()) {
        wallust_run(image_path);
    } else {
        fprintf(stderr, "Warning: wallust not found in PATH; skipping color generation.\n");
    }
    wallust_hook_run(wallust_hook, image_path);
}

int archpaper_cli(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (!check_wayland()) {
        fprintf(stderr, "Warning: no Wayland session detected (WAYLAND_DISPLAY is not set).\n");
    }

    config_t cfg;
    config_load(&cfg);

    backend_t backend = cfg.backend;
    const char *mode = cfg.mode;
    const char *wallust_hook_arg = NULL;
    int wallust_enabled = cfg.wallust_enabled;
    parse_global_args(argc, argv, &backend, &mode, &wallust_enabled, &wallust_hook_arg);

    const char *command = argv[1];

    if (strcmp(command, "set") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }
        char *path = expand_path(argv[2]);
        if (!path || !file_exists(path)) {
            fprintf(stderr, "Error: '%s' not found\n", argv[2]);
            free(path);
            return 1;
        }

        backend = select_backend_for_path(path, backend);
        if (!backend_available(backend)) {
            fprintf(stderr, "Error: backend '%s' not found in PATH.\n", backend_to_string(backend));
            free(path);
            return 1;
        }

        cfg.backend = backend;
        strncpy(cfg.mode, mode, sizeof(cfg.mode) - 1);
        cfg.mode[sizeof(cfg.mode) - 1] = '\0';
        cfg.wallust_enabled = wallust_enabled;
        strncpy(cfg.last_wallpaper, path, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
        config_save(&cfg);

        int r = set_wallpaper(backend, path, mode);
        if (r == 0) {
            run_wallust_theme(wallust_enabled, path,
                              wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook);
        }
        free(path);
        return r;

    } else if (strcmp(command, "clear") == 0) {
        return clear_wallpaper();

    } else if (strcmp(command, "random") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }
        char *dir = expand_path(argv[2]);
        if (!dir || !is_dir(dir)) {
            fprintf(stderr, "Error: '%s' is not a directory.\n", argv[2]);
            free(dir);
            return 1;
        }

        char *img = random_image(dir);
        if (!img) {
            fprintf(stderr, "Error: no images found in '%s'.\n", dir);
            free(dir);
            return 1;
        }

        printf("Selected: %s\n", img);

        backend = select_backend_for_path(img, backend);
        if (!backend_available(backend)) {
            fprintf(stderr, "Error: backend '%s' not found in PATH.\n", backend_to_string(backend));
            free(img);
            free(dir);
            return 1;
        }

        cfg.backend = backend;
        strncpy(cfg.mode, mode, sizeof(cfg.mode) - 1);
        cfg.mode[sizeof(cfg.mode) - 1] = '\0';
        cfg.wallust_enabled = wallust_enabled;
        strncpy(cfg.last_wallpaper, img, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
        config_save(&cfg);

        int r = set_wallpaper(backend, img, mode);
        if (r == 0) {
            run_wallust_theme(wallust_enabled, img,
                              wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook);
        }
        free(img);
        free(dir);
        return r;

    } else if (strcmp(command, "daemon") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }

        const char *dir_raw = NULL;
        int interval = 300;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
                interval = atoi(argv[++i]);
            } else if (argv[i][0] != '-' && !dir_raw) {
                dir_raw = argv[i];
            }
        }

        if (!dir_raw) { print_usage(argv[0]); return 1; }
        char *dir = expand_path(dir_raw);
        if (!dir || !is_dir(dir)) {
            fprintf(stderr, "Error: '%s' is not a directory.\n", dir_raw);
            free(dir);
            return 1;
        }

        printf("Starting daemon: backend=%s, interval=%ds, wallust=%s, directory=%s\n",
               backend_to_string(backend), interval,
               wallust_enabled ? "yes" : "no", dir);
        const char *wallust_hook = wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook;
        return daemonize_random(dir, interval, backend, mode, wallust_enabled, wallust_hook);

    } else if (strcmp(command, "status") == 0) {
        printf("Active backend:   %s\n", backend_to_string(backend));
        printf("Mode:             %s\n", mode);
        printf("Detected backend: %s\n", backend_to_string(detect_backend()));
        printf("Last wallpaper:   %s\n",
               cfg.last_wallpaper[0] ? cfg.last_wallpaper : "(none)");
        return 0;

    } else if (strcmp(command, "backend") == 0) {
        printf("Detected: %s\n", backend_to_string(detect_backend()));
        printf("Active:   %s\n", backend_to_string(backend));
        return 0;

    } else if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        return 0;

    } else {
        fprintf(stderr, "Unknown command: %s\n\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
