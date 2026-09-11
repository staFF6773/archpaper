/* archpaper - Copyright (C) 2024 archpaper contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Configuration shared by the C CLI, daemon and optional GUI. */
#define _POSIX_C_SOURCE 200809L
#include "archpaper/config.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

void config_default(config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->backend = detect_backend();
    strcpy(cfg->mode, "fill");
    cfg->daemon_interval = 300;
    strcpy(cfg->cache_quality, "monitor");
    strcpy(cfg->mpvpaper_profile, "quality");
}

static int one_of(const char *s, const char *const values[]) {
    for (size_t i = 0; values[i]; ++i) if (strcmp(s, values[i]) == 0) return 1;
    return 0;
}

static int valid_text(const char *s, size_t size) {
    return memchr(s, '\0', size) && !strchr(s, '\n') && !strchr(s, '\r');
}

int config_validate(const config_t *cfg) {
    if (!cfg || cfg->backend < BACKEND_SWAYBG || cfg->backend > BACKEND_SWWW ||
        cfg->folder_count < 0 || cfg->folder_count > MAX_FAVORITE_FOLDERS ||
        cfg->daemon_interval < 10 || cfg->daemon_interval > 86400 ||
        (cfg->wallust_enabled != 0 && cfg->wallust_enabled != 1) ||
        (cfg->mpvpaper_hwdec != 0 && cfg->mpvpaper_hwdec != 1)) return AP_INVALID;
    if (!valid_text(cfg->mode, sizeof(cfg->mode)) ||
        !one_of(cfg->mode, (const char *const[]){"fill", "fit", "stretch", "center", "tile", NULL}) ||
        !valid_text(cfg->cache_quality, sizeof(cfg->cache_quality)) ||
        !one_of(cfg->cache_quality, (const char *const[]){"original", "monitor", "low", NULL}) ||
        !valid_text(cfg->mpvpaper_profile, sizeof(cfg->mpvpaper_profile)) ||
        !one_of(cfg->mpvpaper_profile, (const char *const[]){"quality", "balanced", "performance", NULL}) ||
        !valid_text(cfg->last_wallpaper, sizeof(cfg->last_wallpaper)) ||
        !valid_text(cfg->wallust_hook, sizeof(cfg->wallust_hook))) return AP_INVALID;
    for (int i = 0; i < cfg->folder_count; ++i)
        if (!valid_text(cfg->folders[i], sizeof(cfg->folders[i])) || !cfg->folders[i][0]) return AP_INVALID;
    return AP_OK;
}

int config_parse_interval(const char *value, int *seconds) {
    if (!value || !*value || !seconds) return AP_INVALID;
    char *end;
    errno = 0;
    long n = strtol(value, &end, 10);
    if (errno || *end || n < 10 || n > 86400) return AP_INVALID;
    *seconds = (int)n;
    return AP_OK;
}

static int parse_bool(const char *value, int *out) {
    if (!strcmp(value, "true") || !strcmp(value, "1")) *out = 1;
    else if (!strcmp(value, "false") || !strcmp(value, "0")) *out = 0;
    else return AP_INVALID;
    return AP_OK;
}

int config_load(config_t *cfg) {
    if (!cfg) return AP_INVALID;
    config_default(cfg);
    char path[4096];
    ap_result rc = ap_config_path("config", path, sizeof(path));
    if (rc != AP_OK) return rc;
    FILE *file = fopen(path, "re");
    if (!file) return errno == ENOENT ? AP_OK : AP_IO;
    config_t parsed = *cfg;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t len;
    while ((len = getline(&line, &capacity, file)) >= 0) {
        if ((size_t)len != strlen(line) || len > 8192) { rc = AP_INVALID; break; }
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *value = strchr(line, '=');
        if (!value) { rc = AP_INVALID; break; }
        *value++ = '\0';
        if (!strcmp(line, "backend")) {
            if (!one_of(value, (const char *const[]){"swaybg", "hyprpaper", "awww", "mpvpaper", NULL})) rc = AP_INVALID;
            else parsed.backend = backend_from_string(value);
        } else if (!strcmp(line, "mode")) rc = ap_copy_string(parsed.mode, sizeof(parsed.mode), value);
        else if (!strcmp(line, "last")) rc = ap_copy_string(parsed.last_wallpaper, sizeof(parsed.last_wallpaper), value);
        else if (!strcmp(line, "folder")) rc = config_add_folder(&parsed, value);
        else if (!strcmp(line, "wallust")) rc = parse_bool(value, &parsed.wallust_enabled);
        else if (!strcmp(line, "wallust_hook")) {
            char *expanded = expand_path(value);
            rc = expanded ? ap_copy_string(parsed.wallust_hook, sizeof(parsed.wallust_hook), expanded) : AP_NOMEM;
            free(expanded);
        } else if (!strcmp(line, "daemon_interval")) rc = config_parse_interval(value, &parsed.daemon_interval);
        else if (!strcmp(line, "cache_quality")) rc = ap_copy_string(parsed.cache_quality, sizeof(parsed.cache_quality), value);
        else if (!strcmp(line, "mpvpaper_profile")) rc = ap_copy_string(parsed.mpvpaper_profile, sizeof(parsed.mpvpaper_profile), value);
        else if (!strcmp(line, "mpvpaper_hwdec")) rc = parse_bool(value, &parsed.mpvpaper_hwdec);
        /* Unknown keys remain forward-compatible. */
        if (rc != AP_OK) break;
    }
    if (ferror(file) || (rc == AP_OK && !feof(file))) rc = AP_IO;
    free(line);
    fclose(file);
    if (rc == AP_OK) rc = config_validate(&parsed);
    if (rc == AP_OK) *cfg = parsed;
    return rc;
}

static ap_result write_config(FILE *f, const void *data) {
    const config_t *cfg = data;
    fprintf(f, "backend=%s\nmode=%s\nwallust=%s\nwallust_hook=%s\ndaemon_interval=%d\n"
               "cache_quality=%s\nmpvpaper_profile=%s\nmpvpaper_hwdec=%s\nlast=%s\n",
            backend_to_string(cfg->backend), cfg->mode, cfg->wallust_enabled ? "true" : "false",
            cfg->wallust_hook, cfg->daemon_interval, cfg->cache_quality, cfg->mpvpaper_profile,
            cfg->mpvpaper_hwdec ? "true" : "false", cfg->last_wallpaper);
    for (int i = 0; i < cfg->folder_count; ++i) fprintf(f, "folder=%s\n", cfg->folders[i]);
    return ferror(f) ? AP_IO : AP_OK;
}

static int save_unlocked(const config_t *cfg) {
    char path[4096];
    int rc = ap_config_path("config", path, sizeof(path));
    if (rc != AP_OK) return rc;
    return ap_write_atomic(path, write_config, cfg);
}

static int lock_config(int *fd) {
    char path[4096];
    int rc = ap_config_path("", path, sizeof(path));
    if (rc != AP_OK) return rc;
    rc = ap_mkdirs(path);
    if (rc != AP_OK) return rc;
    rc = ap_config_path(".config.lock", path, sizeof(path));
    if (rc != AP_OK) return rc;
    *fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (*fd < 0) return AP_IO;
    while (flock(*fd, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(*fd); return AP_IO;
    }
    return AP_OK;
}

int config_save(const config_t *cfg) {
    int rc = config_validate(cfg);
    if (rc != AP_OK) return rc;
    int fd;
    rc = lock_config(&fd);
    if (rc != AP_OK) return rc;
    rc = save_unlocked(cfg);
    close(fd);
    return rc;
}

int config_record_wallpaper(const char *path, const config_t *options) {
    if (!path || strlen(path) >= 4096 || strchr(path, '\n') || strchr(path, '\r') ||
        (options && config_validate(options) != AP_OK)) return AP_INVALID;
    int fd;
    int rc = lock_config(&fd);
    if (rc != AP_OK) return rc;
    config_t saved;
    rc = config_load(&saved);
    if (rc == AP_OK) {
        if (options) {
            config_t merged = *options;
            memcpy(merged.folders, saved.folders, sizeof(saved.folders));
            merged.folder_count = saved.folder_count;
            saved = merged;
        }
        strcpy(saved.last_wallpaper, path);
        rc = save_unlocked(&saved);
    }
    close(fd);
    return rc;
}

int config_add_folder(config_t *cfg, const char *path) {
    if (!cfg || !path || !*path || strlen(path) >= MAX_FOLDER_LEN || strchr(path, '\n') || strchr(path, '\r') ||
        cfg->folder_count < 0 || cfg->folder_count > MAX_FAVORITE_FOLDERS) return AP_INVALID;
    for (int i = 0; i < cfg->folder_count; ++i) if (!strcmp(cfg->folders[i], path)) return AP_OK;
    if (cfg->folder_count == MAX_FAVORITE_FOLDERS) return AP_INVALID;
    strcpy(cfg->folders[cfg->folder_count++], path);
    return AP_OK;
}

int config_remove_folder(config_t *cfg, int index) {
    if (!cfg || cfg->folder_count > MAX_FAVORITE_FOLDERS || index < 0 || index >= cfg->folder_count) return AP_INVALID;
    for (int i = index; i < cfg->folder_count - 1; ++i) strcpy(cfg->folders[i], cfg->folders[i + 1]);
    cfg->folders[--cfg->folder_count][0] = '\0';
    return AP_OK;
}
