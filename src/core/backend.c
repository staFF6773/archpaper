/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE

#include "archpaper/backend.h"
#include "archpaper/utils.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SWAYBG_CMD "swaybg"
#define HYPRPAPER_CMD "hyprpaper"
#define MPVPAPER_CMD "mpvpaper"
#define SWWW_CMD "awww"
#define SWWW_DAEMON_CMD "awww-daemon"

const char *backend_to_string(backend_t b) {
    switch (b) {
        case BACKEND_HYPRPAPER: return "hyprpaper";
        case BACKEND_MPVPPAPER: return "mpvpaper";
        case BACKEND_SWWW: return "awww";
        default: return "swaybg";
    }
}

backend_t backend_from_string(const char *s) {
    if (!s) return BACKEND_SWAYBG;
    if (strcasecmp(s, "hyprpaper") == 0) return BACKEND_HYPRPAPER;
    if (strcasecmp(s, "mpvpaper") == 0) return BACKEND_MPVPPAPER;
    if (strcasecmp(s, "awww") == 0) return BACKEND_SWWW;
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
        case BACKEND_SWWW: return cmd_exists(SWWW_CMD) && cmd_exists(SWWW_DAEMON_CMD);
        default: return cmd_exists(SWAYBG_CMD);
    }
}

backend_t detect_backend(void) {
    /* Prefer awww when available: efficient for static and animated wallpapers. */
    if (backend_available(BACKEND_SWWW))
        return BACKEND_SWWW;
    /* If running under Hyprland and hyprpaper is available, prefer it. */
    if (getenv("HYPRLAND_INSTANCE_SIGNATURE") && backend_available(BACKEND_HYPRPAPER))
        return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

backend_t select_backend_for_path(const char *path, backend_t preferred) {
    if (!path) return preferred;

    int video = is_video(path);
    int animated = is_animated_image(path);

    /* Videos can only be played by mpvpaper. */
    if (video) {
        if (preferred == BACKEND_MPVPPAPER)
            return preferred;
        if (backend_available(BACKEND_MPVPPAPER))
            return BACKEND_MPVPPAPER;
        return BACKEND_MPVPPAPER;
    }

    /* Animated GIF/WebP: awww is preferred, mpvpaper is a fallback. */
    if (animated) {
        if (preferred == BACKEND_SWWW || preferred == BACKEND_MPVPPAPER)
            return preferred;
        if (backend_available(BACKEND_SWWW))
            return BACKEND_SWWW;
        if (backend_available(BACKEND_MPVPPAPER))
            return BACKEND_MPVPPAPER;
        return BACKEND_SWWW;
    }

    /* Static images: mpvpaper cannot display these, so never pick it. */
    if (preferred != BACKEND_MPVPPAPER && backend_available(preferred))
        return preferred;
    if (backend_available(BACKEND_SWWW))
        return BACKEND_SWWW;
    if (backend_available(BACKEND_SWAYBG))
        return BACKEND_SWAYBG;
    if (backend_available(BACKEND_HYPRPAPER))
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
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    } else {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
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

enum {
    GPU_UNKNOWN,
    GPU_NVIDIA,
    GPU_INTEL,
    GPU_AMD
};

static int run_shell_output(const char *cmd, char *out, size_t out_len) {
    FILE *f = popen(cmd, "r");
    if (!f) return 1;
    out[0] = '\0';
    if (fgets(out, out_len, f) == NULL) {
        pclose(f);
        return 1;
    }
    pclose(f);
    return 0;
}

static void parse_resolution(const char *text, int *width, int *height) {
    *width = 0;
    *height = 0;
    if (!text) return;
    /* Look for the first occurrence of WxH where W and H are 3-5 digits. */
    const char *p = text;
    while (*p) {
        int w = 0, h = 0;
        if (sscanf(p, "%*[^0-9]%dx%d", &w, &h) == 2 || sscanf(p, "%dx%d", &w, &h) == 2) {
            if (w >= 640 && w <= 7680 && h >= 480 && h <= 4320) {
                *width = w;
                *height = h;
                return;
            }
        }
        p++;
    }
}

static void detect_monitor_resolution(int *width, int *height) {
    *width = 0;
    *height = 0;

    char buf[1024];
    const char *cmds[] = {
        "awww query 2>/dev/null",
        "wlr-randr --dryrun 2>/dev/null | grep -m1 'px,'",
        "hyprctl monitors 2>/dev/null | grep -m1 'x[0-9]*@'",
        "swaymsg -t get_outputs 2>/dev/null | grep -m1 '\"width\"'",
        NULL
    };

    for (int i = 0; cmds[i]; i++) {
        if (run_shell_output(cmds[i], buf, sizeof(buf)) == 0 && buf[0] != '\0') {
            parse_resolution(buf, width, height);
            if (*width > 0 && *height > 0) return;
        }
    }
}

static int detect_gpu_vendor(void) {
    FILE *f = fopen("/sys/class/drm/card0/device/vendor", "r");
    if (f) {
        char vendor[16];
        if (fgets(vendor, sizeof(vendor), f)) {
            fclose(f);
            if (strstr(vendor, "10de")) return GPU_NVIDIA;
            if (strstr(vendor, "8086")) return GPU_INTEL;
            if (strstr(vendor, "1002")) return GPU_AMD;
        }
        fclose(f);
    }

    char buf[256];
    if (run_shell_output("lspci -nn 2>/dev/null | grep -iE 'vga|3d|display' | head -1",
                         buf, sizeof(buf)) == 0 && buf[0] != '\0') {
        if (strcasestr(buf, "nvidia")) return GPU_NVIDIA;
        if (strcasestr(buf, "intel")) return GPU_INTEL;
        if (strcasestr(buf, "amd") || strcasestr(buf, "radeon")) return GPU_AMD;
    }
    return GPU_UNKNOWN;
}

static void get_video_resolution(const char *path, int *width, int *height) {
    *width = 0;
    *height = 0;
    if (!cmd_exists("ffprobe")) return;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
             "-of csv=s=x:p=0 '%s' 2>/dev/null", path);

    char buf[64];
    if (run_shell_output(cmd, buf, sizeof(buf)) == 0) {
        sscanf(buf, "%dx%d", width, height);
    }
}

static unsigned long fnv1a_hash(const char *s, unsigned long seed) {
    unsigned long h = seed ? seed : 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static const char *cached_video_path(const char *path) {
    static char cache_path[4096];
    struct stat st;
    if (!cmd_exists("ffmpeg") || stat(path, &st) != 0)
        return path;

    int mon_w = 0, mon_h = 0;
    int vid_w = 0, vid_h = 0;
    detect_monitor_resolution(&mon_w, &mon_h);
    get_video_resolution(path, &vid_w, &vid_h);
    int gpu = detect_gpu_vendor();

    /* Pick a target width that won't melt the CPU. NVIDIA on Wayland has no
     * working hwaccel in most setups, so go aggressive. For Intel/AMD hwaccel
     * usually works, so we can keep the monitor resolution. */
    int target_w = mon_w;
    if (gpu == GPU_NVIDIA || target_w <= 0) {
        target_w = 1280; /* 720p for safety */
    } else if (target_w > 1920) {
        target_w = 1920; /* cap at 1080p for animated wallpapers */
    }

    /* No need to transcode if the video is already small enough. */
    if (vid_w > 0 && vid_w <= target_w) {
        return path;
    }

    unsigned long h = fnv1a_hash(path, 0);
    h ^= (unsigned long)st.st_mtime;
    h *= 1099511628211ULL;
    h ^= (unsigned long)target_w;

    const char *home = getenv("HOME");
    if (!home) home = ".";
    char cache_dir[4096];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/archpaper/videos", home);
    snprintf(cache_path, sizeof(cache_path), "%s/%016lx_%dp.mp4", cache_dir, h, target_w);

    if (file_exists(cache_path))
        return cache_path;

    char mkdir_cmd[4096];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", cache_dir);
    if (system(mkdir_cmd) != 0)
        return path;

    char ffmpeg_cmd[8192];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
             "ffmpeg -y -i '%s' -vf 'scale=%d:-2:flags=fast_bilinear,fps=30' "
             "-c:v libx264 -preset ultrafast -crf 28 -an -movflags +faststart \"%s\" >/dev/null 2>&1",
             path, target_w, cache_path);

    if (system(ffmpeg_cmd) == 0 && file_exists(cache_path))
        return cache_path;

    return path;
}

static void build_mpvpaper_options(char *buf, size_t len) {
    int gpu = detect_gpu_vendor();

    /* Base options that reduce CPU usage even when hwaccel works. */
    snprintf(buf, len,
             "no-audio loop-file=inf vd-lavc-threads=2 scale=bilinear");

    /* Only trust hardware decoding on Intel/AMD; NVIDIA on Wayland usually fails
     * to initialize vaapi/vdpau and falls back to CPU anyway, while spamming
     * the logs. */
    if (gpu == GPU_INTEL || gpu == GPU_AMD || gpu == GPU_UNKNOWN) {
        size_t n = strlen(buf);
        snprintf(buf + n, len - n, " hwdec=auto-safe");
    }
}

static int set_mpvpaper(const char *path) {
    /* mpvpaper [options] <monitor> <path>; '*' applies to all monitors.
     * For high-res videos on NVIDIA/weak GPUs, transcode to a smaller copy
     * first so mpvpaper doesn't have to decode 1440p/4K@60 in software.
     * run_fork() already backgrounds the process, so avoid mpvpaper's own
     * --fork which leaves orphan mpv children that cannot be killed cleanly. */
    const char *actual = cached_video_path(path);
    static char options[256];
    build_mpvpaper_options(options, sizeof(options));

    const char *argv[] = {MPVPAPER_CMD, "-o", options, "*", actual, NULL};
    return run_fork(argv);
}

static int daemon_running(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pgrep -x %s >/dev/null 2>&1", name);
    return system(cmd) == 0;
}

static int start_awww_daemon(void) {
    const char *argv[] = {SWWW_DAEMON_CMD, NULL};
    return run_fork(argv);
}

static int ensure_awww_daemon(void) {
    if (daemon_running(SWWW_DAEMON_CMD))
        return 0;
    if (start_awww_daemon() != 0)
        return 1;
    /* Give the daemon time to bind its Wayland socket before sending img commands. */
    usleep(800000);
    if (!daemon_running(SWWW_DAEMON_CMD))
        return 1;
    return 0;
}

static int set_awww(const char *path) {
    /* awww requires awww-daemon running; start it if the user hasn't already. */
    if (ensure_awww_daemon() != 0)
        return 1;
    const char *argv[] = {SWWW_CMD, "img", "--transition-type", "none", path, NULL};
    return run_fork(argv);
}

int set_wallpaper(backend_t b, const char *path, const char *mode) {
    if (!path || !file_exists(path)) return 1;

    kill_wallpaper_backends();

    switch (b) {
        case BACKEND_HYPRPAPER: return set_hyprpaper(path);
        case BACKEND_MPVPPAPER: return set_mpvpaper(path);
        case BACKEND_SWWW: return set_awww(path);
        default: return set_swaybg(path, mode);
    }
}

int clear_wallpaper(void) {
    kill_wallpaper_backends();
    /* awww can clear to a black background. */
    if (backend_available(BACKEND_SWWW)) {
        ensure_awww_daemon();
        const char *argv[] = {SWWW_CMD, "clear", NULL};
        run_fork(argv);
    }
    return 0;
}
