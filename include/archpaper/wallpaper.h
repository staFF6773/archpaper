#ifndef ARCHPAPER_WALLPAPER_H
#define ARCHPAPER_WALLPAPER_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int applied;
    backend_t backend;
    ap_result persistence;
    ap_result theme;
    int wallust_missing;
} ap_apply_result;

enum { AP_APPLY_SAVE_OPTIONS = 1u };

/* Shared CLI/GUI/daemon transaction. Validates and resolves an absolute path,
 * chooses a compatible backend, applies, then records config/history and runs
 * wallust followed by the hook. AP_OK means the wallpaper was applied; inspect
 * persistence/theme for non-fatal post-apply failures. Synchronous and bounded;
 * a GUI should call this on its worker thread with a configuration snapshot.
 * flags=0 records only the last wallpaper/history, preserving current settings.
 * AP_APPLY_SAVE_OPTIONS also persists explicit CLI option overrides. */
ap_result ap_wallpaper_apply(const char *path, const config_t *options, unsigned flags, ap_apply_result *out);
ap_result ap_wallpaper_clear(void);

#ifdef __cplusplus
}
#endif
#endif
