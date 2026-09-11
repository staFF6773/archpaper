#ifndef ARCHPAPER_LIBRARY_H
#define ARCHPAPER_LIBRARY_H

#include <stddef.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize with {0}. Lists own their strings; release with ap_path_list_free.
 * Scan/load replace the destination only on success. */
typedef struct { char **paths; size_t count; size_t capacity; } ap_path_list;
void ap_path_list_free(ap_path_list *list);
ap_result ap_path_list_append(ap_path_list *list, const char *path);
void ap_path_list_remove(ap_path_list *list, const char *path);
int ap_path_list_contains(const ap_path_list *list, const char *path);
ap_result ap_library_scan(const char *directory, ap_path_list *out);
/* Returns a malloc-owned absolute path; caller must free it. */
ap_result ap_library_random(const char *directory, char **out);
ap_result ap_random_index(size_t count, size_t *out);
/* Filename filter, case-insensitive for ASCII. Empty text matches everything. */
int ap_library_matches(const char *path, const char *text);

#ifdef __cplusplus
}
#endif
#endif
