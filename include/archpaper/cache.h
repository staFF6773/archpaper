#ifndef ARCHPAPER_CACHE_H
#define ARCHPAPER_CACHE_H

#include <stddef.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes either the original path or a completed cached conversion to out.
 * No shared path buffers. Missing converters/failed conversions fall back to
 * the original media. Conversion is bounded to 120 seconds. */
ap_result ap_cache_prepare(const char *path, const char *quality, char *out, size_t size);
/* Extract one video frame to a caller-owned PNG path. */
ap_result ap_thumbnail_extract(const char *path, const char *output);

#ifdef __cplusplus
}
#endif
#endif
