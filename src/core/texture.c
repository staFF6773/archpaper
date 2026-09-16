#define _POSIX_C_SOURCE 200809L
#include "texture.h"
#include "archpaper/process.h"

#include <lz4.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

/* Format layout reference: RePKG's TEXV0005/TEXI0001 and TEXB0001–0004
 * readers (https://github.com/notscuffed/repkg). No renderer is needed. */
#define MAX_TEXTURE_BYTES (256u * 1024u * 1024u)

int ap_slice_read(ap_slice *s, void *out, size_t size) {
    if (size > s->left || fread(out, 1, size, s->file) != size) return 0;
    s->left -= size;
    return 1;
}

int ap_slice_u32(ap_slice *s, uint32_t *out) {
    unsigned char b[4];
    if (!ap_slice_read(s, b, sizeof(b))) return 0;
    *out = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    return 1;
}

ap_result ap_slice_copy(ap_slice *s, FILE *out) {
    unsigned char buffer[65536];
    while (s->left) {
        if (ap_process_cancel_requested()) return AP_CANCELLED;
        size_t n = s->left < sizeof(buffer) ? (size_t)s->left : sizeof(buffer);
        if (!ap_slice_read(s, buffer, n) || fwrite(buffer, 1, n, out) != n) return AP_IO;
    }
    return AP_OK;
}

static int magic(ap_slice *s, const char *text) {
    char b[9];
    return ap_slice_read(s, b, sizeof(b)) && !memcmp(b, text, sizeof(b));
}

static void rgb565(unsigned value, unsigned char *out) {
    unsigned r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;
    out[0] = (r << 3) | (r >> 2);
    out[1] = (g << 2) | (g >> 4);
    out[2] = (b << 3) | (b >> 2);
    out[3] = 255;
}

static void block_decode(const unsigned char *in, unsigned format, unsigned char pixels[16][4]) {
    const unsigned char *color = in + (format == 7 ? 0 : 8);
    unsigned c0 = color[0] | color[1] << 8, c1 = color[2] | color[3] << 8;
    unsigned char colors[4][4];
    rgb565(c0, colors[0]); rgb565(c1, colors[1]);
    int transparent = format == 7 && c0 <= c1;
    for (int c = 0; c < 3; ++c) {
        colors[2][c] = transparent ? (colors[0][c] + colors[1][c]) / 2
                                  : (2 * colors[0][c] + colors[1][c]) / 3;
        colors[3][c] = transparent ? 0 : (colors[0][c] + 2 * colors[1][c]) / 3;
    }
    colors[2][3] = 255; colors[3][3] = transparent ? 0 : 255;
    unsigned char alpha[8] = {in[0], in[1]};
    uint64_t selectors = 0;
    if (format == 4) {
        int steps = alpha[0] > alpha[1] ? 7 : 5;
        for (int i = 1; i < steps; ++i)
            alpha[i + 1] = ((steps - i) * alpha[0] + i * alpha[1]) / steps;
        if (steps == 5) { alpha[6] = 0; alpha[7] = 255; }
        for (int i = 0; i < 6; ++i) selectors |= (uint64_t)in[i + 2] << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        memcpy(pixels[i], colors[(color[4 + i / 4] >> (2 * (i % 4))) & 3], 4);
        if (format == 6) pixels[i][3] = ((in[i / 2] >> (4 * (i % 2))) & 15) * 17;
        if (format == 4) pixels[i][3] = alpha[(selectors >> (3 * i)) & 7];
    }
}

/* Write rows directly to avoid another full-resolution RGBA allocation. */
static ap_result write_pixels(FILE *out, const unsigned char *data, uint32_t format,
                              uint32_t width, uint32_t image_width, uint32_t image_height) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return AP_NOMEM;
    png_infop info = png_create_info_struct(png);
    unsigned char *row = malloc((size_t)image_width * 4);
    if (!info || !row) { free(row); png_destroy_write_struct(&png, &info); return AP_NOMEM; }
    if (setjmp(png_jmpbuf(png))) { free(row); png_destroy_write_struct(&png, &info); return AP_IO; }
    png_init_io(png, out);
    png_set_IHDR(png, info, image_width, image_height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    ap_result rc = AP_OK;
    for (uint32_t y = 0; y < image_height; ++y) {
        if (ap_process_cancel_requested()) { rc = AP_CANCELLED; break; }
        if (format == 4 || format == 6 || format == 7) {
            size_t block_size = format == 7 ? 8 : 16;
            for (uint32_t x = 0; x < image_width; x += 4) {
                unsigned char pixels[16][4];
                block_decode(data + ((size_t)(y / 4) * ((width + 3) / 4) + x / 4) * block_size, format, pixels);
                uint32_t n = image_width - x < 4 ? image_width - x : 4;
                memcpy(row + x * 4, pixels[(y % 4) * 4], n * 4);
            }
        } else {
            unsigned channels = format == 0 ? 4 : format == 8 ? 2 : 1;
            for (uint32_t x = 0; x < image_width; ++x) {
                const unsigned char *p = data + ((size_t)y * width + x) * channels;
                if (format == 0) memcpy(row + x * 4, p, 4);
                else { /* R8/RG88 are grayscale / grayscale-alpha textures. */
                    row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = p[format == 8 ? 1 : 0];
                    row[x * 4 + 3] = format == 8 ? p[0] : 255;
                }
            }
        }
        png_write_row(png, row);
    }
    if (rc == AP_OK) png_write_end(png, info);
    free(row); png_destroy_write_struct(&png, &info);
    return rc;
}

ap_result ap_texture_export(ap_slice *s, FILE *out, char extension[8]) {
    uint32_t format, flags, tw, th, iw, ih, unused, count, version, embedded = UINT32_MAX, video = 0;
    char tag[9];
    if (!magic(s, "TEXV0005") || !magic(s, "TEXI0001")) return AP_UNSUPPORTED;
    if (!ap_slice_u32(s, &format) || !ap_slice_u32(s, &flags) ||
        !ap_slice_u32(s, &tw) || !ap_slice_u32(s, &th) || !ap_slice_u32(s, &iw) ||
        !ap_slice_u32(s, &ih) || !ap_slice_u32(s, &unused) || !ap_slice_read(s, tag, 9)) return AP_INVALID;
    if (memcmp(tag, "TEXB000", 7) || tag[7] < '1' || tag[7] > '4' || tag[8]) return AP_UNSUPPORTED;
    version = tag[7] - '0';
    if (!ap_slice_u32(s, &count)) return AP_INVALID;
    /* Flag 4 denotes a sprite animation; exporting its atlas as a wallpaper is misleading. */
    if (count != 1 || (flags & 4)) return AP_UNSUPPORTED;
    if (version >= 3 && !ap_slice_u32(s, &embedded)) return AP_INVALID;
    if (version == 4 && !ap_slice_u32(s, &video)) return AP_INVALID;
    const char *ext = NULL;
    if (embedded == UINT32_MAX && (video == 1 || (flags & 32))) ext = "mp4";
    else if (embedded != UINT32_MAX) {
        switch (embedded) {
        case 0: ext = "bmp"; break;
        case 2: ext = "jpg"; break;
        case 13: ext = "png"; break;
        case 18: ext = "tiff"; break;
        case 25: ext = "gif"; break;
        case 35: ext = "webp"; break;
        default: return AP_UNSUPPORTED;
        }
    }
    if (!ext && format != 0 && format != 4 && format != 6 && format != 7 && format != 8 && format != 9)
        return AP_UNSUPPORTED;
    uint32_t mips, width, height, compressed = 0, raw_size = 0, bytes;
    if (!ap_slice_u32(s, &mips) || !mips || mips > 32) return AP_INVALID;
    if (version == 4 && video == 1 && embedded == UINT32_MAX) {
        uint32_t a, b; unsigned char c; size_t n = 0;
        if (!ap_slice_u32(s, &a) || !ap_slice_u32(s, &b) || a != 1 || b != 2) return AP_UNSUPPORTED;
        do { if (++n > 65536 || !ap_slice_read(s, &c, 1)) return AP_INVALID; } while (c);
        if (!ap_slice_u32(s, &a) || a != 1) return AP_UNSUPPORTED;
    }
    if (!ap_slice_u32(s, &width) || !ap_slice_u32(s, &height)) return AP_INVALID;
    if (version >= 2 && (!ap_slice_u32(s, &compressed) || !ap_slice_u32(s, &raw_size))) return AP_INVALID;
    if (!ap_slice_u32(s, &bytes) || !bytes || bytes > s->left || compressed > 1) return AP_INVALID;
    strcpy(extension, ext ? ext : "png");
    if (!ext) {
        if (!width || !height || width > 16384 || height > 16384 || !iw || !ih || iw > width || ih > height)
            return AP_INVALID;
        uint64_t expected = (format == 4 || format == 6 || format == 7)
            ? (uint64_t)((width + 3) / 4) * ((height + 3) / 4) * (format == 7 ? 8 : 16)
            : (uint64_t)width * height * (format == 0 ? 4 : format == 8 ? 2 : 1);
        if (expected > MAX_TEXTURE_BYTES) return AP_UNSUPPORTED;
        if (expected != (compressed ? raw_size : bytes)) return AP_INVALID;
    }
    if (compressed && (!raw_size || raw_size > MAX_TEXTURE_BYTES || bytes > MAX_TEXTURE_BYTES)) return AP_UNSUPPORTED;
    if (!out) return AP_OK;
    if (ap_process_cancel_requested()) return AP_CANCELLED;
    s->left = bytes;
    if (ext && !compressed) return ap_slice_copy(s, out);
    unsigned char *data = malloc(bytes);
    if (!data) return AP_NOMEM;
    ap_result rc = AP_OK;
    if (!ap_slice_read(s, data, bytes)) { rc = AP_IO; goto done; }
    if (compressed) {
        unsigned char *raw = malloc(raw_size);
        if (!raw) { rc = AP_NOMEM; goto done; }
        int n = LZ4_decompress_safe((const char *)data, (char *)raw, (int)bytes, (int)raw_size);
        free(data); data = raw;
        if (n != (int)raw_size) { rc = AP_INVALID; goto done; }
    }
    if (ap_process_cancel_requested()) rc = AP_CANCELLED;
    else if (ext) rc = fwrite(data, 1, raw_size, out) == raw_size ? AP_OK : AP_IO;
    else rc = write_pixels(out, data, format, width, iw, ih);
done:
    free(data);
    return rc;
}
