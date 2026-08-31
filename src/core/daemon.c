/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "archpaper/daemon.h"
#include "archpaper/backend.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define PID_FILE "/tmp/archpaper.pid"

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void write_pid(void) {
    FILE *f = fopen(PID_FILE, "w");
    if (f) {
        fprintf(f, "%d\n", getpid());
        fclose(f);
    }
}

static void clear_pid(void) {
    unlink(PID_FILE);
}

int daemonize_random(const char *dir, int interval, backend_t b, const char *mode,
                       int enable_wallust, const char *wallust_hook,
                       const char *cache_quality) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) return 0; /* Parent exits immediately. */

    if (setsid() < 0) return 1;

    write_pid();

    /* Avoid zombies left behind by set_wallpaper() and wallust_run(). */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    /* Detach stdin/stdout/stderr so the daemon does not block the terminal. */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    if (interval <= 0) interval = 300;

    while (running) {
        char *img = random_image(dir);
        if (img) {
            backend_t actual = select_backend_for_path(img, b);
            set_wallpaper(actual, img, mode, cache_quality);
            if (enable_wallust) {
                if (wallust_available()) {
                    wallust_run(img);
                }
                wallust_hook_run(wallust_hook, img);
            }
            free(img);
        }
        sleep((unsigned)interval);
    }

    clear_pid();
    return 0;
}
