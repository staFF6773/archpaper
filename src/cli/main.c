/* archpaper - Copyright (C) 2024 archpaper contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * CLI frontend for the shared C application API. */
#include "archpaper/cli.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/history.h"
#include "archpaper/library.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/wallpaper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *name) {
    printf("Usage: %s <command> [options]\n\n"
           "  set <media>              Apply an image, animation or video\n"
           "  random <directory>       Apply a random wallpaper\n"
           "  clear                    Clear the wallpaper\n"
           "  list <directory>         List supported regular files\n"
           "  favorites                List favorite wallpapers\n"
           "  favorite <media>         Toggle a favorite\n"
           "  recent                   List recent wallpapers\n"
           "  daemon <directory>       Start automatic changes\n"
           "  daemon stop|status       Stop or inspect automatic changes\n"
           "  status | backend         Show saved settings/backend\n\n"
           "Options for set/random/daemon:\n"
           "  --backend swaybg|hyprpaper|awww|mpvpaper\n"
           "  --mode fill|fit|stretch|center|tile\n"
           "  --wallust | --wallust-hook <script>\n"
           "  --cache-quality original|monitor|low\n"
           "  --mpvpaper-profile quality|balanced|performance\n"
           "  --hwdec                  Enable mpvpaper hardware decoding\n"
           "  --interval <10..86400>    Daemon interval in seconds\n", name);
}

static int report(int rc) {
    if (rc != AP_OK) fprintf(stderr, "archpaper: %s\n", ap_error_string((ap_result)rc));
    return rc;
}

static int parse_backend(const char *value, backend_t *out) {
    if (strcmp(value, "swaybg") && strcmp(value, "hyprpaper") && strcmp(value, "awww") && strcmp(value, "mpvpaper")) return AP_INVALID;
    *out = backend_from_string(value);
    return AP_OK;
}

static int internal_daemon(int argc, char **argv) {
    if (argc != 11) return AP_INVALID;
    config_t cfg;
    config_default(&cfg);
    if (config_parse_interval(argv[3], &cfg.daemon_interval) != AP_OK || parse_backend(argv[4], &cfg.backend) != AP_OK ||
        ap_copy_string(cfg.mode, sizeof(cfg.mode), argv[5]) != AP_OK ||
        (strcmp(argv[6], "0") && strcmp(argv[6], "1")) ||
        ap_copy_string(cfg.wallust_hook, sizeof(cfg.wallust_hook), argv[7]) != AP_OK ||
        ap_copy_string(cfg.cache_quality, sizeof(cfg.cache_quality), argv[8]) != AP_OK ||
        ap_copy_string(cfg.mpvpaper_profile, sizeof(cfg.mpvpaper_profile), argv[9]) != AP_OK ||
        (strcmp(argv[10], "0") && strcmp(argv[10], "1"))) return AP_INVALID;
    cfg.wallust_enabled = argv[6][0] == '1';
    cfg.mpvpaper_hwdec = argv[10][0] == '1';
    return daemon_run(argv[2], &cfg);
}

static int options(int argc, char **argv, config_t *cfg, const char **path) {
    *path = NULL;
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, "--wallust")) { cfg->wallust_enabled = 1; continue; }
        if (!strcmp(arg, "--hwdec")) { cfg->mpvpaper_hwdec = 1; continue; }
        if (!strcmp(arg, "--")) {
            if (*path || i + 2 != argc) return AP_INVALID;
            *path = argv[++i]; break;
        }
        if (arg[0] != '-') {
            if (*path) return AP_INVALID;
            *path = arg; continue;
        }
        if (++i >= argc) return AP_INVALID;
        const char *value = argv[i];
        int rc = AP_OK;
        if (!strcmp(arg, "--backend")) rc = parse_backend(value, &cfg->backend);
        else if (!strcmp(arg, "--mode")) rc = ap_copy_string(cfg->mode, sizeof(cfg->mode), value);
        else if (!strcmp(arg, "--cache-quality")) rc = ap_copy_string(cfg->cache_quality, sizeof(cfg->cache_quality), value);
        else if (!strcmp(arg, "--mpvpaper-profile")) rc = ap_copy_string(cfg->mpvpaper_profile, sizeof(cfg->mpvpaper_profile), value);
        else if (!strcmp(arg, "--interval")) rc = config_parse_interval(value, &cfg->daemon_interval);
        else if (!strcmp(arg, "--wallust-hook")) {
            char *expanded = expand_path(value);
            rc = expanded ? ap_copy_string(cfg->wallust_hook, sizeof(cfg->wallust_hook), expanded) : AP_NOMEM;
            free(expanded);
        } else return AP_INVALID;
        if (rc != AP_OK) return rc;
    }
    return config_validate(cfg);
}

int archpaper_cli(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return AP_INVALID; }
    const char *command = argv[1];
    if (!strcmp(command, "__daemon-run")) return internal_daemon(argc, argv);
    if (!strcmp(command, "--help") || !strcmp(command, "-h")) { usage(argv[0]); return AP_OK; }
    if (!strcmp(command, "daemon") && argc == 3 && !strcmp(argv[2], "stop")) return report(daemon_stop());
    if (!strcmp(command, "daemon") && argc == 3 && !strcmp(argv[2], "status")) {
        int pid;
        int rc = daemon_status(&pid);
        if (rc == AP_OK) { if (pid) printf("Running (PID %d)\n", pid); else puts("Stopped"); }
        return report(rc);
    }
    if ((!strcmp(command, "favorites") || !strcmp(command, "recent")) && argc == 2) {
        ap_path_list list = {0};
        int rc = ap_history_load(!strcmp(command, "favorites") ? AP_FAVORITES : AP_RECENT, &list);
        for (size_t i = 0; i < list.count; ++i) puts(list.paths[i]);
        ap_path_list_free(&list);
        return report(rc);
    }
    if (!strcmp(command, "favorite") && argc == 3) {
        char *expanded = expand_path(argv[2]);
        char *absolute = expanded ? realpath(expanded, NULL) : NULL;
        free(expanded);
        if (!absolute) return report(AP_NOT_FOUND);
        int favorite = 0;
        int rc = ap_favorite_toggle(absolute, &favorite);
        if (rc == AP_OK) printf("%s: %s\n", favorite ? "Favorited" : "Unfavorited", absolute);
        free(absolute);
        return report(rc);
    }
    if (!strcmp(command, "list") && argc == 3) {
        ap_path_list list = {0};
        int rc = ap_library_scan(argv[2], &list);
        for (size_t i = 0; i < list.count; ++i) puts(list.paths[i]);
        ap_path_list_free(&list);
        return report(rc);
    }
    if (!strcmp(command, "clear") && argc == 2) return report(ap_wallpaper_clear());
    config_t cfg;
    int rc = config_load(&cfg);
    if (rc != AP_OK) return report(rc);
    if ((!strcmp(command, "status") || !strcmp(command, "backend")) && argc == 2) {
        printf("Preferred backend: %s\nDetected backend: %s\n", backend_to_string(cfg.backend), backend_to_string(detect_backend()));
        if (!strcmp(command, "status")) printf("Mode: %s\nLast wallpaper: %s\n", cfg.mode, cfg.last_wallpaper[0] ? cfg.last_wallpaper : "(none)");
        return AP_OK;
    }
    if (strcmp(command, "set") && strcmp(command, "random") && strcmp(command, "daemon")) {
        usage(argv[0]); return report(AP_INVALID);
    }
    const char *argument;
    rc = options(argc, argv, &cfg, &argument);
    if (rc != AP_OK || !argument) return report(AP_INVALID);
    char *path = NULL;
    if (!strcmp(command, "random")) rc = ap_library_random(argument, &path);
    else { path = expand_path(argument); rc = path ? AP_OK : AP_NOMEM; }
    if (rc != AP_OK) return report(rc);
    if (!strcmp(command, "daemon")) {
        rc = daemon_start(path, &cfg);
        if (rc == AP_OK) printf("Daemon started (%d s)\n", cfg.daemon_interval);
    } else {
        ap_apply_result result;
        rc = ap_wallpaper_apply(path, &cfg, AP_APPLY_SAVE_OPTIONS, &result);
        if (rc == AP_OK) {
            printf("Applied with %s: %s\n", backend_to_string(result.backend), path);
            if (result.persistence != AP_OK) fprintf(stderr, "History/config: %s\n", ap_error_string(result.persistence));
            if (result.theme != AP_OK) fprintf(stderr, "Theme/hook: %s\n", ap_error_string(result.theme));
        }
    }
    free(path);
    return report(rc);
}
