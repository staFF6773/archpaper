#include "archpaper/utils.h"

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
    char *out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s%s", home, path + 1);
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

int is_image(const char *path) {
    if (!path) return 0;
    static const char *exts[] = {
        ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tiff", ".tif", ".gif", NULL
    };
    size_t len = strlen(path);
    for (int i = 0; exts[i]; i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strcasecmp(path + len - elen, exts[i]) == 0)
            return 1;
    }
    return 0;
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
        if (is_image(full)) {
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
