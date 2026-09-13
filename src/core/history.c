#define _GNU_SOURCE
#include "archpaper/history.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/engine.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *history_name(ap_history_kind kind) {
    return kind == AP_FAVORITES ? "favorites" : kind == AP_RECENT ? "recent" : NULL;
}

static ap_result read_history(const char *path, ap_path_list *out, int existing_only) {
    FILE *file = fopen(path, "re");
    if (!file) return errno == ENOENT ? AP_OK : AP_IO;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t len;
    ap_result rc = AP_OK;
    while ((len = getline(&line, &capacity, file)) >= 0) {
        if ((size_t)len != strlen(line) || len > 4096) { rc = AP_INVALID; break; }
        line[strcspn(line, "\r\n")] = '\0';
        if (!*line || ap_path_list_contains(out, line)) continue;
        struct stat st;
        if (existing_only && (stat(line, &st) != 0 || !S_ISREG(st.st_mode))) continue;
        rc = ap_path_list_append(out, line);
        if (rc != AP_OK) break;
    }
    if (ferror(file) || (rc == AP_OK && !feof(file))) rc = AP_IO;
    free(line);
    fclose(file);
    return rc;
}

ap_result ap_history_load(ap_history_kind kind, ap_path_list *out) {
    if (!out || !history_name(kind)) return AP_INVALID;
    char path[4096];
    ap_result rc = ap_config_path(history_name(kind), path, sizeof(path));
    if (rc != AP_OK) return rc;
    ap_path_list list = {0};
    rc = read_history(path, &list, 1);
    if (rc != AP_OK) { ap_path_list_free(&list); return rc; }
    ap_path_list_free(out);
    *out = list;
    return AP_OK;
}

static ap_result write_history(FILE *file, const void *data) {
    const ap_path_list *list = data;
    for (size_t i = 0; i < list->count; ++i) fprintf(file, "%s\n", list->paths[i]);
    return ferror(file) ? AP_IO : AP_OK;
}

static ap_result update_history(ap_history_kind kind, const char *path, int *favorite) {
    if (!path || !*path || strlen(path) >= 4096 || strchr(path, '\n') || strchr(path, '\r')) return AP_INVALID;
    char dest[4096], lockpath[4096], dir[4096];
    ap_result rc = ap_config_path("", dir, sizeof(dir));
    if (rc != AP_OK || (rc = ap_mkdirs(dir)) != AP_OK) return rc;
    rc = ap_config_path(history_name(kind), dest, sizeof(dest));
    if (rc != AP_OK) return rc;
    rc = ap_config_path(kind == AP_FAVORITES ? ".favorites.lock" : ".recent.lock", lockpath, sizeof(lockpath));
    if (rc != AP_OK) return rc;
    int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) return AP_IO;
    while (flock(lock, LOCK_EX) != 0) {
        if (errno == EINTR) continue;
        close(lock); return AP_IO;
    }
    ap_path_list list = {0};
    rc = read_history(dest, &list, 0);
    if (rc != AP_OK) goto done;
    int present = ap_path_list_contains(&list, path);
    if (kind == AP_FAVORITES && !present) {
        struct stat st;
        if ((!is_image(path) && !is_video(path) && !ap_engine_is_project(path)) || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            rc = AP_NOT_FOUND;
            goto done;
        }
    }
    ap_path_list_remove(&list, path);
    if (kind == AP_RECENT || !present) {
        rc = ap_path_list_append(&list, path);
        if (rc != AP_OK) goto done;
    }
    if (kind == AP_RECENT) {
        char *first = list.paths[list.count - 1];
        memmove(list.paths + 1, list.paths, (list.count - 1) * sizeof(char *));
        list.paths[0] = first;
        while (list.count > 50) free(list.paths[--list.count]);
    }
    rc = ap_write_atomic(dest, write_history, &list);
    if (rc == AP_OK && favorite) *favorite = !present;
done:
    ap_path_list_free(&list);
    close(lock);
    return rc;
}

ap_result ap_favorite_toggle(const char *path, int *is_favorite) {
    if (ap_engine_is_project(path)) {
        ap_engine_project project;
        ap_result rc = ap_engine_read(path, &project);
        if (rc != AP_OK) return rc;
        return update_history(AP_FAVORITES, project.manifest, is_favorite);
    }
    return update_history(AP_FAVORITES, path, is_favorite);
}

ap_result ap_recent_add(const char *path) {
    return update_history(AP_RECENT, path, NULL);
}
