#ifndef ARCHPAPER_EXPORT_H
#define ARCHPAPER_EXPORT_H

#include <stdint.h>
#include <stddef.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AP_RESOURCE_IMAGE, AP_RESOURCE_VIDEO, AP_RESOURCE_TEXTURE } ap_resource_kind;
typedef struct {
    char *name;                 /* Project-relative display name. */
    char *source;               /* Canonical loose file or package. */
    uint64_t offset, size;
    uint64_t device, inode, source_size;
    int64_t modified, modified_ns;
    ap_resource_kind kind;
    int packed, preview;
    ap_result support;         /* AP_UNSUPPORTED/INVALID for unreadable TEX variants. */
    char extension[8];          /* Actual output format, including embedded TEX media. */
} ap_resource;
typedef struct {
    ap_resource *items;
    size_t count;
} ap_resource_list;

/* Caller-owned list, including unsupported textures for display. On failure,
 * out is empty. Scans local media and PKGV packages without following symlinks.
 * Both operations honor the per-thread ap_process cancellation callback. */
ap_result ap_resources_read(const char *project, ap_resource_list *out);
void ap_resource_list_free(ap_resource_list *list);
/* Export one resource to an existing directory. Never overwrite: conflicting
 * basenames receive a numeric suffix. out receives the completed absolute path.
 * Files are published atomically; failed/cancelled temporary files are removed.
 * Source changes since listing return AP_BUSY; refresh the list before retrying. */
ap_result ap_resource_export(const ap_resource *resource, const char *directory,
                             char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
