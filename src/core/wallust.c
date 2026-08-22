/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "archpaper/wallust.h"
#include "archpaper/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WALLUST_CMD "wallust"

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

int wallust_available(void) {
    return cmd_exists(WALLUST_CMD);
}

int wallust_run(const char *image_path) {
    if (!image_path || !image_path[0]) return 1;

    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) {
        waitpid(pid, NULL, 0);
        return 0;
    }

    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    execlp(WALLUST_CMD, WALLUST_CMD, "run", image_path, (char *)NULL);
    _exit(1);
}

static int run_hook_fork(const char *hook, const char *image_path) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) {
        waitpid(pid, NULL, 0);
        return 0;
    }

    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    setenv("WALLPAPER", image_path, 1);
    execlp("sh", "sh", hook, image_path, (char *)NULL);
    _exit(1);
}

int wallust_hook_run(const char *hook_path, const char *image_path) {
    if (!image_path || !image_path[0]) return 0;
    if (!hook_path || !hook_path[0]) return 0;
    if (!file_exists(hook_path)) return 0;

    return run_hook_fork(hook_path, image_path);
}
