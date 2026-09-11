#define _GNU_SOURCE
#include "archpaper/cache.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

ap_result ap_thumbnail_extract(const char *path, const char *output) {
    if (!path || !*path || !output || !*output || !strcmp(path, output)) return AP_INVALID;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return AP_NOT_FOUND;
    const char *fast[] = {"ffmpegthumbnailer", "-i", path, "-o", output, "-s", "320", NULL};
    ap_result rc = ap_process_run(fast, 8000, NULL, 0);
    if (rc == AP_CANCELLED) return rc;
    if (rc == AP_OK && stat(output, &st) == 0 && st.st_size > 0) return AP_OK;
    const char *fallback[] = {"ffmpeg", "-y", "-ss", "00:00:01", "-i", path,
        "-vf", "scale=320:-1", "-vframes", "1", "-f", "image2", "-c:v", "png", output, NULL};
    rc = ap_process_run(fallback, 15000, NULL, 0);
    if (rc == AP_OK && (stat(output, &st) != 0 || st.st_size == 0)) rc = AP_PROCESS;
    if (rc != AP_OK) unlink(output);
    return rc;
}

static void parse_resolution(const char *text, int *w, int *h) {
    *w = *h = 0;
    for (const char *p = text; *p; ++p) {
        if (!isdigit((unsigned char)*p)) continue;
        int x, y;
        if (sscanf(p, "%dx%d", &x, &y) == 2 && x >= 640 && y >= 480 && x <= 16384 && y <= 16384) {
            *w = x; *h = y; return;
        }
        while (isdigit((unsigned char)p[1])) ++p;
    }
    const char *width = strstr(text, "\"width\"");
    const char *height = width ? strstr(width, "\"height\"") : NULL;
    if (width && height && sscanf(width, "\"width\" : %d", w) == 1 &&
        sscanf(height, "\"height\" : %d", h) == 1 && *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384) return;
    *w = *h = 0;
}

static void target_resolution(const char *quality, int *w, int *h) {
    const char *const commands[][5] = {
        {"awww", "query", NULL}, {"wlr-randr", NULL},
        {"hyprctl", "monitors", NULL}, {"swaymsg", "-t", "get_outputs", NULL}
    };
    *w = *h = 0;
    char text[32768];
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (ap_process_run(commands[i], 1500, text, sizeof(text)) == AP_OK) {
            parse_resolution(text, w, h);
            if (*w && *h) break;
        }
    }
    if (!strcmp(quality, "low")) {
        if (!*w || !*h) { *w = 1280; *h = 720; }
        else if (*w > 1920 || *h > 1080) { *w = 1920; *h = 1080; }
    } else if (!*w || !*h) { *w = 1920; *h = 1080; }
}

struct entry { char *path; off_t size; time_t mtime; };
static int newest_first(const void *a, const void *b) {
    const struct entry *x = a, *y = b;
    return x->mtime < y->mtime ? 1 : x->mtime > y->mtime ? -1 : strcmp(x->path, y->path);
}

static void prune(const char *dir, const char *keep) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct entry *entries = NULL;
    size_t count = 0, capacity = 0;
    off_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char *path;
        if (asprintf(&path, "%s/%s", dir, ent->d_name) < 0) break;
        struct stat st;
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) { free(path); continue; }
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            struct entry *tmp = realloc(entries, next * sizeof(*entries));
            if (!tmp) { free(path); break; }
            entries = tmp; capacity = next;
        }
        entries[count++] = (struct entry){path, st.st_size, st.st_mtime};
        total += st.st_size;
    }
    closedir(d);
    if (count) qsort(entries, count, sizeof(*entries), newest_first);
    size_t remaining = count;
    for (size_t i = count; i > 0 && (remaining > 50 || total > 2LL * 1024 * 1024 * 1024); --i) {
        struct entry *e = &entries[i - 1];
        if (!strcmp(e->path, keep)) continue;
        if (unlink(e->path) == 0) { total -= e->size; --remaining; }
    }
    for (size_t i = 0; i < count; ++i) free(entries[i].path);
    free(entries);
}

ap_result ap_cache_prepare(const char *path, const char *quality, char *out, size_t size) {
    ap_result rc = ap_copy_string(out, size, path);
    if (rc != AP_OK) return rc;
    if (!quality) quality = "monitor";
    if (!strcmp(quality, "original") || (!is_video(path) && !is_animated_image(path)) ||
        !ap_process_available("ffmpeg")) return AP_OK;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return AP_NOT_FOUND;
    char dimensions[128];
    const char *probe[] = {"ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height", "-of", "csv=s=x:p=0", path, NULL};
    int w = 0, h = 0;
    if (ap_process_run(probe, 5000, dimensions, sizeof(dimensions)) != AP_OK ||
        sscanf(dimensions, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return AP_OK;
    int tw, th;
    target_resolution(quality, &tw, &th);
    if (w <= tw && h <= th) return AP_OK;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    hash ^= (uint64_t)st.st_mtime ^ (uint64_t)st.st_mtim.tv_nsec ^ (uint64_t)st.st_size;
    hash = (hash ^ (uint64_t)tw ^ ((uint64_t)th << 32)) * UINT64_C(1099511628211);
    const int video = is_video(path);
    const int webp = !strcasecmp(file_extension(path), "webp");
    const char *format = video ? "mp4" : webp ? "webp" : "gif";
    char dir[4096], target[4096], lockpath[4096], tmp[4096];
    if (ap_cache_path(video ? "videos" : "animated", dir, sizeof(dir)) != AP_OK || ap_mkdirs(dir) != AP_OK) return AP_OK;
    int n = snprintf(target, sizeof(target), "%s/%016llx.%s", dir, (unsigned long long)hash, format);
    if (n < 0 || (size_t)n >= sizeof(target)) return AP_INVALID;
    n = snprintf(lockpath, sizeof(lockpath), "%s/.lock", dir);
    if (n < 0 || (size_t)n >= sizeof(lockpath)) return AP_INVALID;
    int lock = open(lockpath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) return AP_OK;
    /* Another conversion must not freeze callers waiting on a global lock. */
    if (flock(lock, LOCK_EX | LOCK_NB) != 0) { close(lock); return AP_OK; }
    struct stat cached;
    if (stat(target, &cached) == 0 && S_ISREG(cached.st_mode) && cached.st_size > 0) {
        rc = ap_copy_string(out, size, target);
        close(lock); return rc;
    }
    n = snprintf(tmp, sizeof(tmp), "%s/.conversion-XXXXXX", dir);
    if (n < 0 || (size_t)n >= sizeof(tmp)) { close(lock); return AP_INVALID; }
    int fd = mkstemp(tmp);
    if (fd < 0) { close(lock); return AP_OK; }
    close(fd);
    char filter[512];
    snprintf(filter, sizeof(filter), "scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos", tw, th);
    if (!video && !webp)
        snprintf(filter, sizeof(filter), "fps=30,scale=%d:%d:force_original_aspect_ratio=decrease:flags=lanczos,"
                 "split[s0][s1];[s0]palettegen=max_colors=256[p];[s1][p]paletteuse=dither=bayer", tw, th);
    const char *video_args[] = {"ffmpeg", "-y", "-i", path, "-vf", filter,
        "-c:v", "libx264", "-preset", "medium", "-crf", "20", "-an", "-movflags", "+faststart", "-f", "mp4", tmp, NULL};
    const char *webp_args[] = {"ffmpeg", "-y", "-i", path, "-vf", filter,
        "-loop", "0", "-c:v", "libwebp", "-lossless", "0", "-qscale", "90", "-an", "-f", "webp", tmp, NULL};
    const char *gif_args[] = {"ffmpeg", "-y", "-i", path, "-vf", filter,
        "-loop", "0", "-c:v", "gif", "-an", "-f", "gif", tmp, NULL};
    if (ap_process_run(video ? video_args : webp ? webp_args : gif_args, 120000, NULL, 0) == AP_OK &&
        stat(tmp, &cached) == 0 && cached.st_size > 0 && rename(tmp, target) == 0) {
        rc = ap_copy_string(out, size, target);
        prune(dir, target);
    }
    unlink(tmp);
    close(lock);
    return rc;
}
