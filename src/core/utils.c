/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "archpaper/utils.h"

#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *get_home(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    return home ? home : ".";
}

char *expand_path(const char *path) {
    if (!path) return NULL;
    if (path[0] != '~') return strdup(path);

    const char *home = get_home();
    size_t len = strlen(home) + strlen(path);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    snprintf(out, len + 1, "%s%s", home, path + 1);
    return out;
}

int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

int is_dir(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *file_extension(const char *path) {
    if (!path) return "";
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path || strchr(dot, '/')) return "";
    return dot + 1;
}

static int has_extension(const char *path, const char *exts[]) {
    const char *ext = file_extension(path);
    size_t len = strlen(ext);
    for (int i = 0; exts[i]; i++) {
        size_t elen = strlen(exts[i]);
        if (len == elen && strcasecmp(ext, exts[i]) == 0)
            return 1;
    }
    return 0;
}

int is_image(const char *path) {
    static const char *exts[] = {
        "jpg", "jpeg", "png", "webp", "bmp", "tiff", "tif", "gif", NULL
    };
    return has_extension(path, exts);
}

int is_video(const char *path) {
    static const char *exts[] = {
        "mp4", "webm", "mkv", "mov", "avi", "ogv", NULL
    };
    return has_extension(path, exts);
}

int is_animated_image(const char *path) {
    static const char *exts[] = {
        "gif", "webp", NULL
    };
    return has_extension(path, exts);
}

char *random_image(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return NULL;

    char **files = NULL;
    size_t count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (is_image(full) || is_video(full)) {
            char **tmp = realloc(files, (count + 1) * sizeof(char *));
            if (!tmp) break;
            files = tmp;
            files[count++] = strdup(full);
        }
    }
    closedir(d);

    char *selected = NULL;
    if (count > 0) {
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        selected = strdup(files[rand() % count]);
    }

    for (size_t i = 0; i < count; i++) free(files[i]);
    free(files);
    return selected;
}
