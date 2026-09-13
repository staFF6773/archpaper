#define _GNU_SOURCE
#include "archpaper/engine.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/wallpaper.h"

#include <json-c/json.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
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

static int read_u32(FILE *f, uint32_t *out) {
    unsigned char b[4];
    if (fread(b, 1, sizeof(b), f) != sizeof(b)) return 0;
    *out = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

/* Read only bounded text entries, never unpack textures or trust archive paths.
 * PKGV offsets are relative to the end of the index, not the file header. */
static char *packed_text(const char *path, const char *entry) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return NULL; }
    FILE *f = fdopen(fd, "rb");
    if (!f) { close(fd); return NULL; }
    uint32_t length, count, offset = 0, bytes = 0;
    char name[4096], *text = NULL;
    if (!read_u32(f, &length) || length < 4 || length >= sizeof(name) ||
        fread(name, 1, length, f) != length || memcmp(name, "PKGV", 4) ||
        !read_u32(f, &count) || count > 100000) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pos, size;
        if (!read_u32(f, &length) || length >= sizeof(name) ||
            fread(name, 1, length, f) != length || memchr(name, 0, length) ||
            !read_u32(f, &pos) || !read_u32(f, &size)) goto done;
        name[length] = 0;
        if (!strcmp(name, entry)) { offset = pos; bytes = size; }
    }
    off_t base = ftello(f);
    if (base < 0 || !bytes || bytes > 4 * 1024 * 1024 ||
        base > st.st_size || (uint64_t)offset + bytes > (uint64_t)(st.st_size - base) ||
        fseeko(f, base + offset, SEEK_SET)) goto done;
    text = calloc((size_t)bytes + 1, 1);
    if (text && (fread(text, 1, bytes, f) != bytes || memchr(text, 0, bytes))) { free(text); text = NULL; }
done:
    fclose(f);
    return text;
}

static ap_result write_json(FILE *f, const void *data) {
    return fputs(json_object_to_json_string_ext((json_object *)data, JSON_C_TO_STRING_PRETTY), f) < 0 ? AP_IO : AP_OK;
}

static ap_result write_message(FILE *f, const void *text) { return fputs(text, f) < 0 ? AP_IO : AP_OK; }

static char *project_text(const ap_engine_project *project, const char *entry) {
    char path[4096];
    if (local_file(project->directory, entry, path, sizeof(path))) return read_text(path);
    return local_file(project->directory, "scene.pkg", path, sizeof(path)) ? packed_text(path, entry) : NULL;
}

static void remove_overlay(const char *prepared) {
    struct stat st;
    if (lstat(prepared, &st)) return;
    /* Never recurse through links into the Workshop or official assets. */
    if (!S_ISDIR(st.st_mode)) { unlink(prepared); return; }
    DIR *dir = opendir(prepared);
    if (!dir) return;
    struct dirent *entry;
    char path[4096];
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (join(path, sizeof(path), prepared, entry->d_name)) remove_overlay(path);
    }
    closedir(dir);
    rmdir(prepared);
}

void ap_engine_cleanup(const ap_engine_project *project, const char *prepared) {
    if (!project || !prepared || !*prepared || !strcmp(prepared, project->directory)) return;
    remove_overlay(prepared);
}

/* Materialize just the directories we will write to, retaining links to their
 * other children. In particular, do not write through a linked zcompat tree. */
static ap_result overlay_directory(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT && mkdir(path, 0700) == 0 ? AP_OK : AP_IO;
    if (S_ISDIR(st.st_mode)) return AP_OK;
    if (!S_ISLNK(st.st_mode)) return AP_INVALID;
    char *source = realpath(path, NULL);
    if (!source) return AP_IO;
    DIR *dir = opendir(source);
    if (!dir) { free(source); return AP_IO; }
    ap_result rc = unlink(path) || mkdir(path, 0700) ? AP_IO : AP_OK;
    struct dirent *entry;
    while (rc == AP_OK && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char from[4096], to[4096];
        if (!join(from, sizeof(from), source, entry->d_name) ||
            !join(to, sizeof(to), path, entry->d_name) || symlink(from, to)) rc = AP_IO;
    }
    closedir(dir);
    free(source);
    return rc;
}

static ap_result overlay_text(const char *prepared, const char *relative, const char *text) {
    char path[4096];
    if (!join(path, sizeof(path), prepared, relative)) return AP_INVALID;
    for (char *p = path + strlen(prepared) + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        ap_result rc = overlay_directory(path);
        *p = '/';
        if (rc != AP_OK) return rc;
    }
    return ap_write_atomic(path, write_message, text);
}

static ap_result replace_once(char **text, const char *from, const char *to) {
    char *match = strstr(*text, from);
    if (!match) return AP_OK;
    size_t prefix = (size_t)(match - *text), old = strlen(from), replacement = strlen(to);
    char *fixed = malloc(strlen(*text) - old + replacement + 1);
    if (!fixed) return AP_NOMEM;
    memcpy(fixed, *text, prefix);
    memcpy(fixed + prefix, to, replacement);
    strcpy(fixed + prefix + replacement, match + old);
    free(*text);
    *text = fixed;
    return AP_OK;
}

/* These two Workshop shaders contain a stray #endif tolerated by the Windows
 * compiler. Repair only the known source variants, not arbitrary shader code. */
static int remove_stray_endif(char *text) {
    int depth = 0, changed = 0;
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n'), *p = line;
        if (!end) end = line + strlen(line);
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p < end && *p++ == '#') {
            while (p < end && isspace((unsigned char)*p)) ++p;
            char word[16]; size_t n = 0;
            while (p < end && isalpha((unsigned char)*p) && n + 1 < sizeof(word)) word[n++] = *p++;
            word[n] = 0;
            if (!strcmp(word, "if") || !strcmp(word, "ifdef") || !strcmp(word, "ifndef")) ++depth;
            else if (!strcmp(word, "endif")) {
                if (depth) --depth;
                else { memset(line, ' ', (size_t)(end - line)); changed = 1; }
            }
        }
        line = *end ? end + 1 : end;
    }
    return changed;
}

static json_object *property_value(json_object *properties, const char *name) {
    json_object *property, *value;
    return json_object_object_get_ex(properties, name, &property) &&
        json_object_object_get_ex(property, "value", &value) ? value : NULL;
}

static int property_integer(json_object *properties, const char *name, int fallback, int max) {
    json_object *value = property_value(properties, name);
    if (json_object_is_type(value, json_type_int)) {
        int64_t n = json_object_get_int64(value);
        return n >= 0 && n <= max ? (int)n : fallback;
    }
    if (!json_object_is_type(value, json_type_string)) return fallback;
    const char *text = json_object_get_string(value);
    if (!*text || strlen(text) != (size_t)json_object_get_string_len(value)) return fallback;
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    return !errno && !*end && n >= 0 && n <= max ? (int)n : fallback;
}

/* This project starts five 4K video decoders before its visibility script runs.
 * Keep only the selected variant as an image; hidden video images still allocate
 * decoder surfaces in linux-wallpaperengine. Retain IDs/parents as empty groups
 * so the rest of the scene can keep its original hierarchy. */
static int prepare_elaina(json_object *manifest, json_object *scene) {
    const char *id = string_field(manifest, "workshopid");
    if (!id || strcmp(id, "3470764447")) return 0;
    json_object *objects, *general, *properties;
    if (!json_object_object_get_ex(scene, "objects", &objects) || !json_object_is_type(objects, json_type_array) ||
        !json_object_object_get_ex(manifest, "general", &general) ||
        !json_object_object_get_ex(general, "properties", &properties)) return 0;
    const char *names[] = {"morning", "day", "dusk", "night", "mddn"};
    const int ids[] = {147, 144, 142, 138, 221};
    json_object *variants[5] = {0}, *controller = NULL;
    for (size_t i = 0; i < json_object_array_length(objects); ++i) {
        json_object *object = json_object_array_get_idx(objects, i), *object_id, *visible, *script;
        if (!json_object_object_get_ex(object, "id", &object_id) || !json_object_is_type(object_id, json_type_int)) continue;
        int n = json_object_get_int(object_id);
        const char *name = string_field(object, "name"), *image = string_field(object, "image");
        for (int j = 0; j < 5; ++j) {
            if (n != ids[j] || !name || strcmp(name, names[j]) || !image) continue;
            if (variants[j]) return 0;
            variants[j] = object;
        }
        if (n == 6852 && json_object_object_get_ex(object, "visible", &visible) &&
            json_object_object_get_ex(visible, "script", &script) && json_object_is_type(script, json_type_string)) {
            const char *source = json_object_get_string(script);
            if (strstr(source, "displayVideo") && strstr(source, "getVideoTexture")) controller = object;
        }
    }
    /* Do not partially rewrite a future/edited project with a different layout. */
    if (!controller) return 0;
    for (int i = 0; i < 5; ++i) if (!variants[i]) return 0;
    int selected = property_integer(properties, "display", 1, 4);
    json_object *timevarying = property_value(properties, "timevarying");
    if (!json_object_is_type(timevarying, json_type_boolean) || json_object_get_boolean(timevarying)) {
        const char *hours[] = {"morningtime", "daytime", "dusktime", "nighttime"};
        const int defaults[] = {6, 9, 17, 20};
        int starts[4];
        for (int i = 0; i < 4; ++i) starts[i] = property_integer(properties, hours[i], defaults[i], 23);
        if (!(starts[0] < starts[1] && starts[1] < starts[2] && starts[2] < starts[3]))
            memcpy(starts, defaults, sizeof(starts));
        time_t now = time(NULL);
        struct tm local;
        if (!localtime_r(&now, &local)) return 0;
        selected = 3;
        for (int i = 0; i < 3; ++i)
            if (local.tm_hour >= starts[i] && local.tm_hour < starts[i + 1]) selected = i;
    }
    for (int i = 0; i < 5; ++i) {
        json_object_object_add(variants[i], "visible", json_object_new_boolean(i == selected));
        if (i == selected) continue;
        json_object_object_del(variants[i], "image");
        json_object_object_del(variants[i], "effects");
        json_object_object_add(variants[i], "solid", json_object_new_boolean(1));
    }
    /* The gradient variant's parent has its own visibility script and user
     * condition. Resolve that too when a manual gradient choice is requested. */
    if (selected == 4) {
        for (size_t i = 0; i < json_object_array_length(objects); ++i) {
            json_object *object = json_object_array_get_idx(objects, i), *object_id;
            const char *name = string_field(object, "name");
            if (name && !strcmp(name, "myLayer") && json_object_object_get_ex(object, "id", &object_id) &&
                json_object_get_int(object_id) == 130)
                json_object_object_add(object, "visible", json_object_new_boolean(1));
        }
    }
    /* The old controller calls getVideoTexture() on all five layers. Selection
     * is now fixed until the next apply; keep its post-processing layer active. */
    json_object_object_add(controller, "visible", json_object_new_boolean(1));
    json_object_object_add(manifest, "archpaper_video_variant", json_object_new_string(names[selected]));
    return 1;
}

ap_result ap_engine_prepare(const ap_engine_project *project, char *out, size_t size) {
    if (!project || !out || !size) return AP_INVALID;
    ap_result rc = ap_copy_string(out, size, project->directory);
    if (rc != AP_OK || project->type != AP_ENGINE_SCENE) return rc;
    char *text = read_text(project->manifest);
    if (!text) return AP_IO;
    json_object *manifest = parse_json(!strncmp(text, "\xef\xbb\xbf", 3) ? text + 3 : text);
    free(text);
    if (!manifest) return AP_INVALID;
    const char *file = string_field(manifest, "file");
    char path[4096];
    text = file ? project_text(project, file) : NULL;
    json_object *scene = text ? parse_json(text) : NULL;
    free(text);
    json_object *objects;
    int changed = prepare_elaina(manifest, scene);
    const char *shader_names[] = {
        "2973943998/effects/iris_movement__.vert",
        "3082978660/effects/Simple_Audio_Bars.vert",
        "3082978660/effects/Simple_Audio_Bars.frag"
    };
    const char *shader_targets[] = {
        "zcompat/scene/shaders/2973943998/iris_movement__.vert",
        "zcompat/scene/shaders/3082978660/Simple_Audio_Bars.vert",
        "zcompat/scene/shaders/3082978660/Simple_Audio_Bars.frag"
    };
    char *shaders[3] = {NULL, NULL, NULL};
    const size_t shader_count = sizeof(shaders) / sizeof(shaders[0]);
    if (scene && json_object_object_get_ex(scene, "objects", &objects) && json_object_is_type(objects, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(objects); ++i) {
            json_object *object = json_object_array_get_idx(objects, i), *value;
            if (!json_object_object_get_ex(object, "text", &value)) continue;
            json_object *script;
            if (json_object_object_get_ex(value, "script", &script) && json_object_is_type(script, json_type_string)) {
                const char *source = json_object_get_string(script);
                /* The 12-hour clock uses an optional placeholder name even
                 * though it normally displays only the time. */
                const char *expr = "newString.replace('$', engine.userProperties.name)";
                if (strlen(source) == (size_t)json_object_get_string_len(script) &&
                    strstr(source, "3006161764") && strstr(source, expr)) {
                    char *fixed = strdup(source);
                    rc = fixed ? replace_once(&fixed, expr,
                        "newString.replace('$', (engine.userProperties || {}).name || '')") : AP_NOMEM;
                    if (rc == AP_OK) {
                        json_object_object_add(value, "script", json_object_new_string(fixed));
                        changed = 1;
                    }
                    free(fixed);
                    if (rc != AP_OK) goto done;
                }
            }
            const char *padding = string_field(object, "padding");
            if (!padding) continue;
            /* The Linux text renderer accepts only a scalar margin. Convert
             * equal, nonnegative integral components without changing layout. */
            double x, y;
            char extra;
            if (sscanf(padding, " %lf %lf %c", &x, &y, &extra) != 2 ||
                !isfinite(x) || x < 0 || x > INT_MAX || x != y || x != (int)x) continue;
            json_object_object_add(object, "padding", json_object_new_int((int)x));
            changed = 1;
        }
    }
    int shader_changed = 0;
    for (size_t i = 0; i < shader_count; ++i) {
        /* Respect explicit project compatibility overrides. */
        char *override = project_text(project, shader_targets[i]);
        if (override) { free(override); continue; }
        if (!join(path, sizeof(path), "shaders/workshop", shader_names[i])) { rc = AP_INVALID; goto done; }
        char *source = project_text(project, path);
        if (!source) continue;
        int fixed = 0;
        if (i == 0 && strstr(source, "vec4 transformedCursorPosition") &&
            strstr(source, "vec2 da = transformedCursorPosition * g_CursorScale")) {
            rc = replace_once(&source, "vec2 da = transformedCursorPosition * g_CursorScale",
                "vec2 da = transformedCursorPosition.xy * g_CursorScale");
            if (rc != AP_OK) { free(source); goto done; }
            fixed = 1;
        }
        if ((i == 0 && strstr(source, "g_CursorScaleLimit")) ||
            (i == 1 && strstr(source, "i_DCorrectingFactor") && strstr(source, "#if DEFORMITY")))
            fixed |= remove_stray_endif(source);
        /* Fragment inputs are read-only in GLSL. This source rotates and
         * remaps its UVs in main(), so work on a local copy instead. */
        char *main = strstr(source, "void main() {");
        if (i == 2 && main && strstr(source, "varying vec2 v_TexCoord;") &&
            strstr(main, "v_TexCoord = v_TexCoord.yx;")) {
            char *body = strdup(main);
            if (!body) { free(source); rc = AP_NOMEM; goto done; }
            while (rc == AP_OK && strstr(body, "v_TexCoord"))
                rc = replace_once(&body, "v_TexCoord", "ap_TexCoord");
            if (rc == AP_OK) rc = replace_once(&body, "void main() {",
                "void main() {\n\tvec2 ap_TexCoord = v_TexCoord;");
            if (rc == AP_OK) rc = replace_once(&source, main, body);
            free(body);
            if (rc != AP_OK) { free(source); goto done; }
            fixed = 1;
        }
        if (fixed) { shaders[i] = source; shader_changed = 1; }
        else free(source);
    }
    if (!changed && !shader_changed) goto done;
    char prepared[4096];
    rc = ap_runtime_path("engine-project-XXXXXX", prepared, sizeof(prepared));
    if (rc != AP_OK) goto done;
    if (!mkdtemp(prepared)) { rc = AP_IO; goto done; }
    DIR *dir = opendir(project->directory);
    if (!dir) { rc = AP_IO; goto cleanup; }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, "project.json")) continue;
        char source[4096];
        if (!join(path, sizeof(path), prepared, entry->d_name) ||
            !join(source, sizeof(source), project->directory, entry->d_name) || symlink(source, path)) { rc = AP_IO; break; }
    }
    closedir(dir);
    /* A unique filename avoids package-vs-directory lookup precedence. */
    char scene_path[4096];
    if (rc == AP_OK && changed && !join(scene_path, sizeof(scene_path), prepared, ".archpaper-scene-XXXXXX")) rc = AP_INVALID;
    if (rc == AP_OK && changed) {
        int fd = mkstemp(scene_path);
        if (fd < 0) rc = AP_IO;
        else { close(fd); rc = ap_write_atomic(scene_path, write_json, scene); }
    }
    if (rc == AP_OK) {
        if (changed) json_object_object_add(manifest, "file", json_object_new_string(strrchr(scene_path, '/') + 1));
        if (!join(path, sizeof(path), prepared, "project.json")) rc = AP_INVALID;
        else rc = ap_write_atomic(path, write_json, manifest);
    }
    for (size_t i = 0; rc == AP_OK && i < shader_count; ++i)
        if (shaders[i]) rc = overlay_text(prepared, shader_targets[i], shaders[i]);
    if (rc == AP_OK) rc = ap_copy_string(out, size, prepared);
cleanup:
    if (rc != AP_OK) ap_engine_cleanup(project, prepared);
done:
    for (size_t i = 0; i < shader_count; ++i) free(shaders[i]);
    json_object_put(scene);
    json_object_put(manifest);
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

ap_result ap_engine_status(int *pid) {
    return pid ? engine_owner(pid) : AP_INVALID;
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

ap_result ap_engine_diagnostic(char *out, size_t size) {
    if (!out || !size) return AP_INVALID;
    out[0] = 0;
    char path[4096];
    ap_result rc = ap_runtime_path("engine.log", path, sizeof(path));
    if (rc != AP_OK) return rc;
    char *text = read_text(path);
    if (!text) return AP_NOT_FOUND;
    snprintf(out, size, "%s", text);
    free(text);
    return AP_OK;
}

static long long engine_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

struct render_watch {
    char line[512];
    size_t used;
    char failure[512];
};

static void check_render_line(struct render_watch *watch) {
    const char *line = watch->line;
    /* DBus/GLFW notices, optional script errors and GLSL warnings are not
     * evidence of a missing wallpaper. These messages mean a render layer
     * was discarded, compilation failed, or GPU allocations failed. */
    if (!strncmp(line, "Failed to setup object ", 23) ||
        !strncmp(line, "GLSL vertex unit parsing Failed:", 32) ||
        !strncmp(line, "GLSL fragment unit parsing Failed:", 34) ||
        strstr(line, "CUDA_ERROR_OUT_OF_MEMORY") || strstr(line, "GL_OUT_OF_MEMORY"))
        snprintf(watch->failure, sizeof(watch->failure), "%s", line);
}

static void watch_render_output(const char *data, size_t size, void *context) {
    struct render_watch *watch = context;
    if (watch->failure[0]) return;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n' || watch->used == sizeof(watch->line) - 1) {
            check_render_line(watch);
            if (watch->failure[0]) return;
            if (data[i] == '\n') watch->used = 0;
            else {
                /* Retain overlap for an error marker split across chunks. */
                memmove(watch->line, watch->line + 256, watch->used - 256);
                watch->used -= 256;
            }
        }
        if (data[i] != '\n') watch->line[watch->used++] = data[i];
        watch->line[watch->used] = 0;
    }
    check_render_line(watch);
}

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
    char output[8192] = "", ready[4096], log[4096], pid[32];
    if (ap_runtime_path("engine.ready", ready, sizeof(ready)) != AP_OK ||
        ap_runtime_path("engine.log", log, sizeof(log)) != AP_OK) { close(fd); return AP_IO; }
    unlink(ready);
    config_t previous;
    int have_previous = config_load(&previous) == AP_OK;
    ap_engine_project project = {0};
    char prepared[4096] = "";
    size_t count = 0;
    while (args[count]) ++count;
    const char **command = calloc(count + 1, sizeof(*command));
    ap_result rc = command ? AP_OK : AP_NOMEM;
    if (command) memcpy(command, args, count * sizeof(*args));
    for (size_t i = 0; command && i + 1 < count; ++i) {
        if (strcmp(args[i], "--bg")) continue;
        if (!prepared[0]) {
            rc = ap_engine_read(args[i + 1], &project);
            if (rc == AP_OK) rc = ap_engine_prepare(&project, prepared, sizeof(prepared));
        }
        if (rc != AP_OK) break;
        command[++i] = prepared;
    }
    ap_process process;
    if (rc == AP_OK) rc = ap_process_start_logged(&process, command, output, sizeof(output));
    free(command);
    if (rc != AP_OK) {
        ap_write_atomic(log, write_message, ap_error_string(rc));
        ap_engine_cleanup(&project, prepared);
        close(fd); return rc;
    }
    struct render_watch watch = {0};
    process.output_observer = watch_render_output;
    process.output_context = &watch;
    int announced = 0;
    size_t logged = 0;
    long long started = engine_time(), updated = started;
    while ((rc = ap_process_poll(&process)) == AP_BUSY && engine_running) {
        if (watch.failure[0]) { rc = AP_PROCESS; break; }
        long long now = engine_time();
        if (!announced && now - started >= 1500) {
            snprintf(pid, sizeof(pid), "%d", (int)getpid());
            if (ap_write_atomic(ready, write_message, pid) != AP_OK) { rc = AP_IO; break; }
            announced = 1;
        }
        if (now - updated >= 500 && process.output_total != logged) {
            ap_write_atomic(log, write_message, output);
            logged = process.output_total;
            updated = now;
        }
        usleep(10000);
    }
    if (process.pid > 0) {
        kill(-process.pid, SIGTERM);
        for (int i = 0; i < 50 && ap_process_poll(&process) == AP_BUSY; ++i) usleep(10000);
        if (process.pid > 0) ap_process_cancel(&process);
    }
    char diagnostic[8192];
    if (watch.failure[0]) {
        snprintf(diagnostic, sizeof(diagnostic), "Scene rendering failed: %s\n\n%.*s",
            watch.failure, 7500, output);
        rc = AP_PROCESS;
    } else snprintf(diagnostic, sizeof(diagnostic), "%s", output);
    ap_write_atomic(log, write_message, diagnostic);
    unlink(ready);
    ap_engine_cleanup(&project, prepared);
    close(fd);
    /* Intentional switches/clear send SIGTERM. Only unexpected exits recover,
     * and the shared transaction verifies nobody has selected a newer file. */
    if (announced && engine_running && have_previous && project.manifest[0])
        ap_wallpaper_recover(project.manifest, &previous, diagnostic);
    return rc;
}

ap_result ap_engine_start(const char *const args[]) {
    char executable[4096], ready[4096], log[4096], failed[4096];
    ssize_t n = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (n < 0 || (size_t)n >= sizeof(executable) - 1) return AP_IO;
    executable[n] = 0;
    if (ap_runtime_path("engine.ready", ready, sizeof(ready)) != AP_OK) return AP_IO;
    if (ap_runtime_path("engine.log", log, sizeof(log)) != AP_OK ||
        ap_runtime_path("engine.failed", failed, sizeof(failed)) != AP_OK) return AP_IO;
    unlink(ready);
    unlink(log);
    unlink(failed);
    size_t count = 0;
    while (args[count]) ++count;
    const char **command = calloc(count + 3, sizeof(*command));
    if (!command) return AP_NOMEM;
    command[0] = executable; command[1] = "__engine-run";
    memcpy(command + 2, args, count * sizeof(*args));
    ap_result rc = ap_process_detach(command);
    free(command);
    if (rc != AP_OK) return rc;
    int seen_owner = 0;
    for (int i = 0; i < 500; ++i) {
        usleep(20000);
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; break; }
        int pid;
        rc = engine_owner(&pid);
        if (rc != AP_OK) break;
        if (pid) seen_owner = 1;
        else if (seen_owner || file_exists(log)) { rc = AP_PROCESS; break; }
        char *text = read_text(ready);
        int ready_pid = text ? atoi(text) : 0;
        free(text);
        if (pid > 0 && pid == ready_pid) return AP_OK;
    }
    ap_engine_stop();
    return rc == AP_OK ? AP_PROCESS : rc;
}
