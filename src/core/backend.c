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

#include <dirent.h>
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

static const char *map_awww_mode(const char *mode) {
    if (!mode) return "crop";
    if (strcasecmp(mode, "fill") == 0) return "crop";
    if (strcasecmp(mode, "fit") == 0) return "fit";
    if (strcasecmp(mode, "stretch") == 0) return "stretch";
    if (strcasecmp(mode, "center") == 0) return "no";
    return "crop"; /* tile and unknown values fallback to fill/crop */
}

static const char *map_mpvpaper_mode_options(const char *mode) {
    if (!mode) return "";
    if (strcasecmp(mode, "fill") == 0) return " panscan=1.0";
    if (strcasecmp(mode, "fit") == 0) return "";
    if (strcasecmp(mode, "stretch") == 0) return " keepaspect=no";
    if (strcasecmp(mode, "center") == 0) return " video-unscaled=yes";
    return " panscan=1.0"; /* tile fallback to fill */
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

/* ----------------------------------------------------------------------
 * Cache helpers
 * ---------------------------------------------------------------------- */

#define CACHE_MAX_FILES 50
#define CACHE_MAX_BYTES (2LL * 1024 * 1024 * 1024) /* 2 GiB */

struct cache_entry {
    char path[4096];
    off_t size;
    time_t mtime;
};

static int cache_entry_cmp(const void *a, const void *b) {
    const struct cache_entry *ea = a;
    const struct cache_entry *eb = b;
    if (ea->mtime < eb->mtime) return 1;
    if (ea->mtime > eb->mtime) return -1;
    return 0;
}

static void prune_cache_dir(const char *cache_dir) {
    DIR *d = opendir(cache_dir);
    if (!d) return;

    struct cache_entry *entries = NULL;
    size_t count = 0;
    size_t cap = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", cache_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            struct cache_entry *tmp = realloc(entries, cap * sizeof(*entries));
            if (!tmp) break;
            entries = tmp;
        }
        strncpy(entries[count].path, full, sizeof(entries[count].path) - 1);
        entries[count].path[sizeof(entries[count].path) - 1] = '\0';
        entries[count].size = st.st_size;
        entries[count].mtime = st.st_mtime;
        count++;
    }
    closedir(d);

    if (count == 0) {
        free(entries);
        return;
    }

    qsort(entries, count, sizeof(*entries), cache_entry_cmp);

    off_t total = 0;
    for (size_t i = 0; i < count; i++) total += entries[i].size;

    /* First drop older files over the file limit. */
    for (size_t i = CACHE_MAX_FILES; i < count; i++) {
        total -= entries[i].size;
        unlink(entries[i].path);
    }
    if (count > CACHE_MAX_FILES)
        count = CACHE_MAX_FILES;

    /* Then, if still over the byte cap, delete oldest files first. */
    for (size_t i = count; i > 0 && total > CACHE_MAX_BYTES; i--) {
        total -= entries[i - 1].size;
        unlink(entries[i - 1].path);
    }

    free(entries);
}

static const char *cache_dir_for(const char *subdir) {
    static char dir[4096];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(dir, sizeof(dir), "%s/.cache/archpaper/%s", home, subdir);
    return dir;
}

static int ensure_cache_dir(const char *cache_dir) {
    char mkdir_cmd[4096];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p '%s'", cache_dir);
    return system(mkdir_cmd);
}

static void get_image_resolution(const char *path, int *width, int *height) {
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

static void pick_target_resolution(int *target_w, int *target_h) {
    int mon_w = 0, mon_h = 0;
    detect_monitor_resolution(&mon_w, &mon_h);
    int gpu = detect_gpu_vendor();

    if (gpu == GPU_NVIDIA || mon_w <= 0 || mon_h <= 0) {
        *target_w = 1280;
        *target_h = 720;
        return;
    }

    if (mon_w > 1920 || mon_h > 1080) {
        *target_w = 1920;
        *target_h = 1080;
        return;
    }

    *target_w = mon_w;
    *target_h = mon_h;
}

static const char *cached_video_path(const char *path) {
    static char cache_path[4096];
    struct stat st;
    if (!cmd_exists("ffmpeg") || stat(path, &st) != 0)
        return path;

    int vid_w = 0, vid_h = 0;
    get_video_resolution(path, &vid_w, &vid_h);

    int target_w, target_h;
    pick_target_resolution(&target_w, &target_h);

    /* No need to transcode if the video already fits inside the target box. */
    if (vid_w > 0 && vid_h > 0 && vid_w <= target_w && vid_h <= target_h)
        return path;

    unsigned long h = fnv1a_hash(path, 0);
    h ^= (unsigned long)st.st_mtime;
    h *= 1099511628211ULL;
    h ^= (unsigned long)target_w;
    h ^= (unsigned long)target_h << 16;

    const char *cache_dir = cache_dir_for("videos");
    snprintf(cache_path, sizeof(cache_path), "%s/%016lx_%dx%d.mp4", cache_dir, h, target_w, target_h);

    if (file_exists(cache_path))
        return cache_path;

    if (ensure_cache_dir(cache_dir) != 0)
        return path;
    prune_cache_dir(cache_dir);

    char ffmpeg_cmd[8192];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
             "ffmpeg -y -i '%s' -vf 'scale=%d:%d:force_original_aspect_ratio=decrease:flags=fast_bilinear,fps=30' "
             "-c:v libx264 -preset ultrafast -crf 28 -an -movflags +faststart '%s' >/dev/null 2>&1",
             path, target_w, target_h, cache_path);

    if (system(ffmpeg_cmd) == 0 && file_exists(cache_path))
        return cache_path;

    return path;
}

static const char *cached_animated_path(const char *path) {
    static char cache_path[4096];
    struct stat st;
    if (!cmd_exists("ffmpeg") || stat(path, &st) != 0)
        return path;

    int img_w = 0, img_h = 0;
    get_image_resolution(path, &img_w, &img_h);

    int target_w, target_h;
    pick_target_resolution(&target_w, &target_h);

    /* If the animated image already fits, send it as-is. */
    if (img_w > 0 && img_h > 0 && img_w <= target_w && img_h <= target_h)
        return path;

    unsigned long h = fnv1a_hash(path, 0);
    h ^= (unsigned long)st.st_mtime;
    h *= 1099511628211ULL;
    h ^= (unsigned long)target_w;
    h ^= (unsigned long)target_h << 16;

    const char *cache_dir = cache_dir_for("animated");
    const char *ext = file_extension(path);
    const char *out_ext = "gif";
    if (ext && strcasecmp(ext, "webp") == 0)
        out_ext = "webp";

    snprintf(cache_path, sizeof(cache_path), "%s/%016lx_%dx%d.%s", cache_dir, h, target_w, target_h, out_ext);

    if (file_exists(cache_path))
        return cache_path;

    if (ensure_cache_dir(cache_dir) != 0)
        return path;
    prune_cache_dir(cache_dir);

    char ffmpeg_cmd[8192];
    if (out_ext[0] == 'w') { /* webp -> animated webp */
        snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
                 "ffmpeg -y -i '%s' -vf 'scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos' "
                 "-loop 0 -c:v libwebp -lossless 0 -qscale 80 -an '%s' >/dev/null 2>&1",
                 path, target_w, target_h, cache_path);
    } else { /* gif -> resized gif preserving animation */
        snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
                 "ffmpeg -y -i '%s' -vf 'fps=30,scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos,"
                 "split[s0][s1];[s0]palettegen=max_colors=128[p];[s1][p]paletteuse=dither=bayer' "
                 "-loop 0 -c:v gif -an '%s' >/dev/null 2>&1",
                 path, target_w, target_h, cache_path);
    }

    if (system(ffmpeg_cmd) == 0 && file_exists(cache_path))
        return cache_path;

    return path;
}

static void build_mpvpaper_options(char *buf, size_t len, const char *mode) {
    int gpu = detect_gpu_vendor();
    const char *mode_opts = map_mpvpaper_mode_options(mode);

    /* Base options that reduce CPU usage even when hwaccel works. */
    snprintf(buf, len,
             "no-audio loop-file=inf vd-lavc-threads=2 scale=bilinear%s",
             mode_opts);

    /* Only trust hardware decoding on Intel/AMD; NVIDIA on Wayland usually fails
     * to initialize vaapi/vdpau and falls back to CPU anyway, while spamming
     * the logs. */
    if (gpu == GPU_INTEL || gpu == GPU_AMD || gpu == GPU_UNKNOWN) {
        size_t n = strlen(buf);
        snprintf(buf + n, len - n, " hwdec=auto-safe");
    }
}

static int set_mpvpaper(const char *path, const char *mode) {
    /* mpvpaper [options] <monitor> <path>; '*' applies to all monitors.
     * For high-res videos on NVIDIA/weak GPUs, transcode to a smaller copy
     * first so mpvpaper doesn't have to decode 1440p/4K@60 in software.
     * run_fork() already backgrounds the process, so avoid mpvpaper's own
     * --fork which leaves orphan mpv children that cannot be killed cleanly. */
    const char *actual = cached_video_path(path);
    static char options[256];
    build_mpvpaper_options(options, sizeof(options), mode);

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

static int set_awww(const char *path, const char *mode) {
    /* awww requires awww-daemon running; start it if the user hasn't already. */
    if (ensure_awww_daemon() != 0)
        return 1;

    const char *actual = cached_animated_path(path);
    const char *resize = map_awww_mode(mode);
    const char *argv[] = {SWWW_CMD, "img", "--transition-type", "none", "--resize", resize, actual, NULL};
    return run_fork(argv);
}

const char *backend_optimized_path(const char *path) {
    if (!path || !file_exists(path))
        return path;

    if (is_video(path))
        return cached_video_path(path);
    if (is_animated_image(path))
        return cached_animated_path(path);
    return path;
}

int set_wallpaper(backend_t b, const char *path, const char *mode) {
    if (!path || !file_exists(path)) return 1;

    kill_wallpaper_backends();

    switch (b) {
        case BACKEND_HYPRPAPER: return set_hyprpaper(path);
        case BACKEND_MPVPPAPER: return set_mpvpaper(path, mode);
        case BACKEND_SWWW: return set_awww(path, mode);
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
