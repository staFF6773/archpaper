#define _GNU_SOURCE
#include "archpaper/export.h"
#include "archpaper/process.h"
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <lz4.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
static char root[] = "/tmp/archpaper-export-test-XXXXXX";
static char project[4096], destination[4096], path[8192], output[4096];

static void u32(FILE *f, uint32_t n) {
    unsigned char b[] = {n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >> 24) & 255};
    CHECK(fwrite(b, 1, 4, f) == 4);
}
static void write_file(const char *name, const void *data, size_t n) {
    FILE *f = fopen(name, "wb"); CHECK(f);
    CHECK(fwrite(data, 1, n, f) == n && fclose(f) == 0);
}
static void equal_file(const char *name, const void *data, size_t n) {
    FILE *f = fopen(name, "rb"); CHECK(f);
    unsigned char *bytes = malloc(n); CHECK(bytes);
    CHECK(fread(bytes, 1, n, f) == n && !memcmp(bytes, data, n) && fgetc(f) == EOF);
    free(bytes); fclose(f);
}
static size_t files(const char *dir) {
    DIR *d = opendir(dir); CHECK(d);
    size_t count = 0; struct dirent *e;
    while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) ++count;
    closedir(d); return count;
}
static ap_resource *lookup(ap_resource_list *list, const char *name) {
    for (size_t i = 0; i < list->count; ++i) if (!strcmp(list->items[i].name, name)) return &list->items[i];
    CHECK(0); return NULL;
}

static void texture(unsigned format, unsigned flags, unsigned version, uint32_t embedded,
                    unsigned width, unsigned height, unsigned iw, unsigned ih,
                    const unsigned char *bytes, unsigned count, int compressed) {
    snprintf(path, sizeof(path), "%s/image.tex", project);
    FILE *f = fopen(path, "wb"); CHECK(f);
    CHECK(fwrite("TEXV0005\0TEXI0001\0", 1, 18, f) == 18);
    u32(f, format); u32(f, flags); u32(f, width); u32(f, height); u32(f, iw); u32(f, ih); u32(f, 0);
    char tag[10]; snprintf(tag, sizeof(tag), "TEXB%04u", version);
    CHECK(fwrite(tag, 1, 9, f) == 9); u32(f, 1);
    if (version >= 3) u32(f, embedded);
    if (version == 4) u32(f, flags & 32 ? 1 : 0);
    u32(f, 1);
    if (version == 4 && (flags & 32)) { u32(f, 1); u32(f, 2); CHECK(fwrite("{}\0", 1, 3, f) == 3); u32(f, 1); }
    u32(f, width); u32(f, height);
    if (version >= 2) { u32(f, compressed); u32(f, count); }
    if (compressed) {
        char *packed = malloc(LZ4_compressBound((int)count)); CHECK(packed);
        int size = LZ4_compress_default((const char *)bytes, packed, (int)count, LZ4_compressBound((int)count)); CHECK(size > 0);
        u32(f, size); CHECK(fwrite(packed, 1, size, f) == (size_t)size); free(packed);
    } else { u32(f, count); CHECK(fwrite(bytes, 1, count, f) == count); }
    CHECK(fclose(f) == 0);
}
static void png_pixels(const char *file, unsigned width, unsigned height, const unsigned char *expected) {
    png_image image = {.version = PNG_IMAGE_VERSION};
    CHECK(png_image_begin_read_from_file(&image, file));
    CHECK(image.width == width && image.height == height);
    image.format = PNG_FORMAT_RGBA;
    void *pixels = malloc(PNG_IMAGE_SIZE(image)); CHECK(pixels);
    CHECK(png_image_finish_read(&image, NULL, pixels, 0, NULL));
    CHECK(!memcmp(pixels, expected, (size_t)width * height * 4));
    free(pixels); png_image_free(&image);
}
static void export_texture(unsigned width, unsigned height, const unsigned char *expected) {
    ap_resource_list list = {0}; CHECK(ap_resources_read(project, &list) == AP_OK);
    ap_resource *r = lookup(&list, "image.tex");
    CHECK(r->support == AP_OK && !strcmp(r->extension, "png"));
    CHECK(ap_resource_export(r, destination, output, sizeof(output)) == AP_OK);
    png_pixels(output, width, height, expected);
    CHECK(unlink(output) == 0); ap_resource_list_free(&list);
}

static void test_textures(void) {
    /* Preserve row order, alpha and image dimensions while removing texture padding. */
    const unsigned char rgba[] = {255,0,0,255, 0,255,0,128, 0,0,255,255, 1,2,3,4,
                                  255,255,255,255, 0,0,0,0, 255,255,0,255, 1,2,3,4};
    const unsigned char cropped[] = {255,0,0,255, 0,255,0,128, 0,0,255,255,
                                     255,255,255,255, 0,0,0,0, 255,255,0,255};
    for (unsigned v = 1; v <= 4; ++v) {
        texture(0, 0, v, UINT32_MAX, 4, 2, 3, 2, rgba, sizeof(rgba), v > 1); export_texture(3, 2, cropped);
    }
    const unsigned char gray[] = {12,90}, gray_rgba[] = {12,12,12,255, 90,90,90,255};
    texture(9, 0, 2, UINT32_MAX, 2, 1, 2, 1, gray, 2, 1); export_texture(2, 1, gray_rgba);
    const unsigned char ga[] = {128,90}, ga_rgba[] = {90,90,90,128};
    texture(8, 0, 2, UINT32_MAX, 1, 1, 1, 1, ga, 2, 0); export_texture(1, 1, ga_rgba);

    unsigned char dxt1[] = {0x00,0xf8, 0x1f,0x00, 0xe4,0xe4,0xe4,0xe4};
    unsigned char expected[64];
    const unsigned char colors[] = {255,0,0,255, 0,0,255,255, 170,0,85,255, 85,0,170,255};
    for (int y = 0; y < 4; ++y) memcpy(expected + y * 16, colors, 16);
    texture(7, 0, 2, UINT32_MAX, 4, 4, 4, 4, dxt1, sizeof(dxt1), 1); export_texture(4, 4, expected);
    dxt1[0] = 0x1f; dxt1[1] = 0; dxt1[2] = 0; dxt1[3] = 0xf8;
    const unsigned char transparent[] = {0,0,255,255, 255,0,0,255, 127,0,127,255, 0,0,0,0};
    texture(7, 0, 1, UINT32_MAX, 4, 4, 4, 1, dxt1, sizeof(dxt1), 0); export_texture(4, 1, transparent);
    unsigned char block[16] = {0};
    for (int i = 0; i < 8; ++i) block[i] = (unsigned char)((2*i) | ((2*i+1) << 4));
    block[8] = 0; block[9] = 0xf8; block[10] = 0x1f; block[11] = 0;
    memset(expected, 0, sizeof(expected));
    for (int i = 0; i < 16; ++i) { expected[i*4] = 255; expected[i*4+3] = i * 17; }
    texture(6, 0, 2, UINT32_MAX, 4, 4, 4, 4, block, sizeof(block), 1); export_texture(4, 4, expected);
    for (int mode = 0; mode < 2; ++mode) {
        block[0] = mode ? 0 : 255; block[1] = mode ? 255 : 0;
        uint64_t bits = 0;
        for (int i = 0; i < 16; ++i) bits |= (uint64_t)(i % 8) << (3 * i);
        for (int i = 0; i < 6; ++i) block[i+2] = (bits >> (8*i)) & 255;
        const unsigned char alphas[2][8] = {{255,0,218,182,145,109,72,36}, {0,255,51,102,153,204,0,255}};
        for (int i = 0; i < 16; ++i) expected[i*4+3] = alphas[mode][i%8];
        texture(4, 0, 2, UINT32_MAX, 4, 4, 4, 4, block, sizeof(block), 0); export_texture(4, 4, expected);
    }
    const unsigned char video[] = {0,0,0,24,'f','t','y','p','i','s','o','m',0,1,2,3};
    for (unsigned v = 3; v <= 4; ++v) {
        texture(0, 32, v, UINT32_MAX, 1920, 1080, 1920, 1080, video, sizeof(video), v == 3);
        ap_resource_list list = {0}; CHECK(ap_resources_read(project, &list) == AP_OK);
        ap_resource *r = lookup(&list, "image.tex"); CHECK(!strcmp(r->extension, "mp4"));
        CHECK(ap_resource_export(r, destination, output, sizeof(output)) == AP_OK);
        equal_file(output, video, sizeof(video)); CHECK(unlink(output) == 0); ap_resource_list_free(&list);
    }
    /* Embedded PNG bytes, including metadata, survive unchanged. */
    texture(0, 0, 1, UINT32_MAX, 4, 2, 3, 2, rgba, sizeof(rgba), 0);
    ap_resource_list list = {0}; CHECK(ap_resources_read(project, &list) == AP_OK);
    CHECK(ap_resource_export(lookup(&list, "image.tex"), destination, output, sizeof(output)) == AP_OK);
    ap_resource_list_free(&list);
    FILE *f = fopen(output, "rb"); CHECK(f && fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f); CHECK(size > 0); rewind(f);
    unsigned char *png = malloc(size); CHECK(png && fread(png, 1, size, f) == (size_t)size); fclose(f); CHECK(unlink(output) == 0);
    texture(0, 0, 3, 13, 3, 2, 3, 2, png, (unsigned)size, 0);
    CHECK(ap_resources_read(project, &list) == AP_OK);
    CHECK(ap_resource_export(lookup(&list, "image.tex"), destination, output, sizeof(output)) == AP_OK);
    equal_file(output, png, size); free(png); CHECK(unlink(output) == 0); ap_resource_list_free(&list);
    for (int variant = 0; variant < 5; ++variant) {
        texture(variant == 1 ? 999 : 0, variant == 0 ? 4 : 0, 2, UINT32_MAX,
                variant == 2 ? 20000 : 4, 2, 3, 2, rgba, sizeof(rgba), 0);
        if (variant == 3) CHECK(truncate(path, 40) == 0);
        if (variant == 4) CHECK(truncate(path, 90) == 0);
        CHECK(ap_resources_read(project, &list) == AP_OK);
        ap_resource *r = lookup(&list, "image.tex"); CHECK(r->support != AP_OK);
        CHECK(ap_resource_export(r, destination, output, sizeof(output)) == r->support);
        CHECK(files(destination) == 0); ap_resource_list_free(&list);
    }
    CHECK(unlink(path) == 0);
}

static void package(const char *const *names, const void *const *data, const size_t *sizes, unsigned count) {
    snprintf(path, sizeof(path), "%s/scene.pkg", project);
    FILE *f = fopen(path, "wb"); CHECK(f);
    u32(f, 8); CHECK(fwrite("PKGV0024", 1, 8, f) == 8); u32(f, count);
    uint32_t offset = 0;
    for (unsigned i = 0; i < count; ++i) {
        u32(f, (uint32_t)strlen(names[i])); CHECK(fputs(names[i], f) >= 0);
        u32(f, offset); u32(f, (uint32_t)sizes[i]); offset += sizes[i];
    }
    for (unsigned i = 0; i < count; ++i) CHECK(fwrite(data[i], 1, sizes[i], f) == sizes[i]);
    CHECK(fclose(f) == 0);
}
static int cancel_after(void *context) { return --*(int *)context <= 0; }

static void test_resources(void) {
    const unsigned char rgba[] = {23,45,67,89};
    texture(0, 0, 2, UINT32_MAX, 1, 1, 1, 1, rgba, 4, 1);
    FILE *f = fopen(path, "rb"); CHECK(f && fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f); CHECK(size > 0); rewind(f);
    unsigned char *tex = malloc(size); CHECK(tex && fread(tex, 1, size, f) == (size_t)size); fclose(f); CHECK(unlink(path) == 0);
    const char *names[] = {"scene.json", "materials\\wall.tex", "videos/movie.MP4", "preview.png"};
    const void *data[] = {"{}", tex, "video\0payload", "packed preview"};
    size_t sizes[] = {2, (size_t)size, 13, 14};
    package(names, data, sizes, 4); free(tex);
    snprintf(path, sizeof(path), "%s/preview.png", project); write_file(path, "preview", 7);
    snprintf(path, sizeof(path), "%s/loop", project); CHECK(symlink(project, path) == 0);
    snprintf(path, sizeof(path), "%s/link.mp4", project); CHECK(symlink("/etc/passwd", path) == 0);
    snprintf(path, sizeof(path), "%s/fifo.png", project); CHECK(mkfifo(path, 0600) == 0);
    ap_resource_list list = {0}; CHECK(ap_resources_read(project, &list) == AP_OK && list.count == 3);
    ap_resource *image = lookup(&list, "materials/wall.tex"), *video = lookup(&list, "videos/movie.MP4"), *preview = lookup(&list, "preview.png");
    CHECK(image->packed && video->packed && preview->preview && !preview->packed);
    CHECK(ap_resource_export(image, destination, output, sizeof(output)) == AP_OK);
    png_pixels(output, 1, 1, rgba); CHECK(unlink(output) == 0);
    CHECK(ap_resource_export(video, destination, output, sizeof(output)) == AP_OK); equal_file(output, data[2], sizes[2]);
    char first[4096]; strcpy(first, output);
    CHECK(ap_resource_export(video, destination, output, sizeof(output)) == AP_OK);
    CHECK(strcmp(output, first) && strstr(output, "(1)")); equal_file(first, data[2], sizes[2]);
    CHECK(unlink(first) == 0 && unlink(output) == 0);
    CHECK(ap_resource_export(preview, project, output, sizeof(output)) == AP_OK);
    CHECK(strstr(output, "preview (1).png")); equal_file(preview->source, "preview", 7); CHECK(unlink(output) == 0);
    CHECK(ap_resource_export(preview, destination, output, 2) == AP_INVALID && !*output && files(destination) == 0);
    CHECK(ap_resource_export(preview, "/no-such-archpaper-directory", output, sizeof(output)) == AP_NOT_FOUND);
    snprintf(path, sizeof(path), "%s/preview.png", destination); CHECK(symlink(preview->source, path) == 0);
    CHECK(ap_resource_export(preview, destination, output, sizeof(output)) == AP_OK);
    CHECK(strstr(output, "(1)")); equal_file(preview->source, "preview", 7); CHECK(unlink(output) == 0 && unlink(path) == 0);
    int ticks = 1; ap_process_set_cancel_check(cancel_after, &ticks);
    CHECK(ap_resource_export(video, destination, output, sizeof(output)) == AP_CANCELLED && files(destination) == 0);
    ap_process_set_cancel_check(NULL, NULL);
    /* Cancel after the temporary file has been opened and the payload copied. */
    ticks = 3; ap_process_set_cancel_check(cancel_after, &ticks);
    CHECK(ap_resource_export(video, destination, output, sizeof(output)) == AP_CANCELLED && files(destination) == 0);
    ap_process_set_cancel_check(NULL, NULL);
    write_file(preview->source, "changed", 7);
    struct timespec times[2] = {{1,0},{1,0}}; CHECK(utimensat(AT_FDCWD, preview->source, times, 0) == 0);
    CHECK(ap_resource_export(preview, destination, output, sizeof(output)) == AP_BUSY);
    ap_resource_list_free(&list);
    snprintf(path, sizeof(path), "%s/scene.pkg", project); CHECK(truncate(path, 30) == 0);
    CHECK(ap_resources_read(project, &list) == AP_INVALID && !list.count && !list.items);
    const char *bad[] = {"../escape.png", "/absolute.png", "dir\\..\\escape.png", "C:\\escape.png", "dir//bad.png"};
    const void *dummy[] = {"bad"}; size_t length[] = {3};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        package(&bad[i], dummy, length, 1);
        CHECK(ap_resources_read(project, &list) == AP_INVALID && !list.items);
    }
    const char *valid[] = {"image.png"}; package(valid, dummy, length, 1);
    CHECK(truncate(path, 38) == 0);
    CHECK(ap_resources_read(project, &list) == AP_INVALID && !list.items);
    ticks = 1; ap_process_set_cancel_check(cancel_after, &ticks);
    CHECK(ap_resources_read(project, &list) == AP_CANCELLED && !list.items);
    ap_process_set_cancel_check(NULL, NULL);
}
static int remove_entry(const char *name, const struct stat *st, int type, struct FTW *state) {
    (void)st; (void)type; (void)state; return remove(name);
}
int main(void) {
    CHECK(mkdtemp(root));
    snprintf(project, sizeof(project), "%s/project with spaces", root);
    snprintf(destination, sizeof(destination), "%s/exported", root);
    CHECK(mkdir(project, 0700) == 0 && mkdir(destination, 0700) == 0);
    snprintf(path, sizeof(path), "%s/project.json", project);
    const char *json = "{\"type\":\"scene\",\"file\":\"scene.json\",\"preview\":\"preview.png\"}";
    write_file(path, json, strlen(json));
    test_textures(); test_resources();
    CHECK(nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0);
    puts("Resource export tests passed");
    return 0;
}
