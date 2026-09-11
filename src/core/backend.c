/* archpaper - Copyright (C) 2024 archpaper contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Wayland backend adapters. Process execution and caching live in the C core. */
#include "archpaper/backend.h"
#include "archpaper/cache.h"
#include "archpaper/config.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *backend_to_string(backend_t b) {
    switch (b) {
    case BACKEND_SWAYBG: return "swaybg";
    case BACKEND_HYPRPAPER: return "hyprpaper";
    case BACKEND_MPVPPAPER: return "mpvpaper";
    case BACKEND_SWWW: return "awww";
    }
    return "unknown";
}

backend_t backend_from_string(const char *s) {
    if (s && !strcasecmp(s, "hyprpaper")) return BACKEND_HYPRPAPER;
    if (s && !strcasecmp(s, "mpvpaper")) return BACKEND_MPVPPAPER;
    if (s && !strcasecmp(s, "awww")) return BACKEND_SWWW;
    return BACKEND_SWAYBG;
}

int backend_available(backend_t b) {
    if (b < BACKEND_SWAYBG || b > BACKEND_SWWW) return 0;
    return ap_process_available(backend_to_string(b)) &&
           (b != BACKEND_SWWW || ap_process_available("awww-daemon"));
}

backend_t detect_backend(void) {
    if (backend_available(BACKEND_SWWW)) return BACKEND_SWWW;
    if (getenv("HYPRLAND_INSTANCE_SIGNATURE") && backend_available(BACKEND_HYPRPAPER)) return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

backend_t select_backend_for_path(const char *path, backend_t preferred) {
    if (!path) return preferred;
    if (is_video(path)) return BACKEND_MPVPPAPER;
    if (is_animated_image(path)) {
        if (preferred == BACKEND_SWWW || preferred == BACKEND_MPVPPAPER) return preferred;
        if (backend_available(BACKEND_SWWW)) return BACKEND_SWWW;
        if (backend_available(BACKEND_MPVPPAPER)) return BACKEND_MPVPPAPER;
        return BACKEND_SWWW;
    }
    if (preferred != BACKEND_MPVPPAPER && backend_available(preferred)) return preferred;
    if (backend_available(BACKEND_SWWW)) return BACKEND_SWWW;
    if (backend_available(BACKEND_SWAYBG)) return BACKEND_SWAYBG;
    if (backend_available(BACKEND_HYPRPAPER)) return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

const char *cache_quality_from_string(const char *s) {
    if (s && !strcasecmp(s, "original")) return "original";
    if (s && !strcasecmp(s, "low")) return "low";
    return "monitor";
}

static int stop_backends(void) {
    char uid[32];
    snprintf(uid, sizeof(uid), "%lu", (unsigned long)getuid());
    const char *names[] = {"swaybg", "hyprpaper", "mpvpaper"};
    for (size_t i = 0; i < 3; ++i) {
        const char *args[] = {"pkill", "-u", uid, "-x", names[i], NULL};
        ap_process p;
        ap_result rc = ap_process_start(&p, args, NULL, 0);
        if (rc != AP_OK) return rc;
        rc = ap_process_wait(&p, 2000);
        /* pkill status 1 means no matching backend, which is normal. */
        if (rc != AP_OK && !(rc == AP_PROCESS && p.exit_code == 1)) return rc;
    }
    usleep(100000);
    return AP_OK;
}

static int ensure_awww(void) {
    const char *query[] = {"awww", "query", NULL};
    if (ap_process_run(query, 1000, NULL, 0) == AP_OK) return AP_OK;
    const char *start[] = {"awww-daemon", NULL};
    int rc = ap_process_detach(start);
    if (rc != AP_OK) return rc;
    for (int i = 0; i < 20; ++i) {
        usleep(100000);
        if (ap_process_run(query, 500, NULL, 0) == AP_OK) return AP_OK;
    }
    return AP_TIMEOUT;
}

static ap_result hyprpaper_config(FILE *f, const void *data) {
    fprintf(f, "preload = %s\nwallpaper = ,%s\nsplash = false\n", (const char *)data, (const char *)data);
    return ferror(f) ? AP_IO : AP_OK;
}

int set_wallpaper(backend_t b, const char *path, const char *mode, const char *quality) {
    config_t cfg;
    int rc = config_load(&cfg);
    if (rc != AP_OK) return rc;
    cfg.backend = b;
    if (ap_copy_string(cfg.mode, sizeof(cfg.mode), mode ? mode : "fill") != AP_OK ||
        ap_copy_string(cfg.cache_quality, sizeof(cfg.cache_quality), cache_quality_from_string(quality)) != AP_OK) return AP_INVALID;
    return backend_apply(path, &cfg);
}

int backend_apply(const char *path, const config_t *cfg) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return AP_NOT_FOUND;
    if (strchr(path, '\n') || strchr(path, '\r') || config_validate(cfg) != AP_OK) return AP_INVALID;
    if (!backend_available(cfg->backend)) return AP_NOT_FOUND;
    char actual[4096];
    int rc = ap_cache_prepare(path, cfg->cache_quality, actual, sizeof(actual));
    if (rc != AP_OK) return rc;
    rc = stop_backends();
    if (rc != AP_OK) return rc;
    switch (cfg->backend) {
    case BACKEND_SWAYBG: {
        const char *args[] = {"swaybg", "-i", actual, "-m", cfg->mode, NULL};
        return ap_process_detach(args);
    }
    case BACKEND_HYPRPAPER: {
        char config[4096];
        rc = ap_runtime_path("hyprpaper.conf", config, sizeof(config));
        if (rc != AP_OK) return rc;
        rc = ap_write_atomic(config, hyprpaper_config, actual);
        if (rc != AP_OK) return rc;
        const char *args[] = {"hyprpaper", "--config", config, NULL};
        return ap_process_detach(args);
    }
    case BACKEND_MPVPPAPER: {
        const char *mode = !strcmp(cfg->mode, "fit") ? "" : !strcmp(cfg->mode, "stretch") ? " keepaspect=no" :
                           !strcmp(cfg->mode, "center") ? " video-unscaled=yes" : " panscan=1.0";
        const char *profile = !strcmp(cfg->mpvpaper_profile, "quality") ? " video-sync=display-resample scale=lanczos" :
            !strcmp(cfg->mpvpaper_profile, "balanced") ? " video-sync=desync scale=bilinear deband=no dither=no" :
            " video-sync=desync scale=bilinear deband=no dither=no correct-pts=no";
        char options[512];
        snprintf(options, sizeof(options), "no-audio loop-file=inf pause=no vd-lavc-threads=4 "
            "cache=no demuxer-readahead-secs=0 demuxer-max-bytes=5M%s%s%s", mode, profile,
            cfg->mpvpaper_hwdec ? " hwdec=auto-safe" : "");
        const char *args[] = {"mpvpaper", "-o", options, "*", actual, NULL};
        return ap_process_detach(args);
    }
    case BACKEND_SWWW: {
        rc = ensure_awww();
        if (rc != AP_OK) return rc;
        const char *resize = !strcmp(cfg->mode, "fit") ? "fit" : !strcmp(cfg->mode, "stretch") ? "stretch" :
                             !strcmp(cfg->mode, "center") ? "no" : "crop";
        const char *args[] = {"awww", "img", "--transition-type", "none", "--resize", resize, actual, NULL};
        return ap_process_run(args, 30000, NULL, 0);
    }
    }
    return AP_INVALID;
}

int backend_optimized_path(const char *path, const char *quality, char *out, size_t size) {
    return ap_cache_prepare(path, quality, out, size);
}

int clear_wallpaper(void) {
    int rc = stop_backends();
    if (rc != AP_OK) return rc;
    if (backend_available(BACKEND_SWWW)) {
        rc = ensure_awww();
        if (rc != AP_OK) return rc;
        const char *args[] = {"awww", "clear", NULL};
        return ap_process_run(args, 5000, NULL, 0);
    }
    return AP_OK;
}
