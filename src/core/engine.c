#define _GNU_SOURCE
#include "archpaper/engine.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"

#include <json-c/json.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int join(char *out, size_t size, const char *base, const char *name) {
    int n = snprintf(out, size, "%s/%s", base, name);
    return n >= 0 && (size_t)n < size;
}

int ap_engine_is_project(const char *path) {
    if (!path) return 0;
    const char *name = strrchr(path, '/');
    if (!strcmp(name ? name + 1 : path, "project.json")) return 1;
    char manifest[4096];
    struct stat st;
    return join(manifest, sizeof(manifest), path, "project.json") &&
        stat(manifest, &st) == 0 && S_ISREG(st.st_mode);
}

/* Bound local metadata reads; never block on a FIFO posing as a manifest. */
static char *read_text(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > 4 * 1024 * 1024) {
        close(fd); return NULL;
    }
    size_t size = (size_t)st.st_size, used = 0;
    char *text = calloc(size + 1, 1);
    if (!text) { close(fd); return NULL; }
    while (used < size) {
        ssize_t n = read(fd, text + used, size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { free(text); close(fd); return NULL; }
        used += (size_t)n;
    }
    close(fd);
    if (memchr(text, 0, size)) { free(text); return NULL; }
    return text;
}

static json_object *parse_json(const char *text) {
    json_tokener *tok = json_tokener_new();
    if (!tok) return NULL;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    json_object *obj = json_tokener_parse_ex(tok, text, (int)strlen(text) + 1);
    if (json_tokener_get_error(tok) != json_tokener_success) {
        json_object_put(obj); obj = NULL;
    }
    json_tokener_free(tok);
    return obj;
}

static const char *string_field(json_object *obj, const char *key) {
    json_object *value;
    if (!json_object_object_get_ex(obj, key, &value) || !json_object_is_type(value, json_type_string)) return NULL;
    const char *s = json_object_get_string(value);
    return strlen(s) == (size_t)json_object_get_string_len(value) && !strchr(s, '\n') && !strchr(s, '\r') ? s : NULL;
}

/* Only project-local files can be used as media/preview. Normalize Windows
 * separators, then resolve symlinks and enforce the canonical directory. */
static int local_file(const char *base, const char *name, char *out, size_t size) {
    char relative[4096], path[4096];
    if (!name || !*name || *name == '/' || strchr(name, ':') ||
        ap_copy_string(relative, sizeof(relative), name) != AP_OK) return 0;
    for (char *p = relative; *p; ++p) if (*p == '\\') *p = '/';
    if (*relative == '/' || !join(path, sizeof(path), base, relative)) return 0;
    char *absolute = realpath(path, NULL);
    if (!absolute) return 0;
    struct stat st;
    size_t n = strlen(base);
    int valid = !strncmp(base, absolute, n) && absolute[n] == '/' &&
        stat(absolute, &st) == 0 && S_ISREG(st.st_mode) &&
        ap_copy_string(out, size, absolute) == AP_OK;
    free(absolute);
    return valid;
}

ap_result ap_engine_read(const char *path, ap_engine_project *out) {
    if (!path || !out) return AP_INVALID;
    *out = (ap_engine_project){0};
    char *expanded = expand_path(path);
    char *absolute = expanded ? realpath(expanded, NULL) : NULL;
    free(expanded);
    if (!absolute) return AP_NOT_FOUND;
    struct stat st;
    ap_result rc = AP_INVALID;
    if (stat(absolute, &st) != 0) goto done;
    if (S_ISDIR(st.st_mode)) {
        if (!join(out->manifest, sizeof(out->manifest), absolute, "project.json")) goto done;
    } else {
        const char *name = strrchr(absolute, '/');
        if (!name || strcmp(name + 1, "project.json") ||
            ap_copy_string(out->manifest, sizeof(out->manifest), absolute) != AP_OK) goto done;
    }
    if (ap_copy_string(out->directory, sizeof(out->directory), out->manifest) != AP_OK) goto done;
    *strrchr(out->directory, '/') = 0;
    char *text = read_text(out->manifest);
    if (!text) { rc = AP_NOT_FOUND; goto done; }
    /* Some editors emit a UTF-8 BOM. */
    const char *json = !strncmp(text, "\xef\xbb\xbf", 3) ? text + 3 : text;
    json_object *obj = parse_json(json);
    free(text);
    if (!obj || !json_object_is_type(obj, json_type_object)) { json_object_put(obj); goto done; }
    const char *type = string_field(obj, "type"), *file = string_field(obj, "file");
    const char *title = string_field(obj, "title"), *preview = string_field(obj, "preview");
    const char *name = strrchr(out->directory, '/');
    if (!type || !file || !*file || ap_copy_string(out->type_name, sizeof(out->type_name), type) != AP_OK ||
        ap_copy_string(out->title, sizeof(out->title), title && *title ? title : name ? name + 1 : out->directory) != AP_OK) {
        json_object_put(obj); goto done;
    }
    if (!strcasecmp(type, "video") && local_file(out->directory, file, out->file, sizeof(out->file)) && is_video(out->file))
        out->type = AP_ENGINE_VIDEO;
    else if (!strcasecmp(type, "scene")) {
        /* scene.json is commonly packed inside scene.pkg. */
        if (local_file(out->directory, file, out->file, sizeof(out->file)) ||
            local_file(out->directory, "scene.pkg", out->file, sizeof(out->file))) out->type = AP_ENGINE_SCENE;
    }
    if (local_file(out->directory, preview, out->preview, sizeof(out->preview)) && !is_image(out->preview)) out->preview[0] = 0;
    json_object_put(obj);
    rc = AP_OK;
done:
    free(absolute);
    return rc;
}

static ap_result add_directory(ap_path_list *list, const char *path) {
    char *absolute = realpath(path, NULL);
    if (!absolute) return AP_OK;
    ap_result rc = AP_OK;
    if (is_dir(absolute) && !ap_path_list_contains(list, absolute)) rc = ap_path_list_append(list, absolute);
    free(absolute);
    return rc;
}

/* Valve KeyValues tokenizer: quoted strings with escapes, braces and comments.
 * Modern libraryfolders.vdf uses a "path" field; older versions use numeric
 * keys with absolute path values. Only those pairs are considered. */
static int vdf_token(const char **cursor, char *out, size_t size) {
    const char *p = *cursor;
    for (;;) {
        while (isspace((unsigned char)*p)) ++p;
        if (p[0] != '/' || p[1] != '/') break;
        while (*p && *p != '\n') ++p;
    }
    if (!*p) { *cursor = p; return 0; }
    size_t n = 0;
    if (*p == '{' || *p == '}') out[n++] = *p++;
    else if (*p == '"') {
        ++p;
        while (*p && *p != '"') {
            if (*p == '\\' && (p[1] == '\\' || p[1] == '"')) ++p;
            if (n + 1 >= size) return -1;
            out[n++] = *p++;
        }
        if (*p++ != '"') return -1;
    } else return -1;
    out[n] = 0; *cursor = p; return 1;
}

static ap_result steam_libraries(ap_path_list *list) {
    const char *roots[] = {"~/.steam/steam", "~/.steam/root", "~/.local/share/Steam",
        "~/.var/app/com.valvesoftware.Steam/.local/share/Steam", "~/snap/steam/common/.local/share/Steam", NULL};
    ap_result rc = AP_OK;
    for (size_t i = 0; roots[i] && rc == AP_OK; ++i) {
        char *root = expand_path(roots[i]);
        if (!root) return AP_NOMEM;
        rc = add_directory(list, root);
        char path[4096];
        char *text = join(path, sizeof(path), root, "steamapps/libraryfolders.vdf") ? read_text(path) : NULL;
        free(root);
        if (!text) continue;
        const char *cursor = text;
        char key[4096] = "", token[4096];
        while (rc == AP_OK && vdf_token(&cursor, token, sizeof(token)) > 0) {
            if (!strcmp(token, "{") || !strcmp(token, "}")) { key[0] = 0; continue; }
            if (!key[0]) { strcpy(key, token); continue; }
            if ((!strcmp(key, "path") || strspn(key, "0123456789") == strlen(key)) && token[0] == '/')
                rc = add_directory(list, token);
            key[0] = 0;
        }
        free(text);
    }
    return rc;
}

ap_result ap_engine_discover(ap_path_list *out) {
    if (!out) return AP_INVALID;
    ap_path_list libraries = {0}, folders = {0};
    ap_result rc = steam_libraries(&libraries);
    for (size_t i = 0; i < libraries.count && rc == AP_OK; ++i) {
        char path[4096];
        if (join(path, sizeof(path), libraries.paths[i], "steamapps/workshop/content/431960")) rc = add_directory(&folders, path);
    }
    ap_path_list_free(&libraries);
    if (rc == AP_OK) { ap_path_list_free(out); *out = folders; }
    else ap_path_list_free(&folders);
    return rc;
}

ap_result ap_engine_assets(const config_t *cfg, char *out, size_t size) {
    if (cfg->engine_assets[0]) {
        char *expanded = expand_path(cfg->engine_assets);
        char *absolute = expanded ? realpath(expanded, NULL) : NULL;
        free(expanded);
        ap_result rc = absolute && is_dir(absolute) ? ap_copy_string(out, size, absolute) : AP_ASSETS_MISSING;
        free(absolute); return rc;
    }
    ap_path_list libraries = {0};
    ap_result rc = steam_libraries(&libraries);
    if (rc != AP_OK) { ap_path_list_free(&libraries); return rc; }
    rc = AP_ASSETS_MISSING;
    for (size_t i = 0; i < libraries.count; ++i) {
        char path[4096];
        if (join(path, sizeof(path), libraries.paths[i], "steamapps/common/wallpaper_engine/assets") && is_dir(path)) {
            rc = ap_copy_string(out, size, path); break;
        }
    }
    ap_path_list_free(&libraries);
    return rc;
}

ap_result ap_engine_outputs(const config_t *cfg, ap_path_list *out) {
    if (cfg->engine_output[0]) return ap_path_list_append(out, cfg->engine_output);
    char text[65536];
    const char *args[] = {"hyprctl", "-j", "monitors", NULL};
    ap_result rc = ap_process_run(args, 2000, text, sizeof(text));
    if (rc != AP_OK) return rc == AP_CANCELLED ? rc : AP_OUTPUT_MISSING;
    json_object *obj = parse_json(text);
    if (!obj || !json_object_is_type(obj, json_type_array)) { json_object_put(obj); return AP_OUTPUT_MISSING; }
    for (size_t i = 0; i < json_object_array_length(obj) && rc == AP_OK; ++i) {
        const char *name = string_field(json_object_array_get_idx(obj, i), "name");
        if (name && *name && !ap_path_list_contains(out, name)) rc = ap_path_list_append(out, name);
    }
    json_object_put(obj);
    return rc == AP_OK && !out->count ? AP_OUTPUT_MISSING : rc;
}

int ap_engine_parse_fps(const char *value, int *out) {
    if (!value || !*value || !out) return AP_INVALID;
    errno = 0;
    char *end;
    long n = strtol(value, &end, 10);
    if (errno || *end || n < 1 || n > 240) return AP_INVALID;
    *out = (int)n;
    return AP_OK;
}

static int engine_lock(void) {
    char path[4096];
    if (ap_runtime_path("engine.lock", path, sizeof(path)) != AP_OK) return -1;
    return open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
}

static ap_result engine_owner(int *pid) {
    int fd = engine_lock();
    if (fd < 0) return AP_IO;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    int rc = fcntl(fd, F_GETLK, &lock);
    close(fd);
    if (rc < 0) return AP_IO;
    *pid = lock.l_type == F_UNLCK ? 0 : lock.l_pid;
    return AP_OK;
}

ap_result ap_engine_stop(void) {
    int pid;
    ap_result rc = engine_owner(&pid);
    if (rc != AP_OK || !pid) return rc;
    if (kill(pid, SIGTERM) && errno != ESRCH) return AP_PROCESS;
    for (int i = 0; i < 150; ++i) {
        int current;
        usleep(20000);
        rc = engine_owner(&current);
        if (rc != AP_OK || current != pid) return rc;
    }
    return AP_TIMEOUT;
}

static volatile sig_atomic_t engine_running;
static void engine_signal(int sig) { (void)sig; engine_running = 0; }
static ap_result write_message(FILE *f, const void *text) { return fputs(text, f) < 0 ? AP_IO : AP_OK; }

int ap_engine_run(const char *const args[]) {
    int fd = engine_lock();
    if (fd < 0) return AP_IO;
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (fcntl(fd, F_SETLK, &lock)) { close(fd); return AP_BUSY; }
    engine_running = 1;
    struct sigaction action = {.sa_handler = engine_signal};
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    char output[8192], ready[4096], log[4096], pid[32];
    if (ap_runtime_path("engine.ready", ready, sizeof(ready)) != AP_OK ||
        ap_runtime_path("engine.log", log, sizeof(log)) != AP_OK) { close(fd); return AP_IO; }
    unlink(ready);
    ap_process process;
    ap_result rc = ap_process_start_logged(&process, args, output, sizeof(output));
    if (rc != AP_OK) { close(fd); return rc; }
    int ticks = 0;
    while ((rc = ap_process_poll(&process)) == AP_BUSY && engine_running) {
        if (++ticks == 25) {
            snprintf(pid, sizeof(pid), "%d", (int)getpid());
            if (ap_write_atomic(ready, write_message, pid) != AP_OK) break;
        }
        usleep(10000);
    }
    if (process.pid > 0) {
        kill(-process.pid, SIGTERM);
        for (int i = 0; i < 50 && ap_process_poll(&process) == AP_BUSY; ++i) usleep(10000);
        if (process.pid > 0) ap_process_cancel(&process);
    }
    ap_write_atomic(log, write_message, output);
    unlink(ready);
    close(fd);
    return rc;
}

ap_result ap_engine_start(const char *const args[]) {
    char executable[4096], ready[4096];
    ssize_t n = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (n < 0 || (size_t)n >= sizeof(executable) - 1) return AP_IO;
    executable[n] = 0;
    if (ap_runtime_path("engine.ready", ready, sizeof(ready)) != AP_OK) return AP_IO;
    unlink(ready);
    size_t count = 0;
    while (args[count]) ++count;
    const char **command = calloc(count + 3, sizeof(*command));
    if (!command) return AP_NOMEM;
    command[0] = executable; command[1] = "__engine-run";
    memcpy(command + 2, args, count * sizeof(*args));
    ap_result rc = ap_process_detach(command);
    free(command);
    if (rc != AP_OK) return rc;
    for (int i = 0; i < 150; ++i) {
        usleep(20000);
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; break; }
        int pid;
        rc = engine_owner(&pid);
        if (rc != AP_OK) break;
        char *text = read_text(ready);
        int ready_pid = text ? atoi(text) : 0;
        free(text);
        if (pid > 0 && pid == ready_pid) return AP_OK;
    }
    ap_engine_stop();
    return rc == AP_OK ? AP_PROCESS : rc;
}
