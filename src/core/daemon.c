/* archpaper - Copyright (C) 2024 archpaper contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "archpaper/daemon.h"
#include "archpaper/library.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/wallpaper.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running;
static void stop_signal(int sig) { (void)sig; running = 0; }
static int daemon_cancelled(void *unused) { (void)unused; return !running; }

static int open_lock(void) {
    char path[4096];
    if (ap_runtime_path("daemon.lock", path, sizeof(path)) != AP_OK) return -1;
    return open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
}

int daemon_status(int *pid) {
    if (!pid) return AP_INVALID;
    *pid = 0;
    int fd = open_lock();
    if (fd < 0) return AP_IO;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    int rc = fcntl(fd, F_GETLK, &lock);
    close(fd);
    if (rc < 0) return AP_IO;
    if (lock.l_type != F_UNLCK) *pid = (int)lock.l_pid;
    return AP_OK;
}

int daemon_stop(void) {
    int pid;
    int rc = daemon_status(&pid);
    if (rc != AP_OK || pid <= 0) return rc;
    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) return AP_PROCESS;
    /* A running conversion may still be winding down. Never kill unrelated
     * wallpaper processes or trust a stale PID file. */
    for (int i = 0; i < 100; ++i) {
        usleep(20000);
        int current;
        rc = daemon_status(&current);
        if (rc != AP_OK) return rc;
        if (current != pid) return AP_OK;
    }
    return AP_TIMEOUT;
}

int daemon_start(const char *dir, const config_t *cfg) {
    if (!dir || !is_dir(dir) || config_validate(cfg) != AP_OK) return AP_INVALID;
    int pid;
    int rc = daemon_status(&pid);
    if (rc != AP_OK) return rc;
    if (pid) return AP_BUSY;
    char executable[4096];
    ssize_t n = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (n < 0 || n >= (ssize_t)sizeof(executable) - 1) return AP_IO;
    executable[n] = '\0';
    char interval[32], wallust[2], hwdec[2];
    snprintf(interval, sizeof(interval), "%d", cfg->daemon_interval);
    snprintf(wallust, sizeof(wallust), "%d", cfg->wallust_enabled);
    snprintf(hwdec, sizeof(hwdec), "%d", cfg->mpvpaper_hwdec);
    const char *args[] = {executable, "__daemon-run", dir, interval, backend_to_string(cfg->backend),
        cfg->mode, wallust, cfg->wallust_hook, cfg->cache_quality, cfg->mpvpaper_profile, hwdec, NULL};
    rc = ap_process_detach(args);
    if (rc != AP_OK) return rc;
    for (int i = 0; i < 100; ++i) {
        usleep(20000);
        rc = daemon_status(&pid);
        if (rc != AP_OK) return rc;
        if (pid) return AP_OK;
    }
    return AP_TIMEOUT;
}

/* Runs only after exec into the dedicated CLI command, never in a forked Qt
 * process. The kernel owns the single-instance lock and releases it on exit. */
int daemon_run(const char *dir, const config_t *cfg) {
    if (!dir || !is_dir(dir) || config_validate(cfg) != AP_OK) return AP_INVALID;
    int fd = open_lock();
    if (fd < 0) return AP_IO;
    running = 1;
    struct sigaction action = {0};
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (fcntl(fd, F_SETLK, &lock) < 0) { close(fd); return AP_BUSY; }
    ap_process_set_cancel_check(daemon_cancelled, NULL);
    while (running) {
        char *path = NULL;
        if (ap_library_random(dir, &path) == AP_OK) {
            ap_apply_result result;
            ap_wallpaper_apply(path, cfg, 0, &result);
            free(path);
        }
        struct timespec remaining = {.tv_sec = cfg->daemon_interval};
        while (running && nanosleep(&remaining, &remaining) < 0 && errno == EINTR) {}
    }
    close(fd);
    ap_process_set_cancel_check(NULL, NULL);
    return AP_OK;
}

int daemonize_random(const char *dir, int interval, backend_t backend, const char *mode,
                     int wallust, const char *hook, const char *quality) {
    config_t cfg;
    int rc = config_load(&cfg);
    if (rc != AP_OK) return rc;
    cfg.backend = backend;
    cfg.daemon_interval = interval;
    cfg.wallust_enabled = wallust;
    if (ap_copy_string(cfg.mode, sizeof(cfg.mode), mode ? mode : "fill") != AP_OK ||
        ap_copy_string(cfg.wallust_hook, sizeof(cfg.wallust_hook), hook ? hook : "") != AP_OK ||
        ap_copy_string(cfg.cache_quality, sizeof(cfg.cache_quality), quality ? quality : "monitor") != AP_OK) return AP_INVALID;
    return daemon_start(dir, &cfg);
}
