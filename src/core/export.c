#define _GNU_SOURCE
#include "archpaper/export.h"
#include "archpaper/engine.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "texture.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ENTRIES 100000u

static int join(char *out, size_t size, const char *base, const char *name) {
    int n = snprintf(out, size, "%s/%s", base, name);
    return n >= 0 && (size_t)n < size;
}

static int resource_name(char *name) {
    for (char *p = name; *p; ++p) {
        if (*p == '\\') *p = '/';
        if ((unsigned char)*p < 32 || *p == ':') return 0;
    }
    if (!*name || *name == '/') return 0;
    for (const char *p = name; *p;) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && !strncmp(p, "..", 2))) return 0;
        if (!end) return 1;
        p = end + 1;
    }
    return 0;
}

static int is_texture(const char *name) { return !strcasecmp(file_extension(name), "tex"); }
static int is_resource(const char *name) { return is_image(name) || is_video(name) || is_texture(name); }

static void stamp(ap_resource *r, const struct stat *st) {
    r->device = st->st_dev; r->inode = st->st_ino; r->source_size = st->st_size;
    r->modified = st->st_mtim.tv_sec; r->modified_ns = st->st_mtim.tv_nsec;
}

static int unchanged(const ap_resource *r, const struct stat *st) {
    return S_ISREG(st->st_mode) && r->device == (uint64_t)st->st_dev &&
        r->inode == (uint64_t)st->st_ino && r->source_size == (uint64_t)st->st_size &&
        r->modified == st->st_mtim.tv_sec && r->modified_ns == st->st_mtim.tv_nsec;
}

void ap_resource_list_free(ap_resource_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) { free(list->items[i].name); free(list->items[i].source); }
    free(list->items);
    *list = (ap_resource_list){0};
}

static ap_result append(ap_resource_list *list, const char *name, const char *source,
                        uint64_t offset, uint64_t bytes, int packed, const struct stat *st) {
    if (list->count >= MAX_ENTRIES) return AP_UNSUPPORTED;
    ap_resource r = {.offset = offset, .size = bytes, .packed = packed,
        .kind = is_texture(name) ? AP_RESOURCE_TEXTURE : is_video(name) ? AP_RESOURCE_VIDEO : AP_RESOURCE_IMAGE};
    r.name = strdup(name); r.source = strdup(source);
    if (!r.name || !r.source) { free(r.name); free(r.source); return AP_NOMEM; }
    stamp(&r, st);
    if (r.kind != AP_RESOURCE_TEXTURE) ap_copy_string(r.extension, sizeof(r.extension), file_extension(name));
    ap_resource *items = realloc(list->items, (list->count + 1) * sizeof(*items));
    if (!items) { free(r.name); free(r.source); return AP_NOMEM; }
    list->items = items; list->items[list->count++] = r;
    return AP_OK;
}

static FILE *open_regular(const char *path, struct stat *st) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    if (fstat(fd, st) || !S_ISREG(st->st_mode) || st->st_size < 0) { close(fd); return NULL; }
    FILE *f = fdopen(fd, "rb");
    if (!f) close(fd);
    return f;
}

static ap_result package_read(const char *path, const char *prefix, ap_resource_list *list) {
    struct stat st;
    FILE *f = open_regular(path, &st);
    if (!f) return AP_IO;
    ap_slice s = {f, st.st_size};
    uint32_t length, count;
    char name[4096], relative[4096];
    size_t first = list->count;
    ap_result rc = AP_INVALID;
    if (!ap_slice_u32(&s, &length) || length != 8 || !ap_slice_read(&s, name, length) ||
        memcmp(name, "PKGV", 4)) goto done;
    for (unsigned i = 4; i < 8; ++i) if (name[i] < '0' || name[i] > '9') goto done;
    if (!ap_slice_u32(&s, &count) || count > MAX_ENTRIES) goto done;
    /* Keep all bounds, even non-media entries, until the index end is known. */
    uint64_t end = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; goto done; }
        uint32_t offset, bytes;
        if (!ap_slice_u32(&s, &length) || !length || length >= sizeof(name) ||
            !ap_slice_read(&s, name, length) || memchr(name, 0, length) ||
            !ap_slice_u32(&s, &offset) || !ap_slice_u32(&s, &bytes)) goto done;
        name[length] = 0;
        if (!resource_name(name)) goto done;
        if ((uint64_t)offset + bytes > end) end = (uint64_t)offset + bytes;
        if (!is_resource(name)) continue;
        if (*prefix) {
            if (!join(relative, sizeof(relative), prefix, name)) goto done;
        } else if (ap_copy_string(relative, sizeof(relative), name) != AP_OK) goto done;
        ap_result added = append(list, relative, path, offset, bytes, 1, &st);
        if (added != AP_OK) { rc = added; goto done; }
    }
    if (end > s.left) goto done;
    uint64_t base = (uint64_t)st.st_size - s.left;
    for (size_t i = first; i < list->count; ++i) list->items[i].offset += base;
    rc = AP_OK;
done:
    fclose(f);
    return rc;
}

static ap_result scan(const char *root, const char *relative, unsigned depth,
                       size_t *visited, ap_resource_list *list) {
    if (depth > 32) return AP_UNSUPPORTED;
    char path[4096];
    if (*relative) { if (!join(path, sizeof(path), root, relative)) return AP_INVALID; }
    else if (ap_copy_string(path, sizeof(path), root) != AP_OK) return AP_INVALID;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return AP_IO;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return AP_IO; }
    ap_result rc = AP_OK;
    for (;;) {
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; break; }
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) rc = AP_IO; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (++*visited > MAX_ENTRIES) { rc = AP_UNSUPPORTED; break; }
        char name[4096], full[4096];
        if (*relative) {
            if (!join(name, sizeof(name), relative, entry->d_name)) { rc = AP_INVALID; break; }
        } else if (ap_copy_string(name, sizeof(name), entry->d_name) != AP_OK) { rc = AP_INVALID; break; }
        if (!resource_name(name) || !join(full, sizeof(full), root, name)) { rc = AP_INVALID; break; }
        struct stat st;
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) { rc = AP_IO; break; }
        if (S_ISDIR(st.st_mode)) rc = scan(root, name, depth + 1, visited, list);
        else if (S_ISREG(st.st_mode)) {
            if (is_resource(name)) rc = append(list, name, full, 0, st.st_size, 0, &st);
            else if (!strcasecmp(file_extension(name), "pkg")) rc = package_read(full, relative, list);
        }
        if (rc != AP_OK) break;
    }
    closedir(dir);
    return rc;
}

static int compare(const void *a, const void *b) {
    const ap_resource *x = a, *y = b;
    int n = strcmp(x->name, y->name);
    if (n) return n;
    if (x->packed != y->packed) return x->packed - y->packed; /* Loose overrides packed. */
    n = strcmp(x->source, y->source);
    if (n) return n;
    return x->offset < y->offset ? -1 : x->offset > y->offset;
}

ap_result ap_resources_read(const char *path, ap_resource_list *out) {
    if (!out) return AP_INVALID;
    *out = (ap_resource_list){0};
    ap_engine_project project;
    ap_result rc = ap_engine_read(path, &project);
    if (rc != AP_OK) return rc;
    ap_resource_list list = {0};
    size_t visited = 0;
    rc = scan(project.directory, "", 0, &visited, &list);
    if (rc != AP_OK) goto done;
    if (list.count > 1) qsort(list.items, list.count, sizeof(*list.items), compare);
    size_t used = 0;
    for (size_t i = 0; i < list.count; ++i) {
        ap_resource *r = &list.items[i];
        if (used && !strcmp(list.items[used - 1].name, r->name)) { free(r->name); free(r->source); continue; }
        list.items[used++] = *r;
    }
    list.count = used;
    for (size_t i = 0; i < list.count; ++i) {
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; goto done; }
        ap_resource *r = &list.items[i];
        r->preview = !r->packed && !strcmp(r->source, project.preview);
        if (r->kind != AP_RESOURCE_TEXTURE) continue;
        struct stat st;
        FILE *f = open_regular(r->source, &st);
        if (!f) { rc = AP_IO; goto done; }
        if (!unchanged(r, &st)) { fclose(f); rc = AP_BUSY; goto done; }
        if (fseeko(f, (off_t)r->offset, SEEK_SET)) { fclose(f); rc = AP_IO; goto done; }
        ap_slice s = {f, r->size};
        r->support = ap_texture_export(&s, NULL, r->extension);
        fclose(f);
    }
    *out = list;
    return AP_OK;
done:
    ap_resource_list_free(&list);
    return rc;
}

ap_result ap_resource_export(const ap_resource *r, const char *directory, char *out, size_t size) {
    if (!r || !r->source || !r->name || !directory || !out || !size) return AP_INVALID;
    *out = 0;
    if (r->support != AP_OK) return r->support;
    if (ap_process_cancel_requested()) return AP_CANCELLED;
    struct stat st;
    FILE *input = open_regular(r->source, &st);
    if (!input) return AP_IO;
    ap_result rc = AP_IO;
    if (!unchanged(r, &st)) { fclose(input); return AP_BUSY; }
    if (r->offset > r->source_size || r->size > r->source_size - r->offset ||
        fseeko(input, (off_t)r->offset, SEEK_SET)) { fclose(input); return AP_INVALID; }
    char *expanded = expand_path(directory), *dest = expanded ? realpath(expanded, NULL) : NULL;
    free(expanded);
    if (!dest) { fclose(input); return AP_NOT_FOUND; }
    char temp[4096], final[4096], stem[221];
    const char *base = strrchr(r->name, '/'); base = base ? base + 1 : r->name;
    const char *dot = strrchr(base, '.');
    size_t n = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    /* Leave room for extension and conflict suffix within NAME_MAX. */
    if (!n || n > 220 || !*r->extension || strspn(r->extension, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != strlen(r->extension)) {
        rc = AP_INVALID; goto done;
    }
    memcpy(stem, base, n); stem[n] = 0;
    if (!join(temp, sizeof(temp), dest, ".archpaper-export-XXXXXX")) { rc = AP_INVALID; goto done; }
    int fd = mkstemp(temp);
    if (fd < 0) goto done;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    FILE *output = fdopen(fd, "wb");
    if (!output) { close(fd); unlink(temp); goto done; }
    ap_slice s = {input, r->size};
    char extension[8] = "";
    rc = r->kind == AP_RESOURCE_TEXTURE ? ap_texture_export(&s, output, extension) : ap_slice_copy(&s, output);
    if (rc == AP_OK && r->kind == AP_RESOURCE_TEXTURE && strcmp(extension, r->extension)) rc = AP_BUSY;
    if (rc == AP_OK && (fflush(output) || fsync(fd))) rc = AP_IO;
    if (fclose(output) && rc == AP_OK) rc = AP_IO;
    if (rc == AP_OK && (fstat(fileno(input), &st) || !unchanged(r, &st))) rc = AP_BUSY;
    if (rc == AP_OK && ap_process_cancel_requested()) rc = AP_CANCELLED;
    if (rc == AP_OK) {
        rc = AP_BUSY;
        for (unsigned i = 0; i < MAX_ENTRIES; ++i) {
            char name[256];
            if (i) snprintf(name, sizeof(name), "%s (%u).%s", stem, i, r->extension);
            else snprintf(name, sizeof(name), "%s.%s", stem, r->extension);
            if (!join(final, sizeof(final), dest, name) || strlen(final) >= size) { rc = AP_INVALID; break; }
            if (ap_process_cancel_requested()) { rc = AP_CANCELLED; break; }
            /* Linux no-replace rename also works on destinations without hard links. */
            if (!renameat2(AT_FDCWD, temp, AT_FDCWD, final, RENAME_NOREPLACE)) {
                ap_copy_string(out, size, final); rc = AP_OK; break;
            }
            if (errno != EEXIST) { rc = AP_IO; break; }
        }
    }
    unlink(temp);
done:
    free(dest); fclose(input);
    return rc;
}
