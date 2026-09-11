#define _GNU_SOURCE
#include "archpaper/library.h"
#include "archpaper/utils.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

void ap_path_list_free(ap_path_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) free(list->paths[i]);
    free(list->paths);
    *list = (ap_path_list){0};
}

ap_result ap_path_list_append(ap_path_list *list, const char *path) {
    if (!list || !path) return AP_INVALID;
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2 / sizeof(char *)) return AP_NOMEM;
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        char **tmp = realloc(list->paths, capacity * sizeof(*tmp));
        if (!tmp) return AP_NOMEM;
        list->paths = tmp; list->capacity = capacity;
    }
    char *copy = strdup(path);
    if (!copy) return AP_NOMEM;
    list->paths[list->count++] = copy;
    return AP_OK;
}

int ap_path_list_contains(const ap_path_list *list, const char *path) {
    if (!list || !path) return 0;
    for (size_t i = 0; i < list->count; ++i) if (!strcmp(list->paths[i], path)) return 1;
    return 0;
}

void ap_path_list_remove(ap_path_list *list, const char *path) {
    if (!list || !path) return;
    char *deferred = NULL;
    for (size_t i = 0; i < list->count;) {
        if (strcmp(list->paths[i], path)) { ++i; continue; }
        /* The key may itself be one of the list-owned strings. */
        if (list->paths[i] == path) deferred = list->paths[i];
        else free(list->paths[i]);
        memmove(list->paths + i, list->paths + i + 1, (list->count - i - 1) * sizeof(char *));
        --list->count;
    }
    free(deferred);
}

ap_result ap_random_index(size_t count, size_t *out) {
    if (!count || !out) return AP_INVALID;
    uint64_t n, threshold = (uint64_t)(-((uint64_t)count)) % count;
    do {
        size_t got = 0;
        while (got < sizeof(n)) {
            ssize_t r = getrandom((char *)&n + got, sizeof(n) - got, 0);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) return AP_IO;
            got += (size_t)r;
        }
    } while (n < threshold);
    *out = (size_t)(n % count);
    return AP_OK;
}

static int compare_paths(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static ap_result scan(const char *directory, ap_path_list *list, char **random) {
    if (!directory || !*directory) return AP_INVALID;
    char *expanded = expand_path(directory);
    if (!expanded) return AP_NOMEM;
    char *base = realpath(expanded, NULL);
    free(expanded);
    if (!base) return errno == ENOENT ? AP_NOT_FOUND : AP_IO;
    DIR *dir = opendir(base);
    if (!dir) { free(base); return AP_IO; }
    ap_result result = AP_OK;
    struct dirent *entry;
    size_t count = 0;
    while (1) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) { if (errno) result = AP_IO; break; }
        if (entry->d_name[0] == '.' || (!is_image(entry->d_name) && !is_video(entry->d_name))) continue;
        struct stat st;
        if (fstatat(dirfd(dir), entry->d_name, &st, 0) != 0 || !S_ISREG(st.st_mode)) continue;
        if (random) {
            size_t index;
            result = ap_random_index(++count, &index);
            if (result != AP_OK) break;
            if (index) continue;
        }
        size_t size = strlen(base) + strlen(entry->d_name) + 2;
        char *path = malloc(size);
        if (!path) { result = AP_NOMEM; break; }
        snprintf(path, size, "%s/%s", base, entry->d_name);
        if (random) { free(*random); *random = path; }
        else { result = ap_path_list_append(list, path); free(path); }
        if (result != AP_OK) break;
    }
    closedir(dir);
    free(base);
    return result;
}

ap_result ap_library_scan(const char *directory, ap_path_list *out) {
    if (!out) return AP_INVALID;
    ap_path_list list = {0};
    ap_result result = scan(directory, &list, NULL);
    if (result != AP_OK) { ap_path_list_free(&list); return result; }
    if (list.count) qsort(list.paths, list.count, sizeof(char *), compare_paths);
    ap_path_list_free(out);
    *out = list;
    return AP_OK;
}

ap_result ap_library_random(const char *directory, char **out) {
    if (!out) return AP_INVALID;
    *out = NULL;
    ap_result result = scan(directory, NULL, out);
    if (result != AP_OK) { free(*out); *out = NULL; }
    return result == AP_OK && !*out ? AP_NOT_FOUND : result;
}

int ap_library_matches(const char *path, const char *text) {
    if (!path) return 0;
    if (!text || !*text) return 1;
    const char *name = strrchr(path, '/');
    return strcasestr(name ? name + 1 : path, text) != NULL;
}
