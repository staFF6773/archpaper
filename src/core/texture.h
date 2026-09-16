#ifndef ARCHPAPER_TEXTURE_H
#define ARCHPAPER_TEXTURE_H

#include <stdio.h>
#include <stdint.h>
#include "archpaper/error.h"

/* A bounded slice of a loose file or PKGV entry. Internal export implementation. */
typedef struct { FILE *file; uint64_t left; } ap_slice;
int ap_slice_read(ap_slice *s, void *out, size_t size);
int ap_slice_u32(ap_slice *s, uint32_t *out);
ap_result ap_slice_copy(ap_slice *s, FILE *out);
/* Inspect header only when out == NULL, otherwise write decoded PNG or original
 * embedded media. Only the first/largest mip of a single static image is used;
 * animated/sprite textures are explicitly unsupported. */
ap_result ap_texture_export(ap_slice *s, FILE *out, char extension[8]);

#endif
