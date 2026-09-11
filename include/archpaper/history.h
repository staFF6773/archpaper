#ifndef ARCHPAPER_HISTORY_H
#define ARCHPAPER_HISTORY_H

#include "library.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AP_FAVORITES, AP_RECENT } ap_history_kind;
/* Returns existing regular files. Unavailable paths remain stored on disk. */
ap_result ap_history_load(ap_history_kind kind, ap_path_list *out);
ap_result ap_favorite_toggle(const char *path, int *is_favorite);
ap_result ap_recent_add(const char *path);

#ifdef __cplusplus
}
#endif
#endif
