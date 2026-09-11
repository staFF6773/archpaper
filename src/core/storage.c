#define _GNU_SOURCE
#include "archpaper/storage.h"
#include "archpaper/utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

ap_result ap_copy_string(char *out, size_t size, const char *value) {
    if (!out || !value || !size || strlen(value) >= size) return AP_INVALID;
    memmove(out, value, strlen(value) + 1);
    return AP_OK;
}

static ap_result data_path(const char *env, const char *fallback, const char *name,
                           char *out, size_t size) {
    if (!name || strchr(name, '/') || !out || !size) return AP_INVALID;
    const char *base = getenv(env);
    int n = base && base[0] == '/'
        ? snprintf(out, size, "%s/archpaper/%s", base, name)
        : snprintf(out, size, "%s/%s/archpaper/%s", get_home(), fallback, name);
    return n < 0 || (size_t)n >= size ? AP_INVALID : AP_OK;
}

ap_result ap_config_path(const char *name, char *out, size_t size) {
    return data_path("XDG_CONFIG_HOME", ".config", name, out, size);
}

ap_result ap_cache_path(const char *name, char *out, size_t size) {
    return data_path("XDG_CACHE_HOME", ".cache", name, out, size);
}

ap_result ap_mkdirs(const char *path) {
    if (!path || !*path) return AP_INVALID;
    char *copy = strdup(path);
    if (!copy) return AP_NOMEM;
    ap_result result = AP_OK;
    for (char *p = copy + 1; ; ++p) {
        if (*p != '/' && *p) continue;
        char c = *p;
        *p = '\0';
        if (mkdir(copy, 0700) != 0) {
            struct stat st;
            if (errno != EEXIST || stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) result = AP_IO;
        }
        *p = c;
        if (result != AP_OK || !c) break;
    }
    free(copy);
    return result;
}

ap_result ap_runtime_path(const char *name, char *out, size_t size) {
    if (!name || strchr(name, '/') || !out || !size) return AP_INVALID;
    char dir[4096];
    const char *base = getenv("XDG_RUNTIME_DIR");
    int n = base && base[0] == '/'
        ? snprintf(dir, sizeof(dir), "%s/archpaper", base)
        : snprintf(dir, sizeof(dir), "/tmp/archpaper-%lu", (unsigned long)getuid());
    if (n < 0 || (size_t)n >= sizeof(dir)) return AP_INVALID;
    if (ap_mkdirs(dir) != AP_OK) return AP_IO;
    struct stat st;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0077)) return AP_IO;
    n = snprintf(out, size, "%s/%s", dir, name);
    return n < 0 || (size_t)n >= size ? AP_INVALID : AP_OK;
}

ap_result ap_write_atomic(const char *path, ap_file_writer writer, const void *data) {
    if (!path || !*path || !writer) return AP_INVALID;
    char *tmp = malloc(strlen(path) + 12);
    char *parent = strdup(path);
    if (!tmp || !parent) { free(tmp); free(parent); return AP_NOMEM; }
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) *slash = '\0';
    else strcpy(parent, slash ? "/" : ".");
    ap_result rc = ap_mkdirs(parent);
    if (rc != AP_OK) goto done;
    sprintf(tmp, "%s.XXXXXX", path);
    int fd = mkstemp(tmp);
    if (fd < 0) { rc = AP_IO; goto done; }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    FILE *file = fdopen(fd, "w");
    if (!file) { close(fd); unlink(tmp); rc = AP_IO; goto done; }
    rc = writer(file, data);
    if (ferror(file) || fflush(file) != 0 || fsync(fd) != 0) rc = AP_IO;
    if (fclose(file) != 0) rc = AP_IO;
    if (rc == AP_OK && rename(tmp, path) != 0) rc = AP_IO;
    if (rc != AP_OK) unlink(tmp);
done:
    free(tmp);
    free(parent);
    return rc;
}
