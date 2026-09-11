#ifndef ARCHPAPER_STORAGE_H
#define ARCHPAPER_STORAGE_H

#include <stddef.h>
#include <stdio.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All paths are written to caller-owned buffers. */
ap_result ap_config_path(const char *name, char *out, size_t size);
ap_result ap_cache_path(const char *name, char *out, size_t size);
ap_result ap_runtime_path(const char *name, char *out, size_t size);
ap_result ap_mkdirs(const char *path);
ap_result ap_copy_string(char *out, size_t size, const char *value);
/* Writer must report failures; temporary files live next to the destination.
 * The destination is replaced only after a successful flush/fsync/close. */
typedef ap_result (*ap_file_writer)(FILE *file, const void *data);
ap_result ap_write_atomic(const char *path, ap_file_writer writer, const void *data);

#ifdef __cplusplus
}
#endif
#endif
