/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "archpaper/backend.h"
#include "archpaper/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SWAYBG_CMD "swaybg"
#define HYPRPAPER_CMD "hyprpaper"
#define MPVPAPER_CMD "mpvpaper"
#define SWWW_CMD "swww"

const char *backend_to_string(backend_t b) {
    switch (b) {
        case BACKEND_HYPRPAPER: return "hyprpaper";
        case BACKEND_MPVPPAPER: return "mpvpaper";
        case BACKEND_SWWW: return "swww";
        default: return "swaybg";
    }
}

backend_t backend_from_string(const char *s) {
    if (!s) return BACKEND_SWAYBG;
    if (strcasecmp(s, "hyprpaper") == 0) return BACKEND_HYPRPAPER;
    if (strcasecmp(s, "mpvpaper") == 0) return BACKEND_MPVPPAPER;
    if (strcasecmp(s, "swww") == 0) return BACKEND_SWWW;
    return BACKEND_SWAYBG;
}

static int cmd_exists(const char *cmd) {
    const char *path_env = getenv("PATH");
    if (!path_env) return 0;

    char *path = strdup(path_env);
    if (!path) return 0;

    int found = 0;
    char *saveptr = NULL;
    char *dir = strtok_r(path, ":", &saveptr);
    char full[4096];

    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) {
            found = 1;
            break;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path);
    return found;
}

int backend_available(backend_t b) {
    switch (b) {
        case BACKEND_HYPRPAPER: return cmd_exists(HYPRPAPER_CMD);
        case BACKEND_MPVPPAPER: return cmd_exists(MPVPAPER_CMD);
        case BACKEND_SWWW: return cmd_exists(SWWW_CMD);
        default: return cmd_exists(SWAYBG_CMD);
    }
}

backend_t detect_backend(void) {
    /* Prefer swww when available: efficient for static and animated wallpapers. */
    if (backend_available(BACKEND_SWWW))
        return BACKEND_SWWW;
    /* If running under Hyprland and hyprpaper is available, prefer it. */
    if (getenv("HYPRLAND_INSTANCE_SIGNATURE") && backend_available(BACKEND_HYPRPAPER))
        return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

static int pkill_backend(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pkill -x %s 2>/dev/null", name);
    return system(cmd);
}

static void kill_wallpaper_backends(void) {
    pkill_backend(SWAYBG_CMD);
    pkill_backend(HYPRPAPER_CMD);
    pkill_backend(MPVPAPER_CMD);
    usleep(100000); /* 100 ms to release resources */
}

static const char *map_swaybg_mode(const char *mode) {
    if (!mode) return "fill";
    if (strcasecmp(mode, "fill") == 0) return "fill";
    if (strcasecmp(mode, "fit") == 0) return "fit";
    if (strcasecmp(mode, "stretch") == 0 || strcasecmp(mode, "scale") == 0) return "stretch";
    if (strcasecmp(mode, "center") == 0) return "center";
    if (strcasecmp(mode, "tile") == 0) return "tile";
    return "fill";
}

static int run_fork(const char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) {
        /* Wait for the intermediate child so it does not become a zombie. */
        waitpid(pid, NULL, 0);
        return 0;
    }

    /* Intermediate child: double fork and exit. */
    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    /* Grandchild: adopted by init, runs the backend in the background. */
    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execvp(argv[0], (char *const *)argv);
    _exit(1);
}

static int set_swaybg(const char *path, const char *mode) {
    const char *mode_str = map_swaybg_mode(mode);
    const char *argv[] = {SWAYBG_CMD, "-i", path, "-m", mode_str, NULL};
    return run_fork(argv);
}

static int set_hyprpaper(const char *path) {
    const char *cfg_path = "/tmp/archpaper_hyprpaper.conf";
    FILE *f = fopen(cfg_path, "w");
    if (!f) return 1;

    fprintf(f, "preload = %s\n", path);
    fprintf(f, "wallpaper = ,%s\n", path);
    fprintf(f, "splash = false\n");
    fclose(f);

    const char *argv[] = {HYPRPAPER_CMD, "--config", cfg_path, NULL};
    return run_fork(argv);
}

static int set_mpvpaper(const char *path) {
    /* mpvpaper [options] <monitor> <path>; '*' applies to all monitors.
     * Force loop playback and disable audio so the wallpaper never disappears. */
    const char *argv[] = {MPVPAPER_CMD, "--fork", "-o", "no-audio loop-file=inf", "*", path, NULL};
    return run_fork(argv);
}

static int set_swww(const char *path) {
    /* swww requires swww-daemon running; 'swww img' handles static and GIFs. */
    const char *argv[] = {SWWW_CMD, "img", "--transition-type", "none", path, NULL};
    return run_fork(argv);
}

int set_wallpaper(backend_t b, const char *path, const char *mode) {
    if (!path || !file_exists(path)) return 1;

    kill_wallpaper_backends();

    switch (b) {
        case BACKEND_HYPRPAPER: return set_hyprpaper(path);
        case BACKEND_MPVPPAPER: return set_mpvpaper(path);
        case BACKEND_SWWW: return set_swww(path);
        default: return set_swaybg(path, mode);
    }
}

int clear_wallpaper(void) {
    kill_wallpaper_backends();
    /* swww can clear to a black background. */
    if (backend_available(BACKEND_SWWW)) {
        const char *argv[] = {SWWW_CMD, "clear", NULL};
        run_fork(argv);
    }
    return 0;
}
